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
#include "cle_instance.h"

struct _rt_code
{
	struct _rt_code* next;
	char* code;
	st_ptr home;
	st_ptr strings;
	struct _body_ body;
};

struct _rt_callframe;

struct _rt_stack
{
	union 
	{
		double dbl;
		struct _rt_callframe* cfp;
		struct _rt_stack* var;
		st_ptr single_ptr;
		struct
		{
			st_ptr ptr;
			st_ptr obj;
		};
		struct
		{
			ulong prop_id;
			st_ptr prop_obj;
		};
		struct
		{
			struct _rt_code* code;
			st_ptr code_obj;
		};
		struct
		{
			cle_pipe* out;
			void* outdata;
		};
	};
	uchar type;
	uchar flags;	// JIT-flags
};

// stack-element types
#define STACK_NULL 0
#define STACK_DOUBLE 1
#define STACK_CALL 2
#define STACK_OBJ 3
#define STACK_OUTPUT 4
#define STACK_REF 5
#define STACK_CODE 6
#define STACK_PTR 7
#define STACK_PROP 8

struct _rt_callframe
{
	struct _rt_callframe* parent;
	struct _rt_code* code;
	char* pc;
	struct _rt_stack* vars;
	struct _rt_stack* sp;
	st_ptr object;
	char is_expr;
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

static struct _rt_callframe* _rt_newcall(struct _rt_invocation* inv, struct _rt_code* code, st_ptr* object, int is_expr)
{
	struct _rt_callframe* cf =
	(struct _rt_callframe*)tk_alloc(inv->t,sizeof(struct _rt_callframe)
		+ (code->body.maxvars + code->body.maxstack) * sizeof(struct _rt_stack));

	// push
	cf->parent = inv->top;
	inv->top = cf;

	cf->pc = code->code;
	cf->code = code;
	cf->object = *object;

	cf->vars = (struct _rt_stack*)((char*)cf + sizeof(struct _rt_callframe));

	// clear all vars
	memset(cf->vars,0,code->body.maxvars * sizeof(struct _rt_stack));
	cf->sp = cf->vars + code->body.maxvars + code->body.maxstack;

	cf->is_expr = is_expr;
	return cf;
}

static int _rt_equal(struct _rt_stack* op1, struct _rt_stack* op2)
{
	if(op1->type != op2->type)
		return 0;

	return 0;
}

static int _rt_compare(struct _rt_stack* op1, struct _rt_stack* op2)
{
	// illegal compare
	op2->type = STACK_NULL;
	return 0;
}

static void _rt_get(struct _rt_invocation* inv, struct _rt_stack** sp)
{
	struct _rt_stack top = **sp;
	char buffer[HEAD_SIZE];

	// look for header
	if(st_get(inv->t,&top.ptr,buffer,HEAD_SIZE) != -2 || buffer[0] != 0)
		return;

	switch(buffer[1])
	{
	case 'M':	// method
		(*sp)->code = _rt_load_code(inv,top.ptr);
		(*sp)->code_obj = top.obj;
		(*sp)->type = STACK_CODE;
		break;
	case 'E':	// expr
		if(top.obj.pg != inv->top->object.pg || top.obj.key != inv->top->object.key)
			return;
		// eval expr
		inv->top = _rt_newcall(inv,_rt_load_code(inv,top.ptr),&top.obj,1);

		(*sp)->type = STACK_NULL;
		inv->top->sp--;
		inv->top->sp->type = STACK_REF;
		inv->top->sp->var = *sp;
		*sp = inv->top->sp;
		break;
	case 'R':	// ref (oid)
		if(top.obj.pg != inv->top->object.pg || top.obj.key != inv->top->object.key)
			return;
		(*sp)->obj = inv->hdl->instance;
		if(st_move(inv->t,&(*sp)->obj,HEAD_OID,HEAD_SIZE) != 0)
			return;
		// copy_move sp-obj , top.ptr
		(*sp)->ptr = (*sp)->obj;
		(*sp)->type = STACK_OBJ;
		break;
	case 'y':	// property
		if(top.obj.pg != inv->top->object.pg || top.obj.key != inv->top->object.key)
			return;
		if(st_get(inv->t,&top.ptr,(char*)&(*sp)->prop_id,PROPERTY_SIZE) != -1)
			return;
		(*sp)->prop_obj = top.obj;
		(*sp)->type = STACK_PROP;
	}
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
	struct _rt_stack* sp = inv->top->sp;
	ushort tmpushort;
	while(1)
	{
		switch(*inv->top->pc++)
		{
		case OP_NOOP:
			break;
		case OP_NULL:
			sp--;
			sp->type = STACK_NULL;
			break;
		case OP_POP:
			sp++;
			break;
		case OP_ADD:
			if(sp[0].type == STACK_DOUBLE && sp[1].type == STACK_DOUBLE)
				sp[1].dbl += sp[0].dbl;
			else
				sp[1].type = STACK_NULL;
			sp++;
			break;
		case OP_SUB:
			if(sp[0].type == STACK_DOUBLE && sp[1].type == STACK_DOUBLE)
				sp[1].dbl -= sp[0].dbl;
			else
				sp[1].type = STACK_NULL;
			sp++;
			break;
		case OP_MUL:
			if(sp[0].type == STACK_DOUBLE && sp[1].type == STACK_DOUBLE)
				sp[1].dbl *= sp[0].dbl;
			else
				sp[1].type = STACK_NULL;
			sp++;
			break;
		case OP_DIV:
			if(sp[0].type == STACK_DOUBLE && sp[1].type == STACK_DOUBLE && sp[0].dbl != 0)
				sp[1].dbl /= sp[0].dbl;
			else
				sp[1].type = STACK_NULL;
			sp++;
			break;

		case OP_EQ:
			sp[1].dbl = _rt_equal(sp,sp + 1);
			sp[1].type = STACK_DOUBLE;
			sp++;
			break;
		case OP_NE:
			sp[1].dbl = _rt_equal(sp,sp + 1) == 0;
			sp[1].type = STACK_DOUBLE;
			sp++;
			break;
		case OP_GE:
			sp[1].dbl = _rt_compare(sp,sp + 1) >= 0;
			sp++;
			break;
		case OP_GT:
			sp[1].dbl = _rt_compare(sp,sp + 1) > 0;
			sp++;
			break;
		case OP_LE:
			sp[1].dbl = _rt_compare(sp,sp + 1) <= 0;
			sp++;
			break;
		case OP_LT:
			sp[1].dbl = _rt_compare(sp,sp + 1) < 0;
			sp++;
			break;

		case OP_NOT:
			if(sp[0].type == STACK_DOUBLE)
				sp[0].dbl = (sp[0].dbl == 0);
			else
				sp[0].type = STACK_NULL;
			break;
		case OP_BNZ:
			// emit Is (branch forward conditional)
			tmpushort = *((ushort*)inv->top->pc);
			inv->top->pc += sizeof(ushort);
			if((sp[0].type != STACK_NULL) && (
				(sp[0].type == STACK_DOUBLE && sp[0].dbl != 0) ||
				(sp[0].type == STACK_PTR && st_is_empty(&sp[0].single_ptr) == 0)))
				inv->top->pc += tmpushort;
			sp++;
		case OP_BZ:
			// emit Is (branch forward conditional)
			tmpushort = *((ushort*)inv->top->pc);
			inv->top->pc += sizeof(ushort);
			if((sp[0].type == STACK_NULL) ||
				(sp[0].type == STACK_DOUBLE && sp[0].dbl == 0) ||
				(sp[0].type == STACK_PTR && st_is_empty(&sp[0].single_ptr)))
				inv->top->pc += tmpushort;
			sp++;
			break;
		case OP_BR:
			// emit Is (branch forward)
			tmpushort = *((ushort*)inv->top->pc);
			inv->top->pc += tmpushort - sizeof(ushort);
			break;

		case OP_LOOP:	// JIT-HOOK 
			// emit Is (branch back)
			tmpushort = *((ushort*)inv->top->pc);
			inv->top->pc -= tmpushort - sizeof(ushort);
			break;

		case OP_FREE:
			// emit Ic2 (byte,byte)
			tmpushort = *inv->top->pc++;
			while(tmpushort-- > 0)
			{
				inv->top->vars[tmpushort + *inv->top->pc].type = STACK_NULL;
			}
			inv->top->pc++;
			break;

		case OP_IMM:
			// emit II (imm short)
			sp--;
			sp->type = STACK_DOUBLE;
			sp->dbl = *((short*)inv->top->pc);
			inv->top->pc += sizeof(short);
			break;
		case OP_STR:
			// emit Is
			sp--;
			sp->type = STACK_PTR;
			sp->single_ptr = inv->top->code->strings;
			st_move(inv->t,&sp->single_ptr,inv->top->pc,sizeof(ushort));
			inv->top->pc += sizeof(ushort);
			break;
		case OP_OBJ:
			sp--;
			sp->type = STACK_OBJ;
			sp->ptr = sp->obj = inv->top->object;
			break;

		case OP_FMV:
			tmpushort = *((ushort*)inv->top->pc);
			inv->top->pc += sizeof(ushort);
			sp--;
			sp->type = STACK_OBJ;
			sp->ptr = sp->obj = inv->top->object;
			if(cle_get_property_host(inv->t,inv->hdl->instance,&sp->ptr,inv->top->pc,tmpushort) < 0)
			{
				_rt_error(inv,__LINE__);
				return;
			}
			inv->top->pc += tmpushort;
			_rt_get(inv,&sp);
			break;
		case OP_MV:
			if(sp->type != STACK_OBJ)
			{
				_rt_error(inv,__LINE__);
				return;
			}
			tmpushort = *((ushort*)inv->top->pc);
			inv->top->pc += sizeof(ushort);
			// object-root?
			if(sp->obj.pg == sp->ptr.pg && sp->obj.key == sp->ptr.key)
			{
				if(cle_get_property_host(inv->t,inv->hdl->instance,&sp->ptr,inv->top->pc,tmpushort) < 0)
				{
					_rt_error(inv,__LINE__);
					return;
				}
			}
			else if(st_move(inv->t,&sp->ptr,inv->top->pc,tmpushort) != 0)
			{
				_rt_error(inv,__LINE__);
				return;
			}
			inv->top->pc += tmpushort;
			_rt_get(inv,&sp);
			break;

		case OP_RECV:
			sp--;
			sp->type = STACK_PTR;
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
			if(sp->type != STACK_CODE)
			{
				_rt_error(inv,__LINE__);
				return;
			}
			sp->cfp = _rt_newcall(inv,sp->code,&sp->code_obj,0);
			sp->type = STACK_CALL;
			break;
		case OP_DOCALL:
			inv->top->sp = sp + 1;
			inv->top = sp->cfp;
			*(--inv->top->sp) = *sp;	// copy output-target
			sp = inv->top->sp;
			break;
		case OP_DOCALL_N:
			inv->top = sp->cfp;
			sp->type = STACK_NULL;
			inv->top->sp--;
			inv->top->sp->type = STACK_REF;	// ref to sp-top
			inv->top->sp->var = sp;
			sp = inv->top->sp;
			break;
//		case OP_DOCALL_T:	// tail-call
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
	_rt_newcall(inv,inv->code_cache,&hdl->object,0);

	// push response-pipe
	inv->top->sp--;
	inv->top->sp->out = hdl->response;
	inv->top->sp->outdata = hdl->respdata;
	inv->top->sp->type = STACK_OUTPUT;

	// get parameters before launch?
	inv->params_before_run = inv->code_cache->body.maxparams;

	if(inv->params_before_run == 0)
		_rt_run(inv);
}

static void _rt_next(event_handler* hdl)
{
	struct _rt_invocation* inv = (struct _rt_invocation*)hdl->handler_data;

	if(inv->params_before_run == 0)
	{
		inv->top->sp--;
		inv->top->sp->single_ptr = hdl->top->pt;
		inv->top->sp->type = STACK_PTR;
	}
	else
	{
		struct _rt_stack* var = inv->top->vars + (inv->top->code->body.maxparams - inv->params_before_run);

		var->single_ptr = hdl->top->pt;
		var->type = STACK_PTR;

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
