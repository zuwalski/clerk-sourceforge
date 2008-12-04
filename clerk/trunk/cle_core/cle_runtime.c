/* 
    Clerk application and storage engine.
    Copyright (C) 2008  Lars Szuwalski

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "cle_runtime.h"
#include "cle_stream.h"

struct _rt_code
{
	struct _rt_code* next;
	char* code;
	st_ptr home;
	st_ptr strings;
	struct _body_ body;
};

struct _rt_callframe;

struct _rt_output
{
	cle_pipe* out;
	void* outdata;
};

union _rt_stack
{
	double dbl;
	st_ptr ptr;
	struct _rt_callframe* cfp;
	struct _rt_output output;
};

struct _rt_callframe
{
	struct _rt_callframe* parent;
	struct _rt_code* code;
	char* pc;
	union _rt_stack* vars;
	union _rt_stack* sp;
	st_ptr object;
};

struct _rt_invocation
{
	struct _rt_callframe* top;
	struct _rt_code* code_cache;
	task* t;
	event_handler* hdl;
	int params_before_run;
};

static void _rt_error(struct _rt_invocation* inv, uint code)
{
	cle_stream_fail(inv->hdl,"runtime",8);
}

static struct _rt_code* _rt_load_code(struct _rt_invocation* inv, st_ptr code)
{
	struct _rt_code* ret = inv->code_cache;
	st_ptr pt;
	struct _body_ body;

	// lookup in cache
	while(ret != 0)
	{
		if(ret->home.pg == code.pg && ret->home.key == code.key)
			return ret;

		ret = ret->next;
	}

	// not found -> load it and add it
	pt = code;
	if(st_move(inv->t,&pt,"B",2) != 0)
	{
		_rt_error(inv,__LINE__);
		return 0;
	}

	if(st_get(inv->t,&pt,(char*)&body,sizeof(struct _body_)) != -2)
	{
		_rt_error(inv,__LINE__);
		return 0;
	}

	ret = (struct _rt_code*)tk_alloc(inv->t,sizeof(struct _rt_code) + body.codesize - sizeof(struct _body_));

	// push
	ret->next = inv->code_cache;
	inv->code_cache = ret;

	ret->home = code;
	ret->body = body;

	ret->code = (char*)ret + sizeof(struct _rt_code);
	if(st_get(inv->t,&pt,ret->code,ret->body.codesize - sizeof(struct _body_)) != -1)
	{
		_rt_error(inv,__LINE__);
		return 0;
	}

	// get strings
	ret->strings = code;
	st_move(inv->t,&ret->strings,"S",2);

	return ret;
}

static struct _rt_callframe* _rt_newcall(struct _rt_invocation* inv, struct _rt_code* code, st_ptr* object)
{
	struct _rt_callframe* cf =
	(struct _rt_callframe*)tk_alloc(inv->t,sizeof(struct _rt_callframe)
		+ (code->body.maxvars + code->body.maxstack) * sizeof(union _rt_stack));

	// push
	cf->parent = inv->top;
	inv->top = cf;

	cf->pc = code->code;
	cf->code = code;
	cf->object = *object;

	cf->vars = (union _rt_stack*)((char*)cf + sizeof(struct _rt_callframe));

	// clear all vars
	memset(cf->vars,0,code->body.maxvars * sizeof(union _rt_stack));
	cf->sp = cf->vars + code->body.maxvars + code->body.maxstack;

	return cf;
}

/*
	stacklayout:
	0 output-target

	1 object
	2 ptr

	1 number
*/
static void _rt_run(struct _rt_invocation* inv)
{
	union _rt_stack* sp = inv->top->sp;
	while(1)
	{
		switch(*inv->top->pc++)
		{
		case OP_NOOP:
			break;
		case OP_RECV:
			sp--;
			sp->ptr.pg = 0;
			return;
		case OP_END:
			if(inv->top->parent == 0)
			{
				cle_stream_end(inv->hdl);
				return;
			}
			inv->top = inv->top->parent;
			sp = inv->top->sp;
			break;
		case OP_CALL:	// sp -> method-pointer, sp1 -> object
			sp[1].cfp = _rt_newcall(inv,_rt_load_code(inv,sp->ptr),&sp[1].ptr);
			sp++;
			break;
		case OP_DOCALL:
			inv->top->sp = sp + 1;
			inv->top = sp->cfp;
			*(--inv->top->sp) = *sp;	// copy output-target
			sp = inv->top->sp;
			break;
		case OP_DOCALL_N:
			break;
		default:
			_rt_error(inv,__LINE__);
			return;
		}
	}
}

/*
	load method from "handler" and invoke on "object"
	if method takes any number of parameters -> collect parameters in next, before invoke
*/
static void _rt_start(event_handler* hdl)
{
	struct _rt_invocation* inv = (struct _rt_invocation*)tk_alloc(hdl->instance_tk,sizeof(struct _rt_invocation));
	hdl->handler_data = inv;

	inv->t = hdl->instance_tk;
	inv->hdl = hdl;
	inv->top = 0;

	_rt_load_code(inv,hdl->handler);
	_rt_newcall(inv,inv->code_cache,&hdl->object);

	// push response-pipe
	inv->top->sp--;
	inv->top->sp->output.out = hdl->response;
	inv->top->sp->output.outdata = hdl->respdata;

	// get parameters before launch?
	inv->params_before_run = inv->code_cache->body.maxparams;

	if(inv->params_before_run == 0)
		_rt_run(inv);
}

static void _rt_next(event_handler* hdl)
{
	struct _rt_invocation* inv = (struct _rt_invocation*)hdl->handler_data;

	if(inv->params_before_run == 0)
		(inv->top->sp--)->ptr = hdl->top->pt;
	else
	{
		inv->top->vars[inv->top->code->body.maxparams - inv->params_before_run].ptr = hdl->top->pt;

		if(--inv->params_before_run != 0)
			return;
	}

	_rt_run(inv);
}

static void _rt_end(event_handler* hdl, cdat code, uint length)
{
	struct _rt_invocation* inv = (struct _rt_invocation*)hdl->handler_data;

	if(length == 0 && inv->params_before_run != 0)
		_rt_run(inv);

	hdl->response->end(hdl->respdata,code,length);
}

cle_syshandler _runtime_handler = {0,{_rt_start,_rt_next,_rt_end,cle_standard_pop,cle_standard_push,cle_standard_data,cle_standard_submit},0};
