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
#include "cle_stream.h"
#include "cle_object.h"

/*
*	The main input-interface to the running system
*	Events/messages are "pumped" in through the exported set of functions
*/

/* TODO: 
		kill submit or rework st_link etc.
		Share: handlers on "same" object should have ref to same instance (not new each time)
*/

// error-messages
static char input_underflow[] = "stream:input underflow";
static char event_not_allowed[] = "stream:event not allowed";

// structs
struct _ipt_internal
{
	event_handler* event_chain_begin;
	sys_handler_data sys;
};

// nil output-handler
static void _nil1(void* v){}
static uint _nil1x(void* v){return 0;}
static void _nil2(void* v,cdat c,uint u){}
static uint _nil2x(void* v,cdat c,uint u){return 0;}
static uint _nil2y(void* v,cdat c,uint u){return 1;}
static void _nil3(void* v,task* t,st_ptr* st){}
static cle_pipe _nil_out = {_nil1,_nil1,_nil2,_nil1x,_nil1,_nil2x,_nil3};

// async output-handler
static int _async_end(event_handler* hdl, cdat c, uint clen)
{
	// end async-task
	if(clen == 0)
		return tk_commit_task(hdl->inst.t);

	tk_drop_task(hdl->inst.t);
	return 0;
}

static cle_pipe _async_out = {_nil1,_nil1,_async_end,_nil1x,_nil1,_nil2x,_nil3};

// convenience functions for implementing the cle_pipe-interface
uint cle_standard_pop(event_handler* hdl)
{
	ptr_list* elm = hdl->top;
	if(hdl->top->link == 0)
		return 1;
	hdl->top = hdl->top->link;

	elm->link = hdl->eventdata->free;
	hdl->eventdata->free = elm;
	return 0;
}

static ptr_list* _alloc_elem(event_handler* hdl)
{
	if(hdl->eventdata->free)
	{
		ptr_list* elm = hdl->eventdata->free;
		hdl->eventdata->free = elm->link;
		return elm;
	}

	return (ptr_list*)tk_alloc(hdl->inst.t,sizeof(ptr_list),0);
}

void cle_standard_push(event_handler* hdl)
{
	ptr_list* elm = _alloc_elem(hdl);

	if(hdl->top != 0)
		elm->pt = hdl->top->pt;
	else
	{
		st_empty(hdl->inst.t,&elm->pt);
		hdl->root = elm->pt;
	}

	elm->link = hdl->top;
	hdl->top = elm;
}

uint cle_standard_data(event_handler* hdl, cdat data, uint length)
{
	return st_append(hdl->inst.t,&hdl->top->pt,data,length);
}

void cle_standard_submit(event_handler* hdl, task* t, st_ptr* from)
{
	// toplevel?
	if(hdl->top->link == 0 && hdl->top->pt.offset == hdl->root.offset && hdl->top->pt.key == hdl->root.key && hdl->top->pt.pg == hdl->root.pg)
		hdl->root = hdl->top->pt = *from;	// subst
	else
		st_link(t,&hdl->top->pt,from);
}

void cle_standard_next_done(event_handler* hdl)
{
	if(hdl->error == 0)
	{
		// done processing .. clear and ready for next input-stream
		st_empty(hdl->inst.t,&hdl->top->pt);
		hdl->root = hdl->top->pt;
	}
}

// pipeline activate All handlers
static uint _pa_pop(event_handler* hdl)
{
	do
	{
		if(cle_standard_pop(hdl))
			return 1;
		hdl = hdl->next;
	}
	while(hdl != 0);
	return 0;
}

static void _pa_push(event_handler* hdl)
{
	do
	{
		cle_standard_push(hdl);
		hdl = hdl->next;
	}
	while(hdl != 0);
}

static uint _pa_data(event_handler* hdl, cdat data, uint length)
{
	do
	{
		if(cle_standard_data(hdl,data,length))
			return 1;
		hdl = hdl->next;
	}
	while(hdl != 0);
	return 0;
}

static void _pa_submit(event_handler* hdl, task* t, st_ptr* st)
{
	do
	{
		cle_standard_submit(hdl,t,st);
		hdl = hdl->next;
	}
	while(hdl != 0);
}

static cle_pipe _pipeline_all = {cle_notify_start,cle_notify_next,cle_notify_end,_pa_pop,_pa_push,_pa_data,_pa_submit};

/* event-handler exit-functions */

/* copy-handler */
static void _cpy_start(event_handler* hdl) {hdl->response->start(hdl->respdata);}
static void _cpy_next(event_handler* hdl) {hdl->response->next(hdl->respdata);}
static void _cpy_end(event_handler* hdl,cdat c,uint u) {hdl->response->end(hdl->respdata,c,u);}
static uint _cpy_pop(event_handler* hdl) {return hdl->response->pop(hdl->respdata);}
static void _cpy_push(event_handler* hdl) {hdl->response->push(hdl->respdata);}
static uint _cpy_data(event_handler* hdl,cdat c,uint u) {return hdl->response->data(hdl->respdata,c,u);}
static void _cpy_submit(event_handler* hdl,task* t,st_ptr* s) {hdl->response->submit(hdl->respdata,t,s);}

static cle_syshandler _copy_handler = {0,{_cpy_start,_cpy_next,_cpy_end,_cpy_pop,_cpy_push,_cpy_data,_cpy_submit},0};

void cle_stream_leave(event_handler* hdl)
{
	hdl->thehandler = &_copy_handler;
}

static cle_syshandler _nil_handler = {0,{_nil1,_nil1,_nil2,_nil1x,_nil1,_nil2y,_nil3},0};

void cle_stream_fail(event_handler* hdl, cdat msg, uint msglen)
{
	hdl->thehandler = &_nil_handler;

	hdl->error = (msglen == 0)? "" : msg;
	hdl->errlength = msglen;

	hdl->response->end(hdl->respdata,msg,msglen);
}

void cle_stream_end(event_handler* hdl)
{
	cle_stream_fail(hdl,0,0);
}

/* system event-handler setup */
void cle_add_sys_handler(task* config_task, st_ptr config_root, cdat eventmask, uint mask_length, cle_syshandler* handler)
{
	cle_syshandler* exsisting;

	st_insert(config_task,&config_root,eventmask,mask_length);

	st_insert(config_task,&config_root,HEAD_HANDLER,HEAD_SIZE);

	if(st_get(config_task,&config_root,(char*)&exsisting,sizeof(cle_syshandler*)) == -1)
		// prepend to list
		handler->next_handler = exsisting;
	else
		handler->next_handler = 0;

	st_update(config_task,&config_root,(cdat)&handler,sizeof(cle_syshandler*));
}

cle_syshandler cle_create_simple_handler(void (*start)(void*),void (*next)(void*),void (*end)(void*,cdat,uint),enum handler_type type)
{
	cle_syshandler hdl;
	hdl.next_handler = 0;
	hdl.input.start = start == 0 ? _nil1 : start;
	hdl.input.next = next == 0 ? _nil1 : next;
	hdl.input.end = end == 0 ? _nil2 : end;
	hdl.input.pop = cle_standard_pop;
	hdl.input.push = cle_standard_push;
	hdl.input.data = cle_standard_data;
	hdl.input.submit = cle_standard_submit;
	hdl.systype = type;
	return hdl;
}

uint cle_obj_from_event(event_handler* hdl, uint sizeofname, st_ptr* obj)
{
	*obj = hdl->eventdata->eventid;
	st_offset(hdl->inst.t,obj,sizeofname);

	return cle_goto_object(hdl->inst,*obj,obj);
}

void cle_collect_params_next(event_handler* hdl)
{
	ptr_list* list = (ptr_list*)tk_alloc(hdl->inst.t,sizeof(ptr_list),0);

	list->pt = hdl->root;
	list->link = (ptr_list*)hdl->handler_data;
	hdl->handler_data = list;

	cle_standard_next_done(hdl);
}

/* control role-access */
void cle_allow_role(task* app_instance, st_ptr root, cdat eventmask, uint mask_length, cdat role, uint role_length)
{
	// max length!
	if(role_length > 255)
		return;

	st_insert(app_instance,&root,HEAD_EVENT,HEAD_SIZE);

	st_insert(app_instance,&root,eventmask,mask_length);

	st_insert(app_instance,&root,HEAD_ROLES,HEAD_SIZE);

	st_insert(app_instance,&root,role,role_length);
}

void cle_revoke_role(task* app_instance, st_ptr app_root, cdat eventmask, uint mask_length, cdat role, uint role_length)
{}

void cle_give_role(task* app_instance, st_ptr app_root, cdat eventmask, uint mask_length, cdat role, uint role_length)
{}

// input-functions
#define _error(txt) response->end(responsedata,txt,sizeof(txt))

struct _mark_chain
{
	struct _mark_chain* next;
	cle_syshandler* syshdl;
	st_ptr eventpt;
};

struct _scan_event
{
	task* t;
	struct _mark_chain* first, *last;
	st_ptr eventpt, syspt;
	uint allowed, state, oid_begin, trace_begin, index;
};

static uint _access_check(task* app_instance, st_ptr pt, char* user_roles[])
{
	if(st_move(app_instance,&pt,HEAD_ROLES,HEAD_SIZE) == 0)
	{
		// has allowed-roles
		it_ptr it;
		int r = 0;

		it_create(app_instance,&it,&pt);

		while(user_roles[r] != 0)
		{
			int cmp;
			it_load(app_instance,&it,user_roles[r] + 1,*user_roles[r]);

			cmp = it_next_eq(app_instance,&pt,&it,0);
			if(cmp == 0)
				break;
			else if(cmp == 2)
				return 1;

			for(r++;user_roles[r] != 0;r++)
			{
				cmp = memcmp(user_roles[r] + 1,it.kdata,it.kused < *user_roles[r]?it.kused:*user_roles[r]);
				if(cmp == 0)
					return 1;
				else if(cmp > 0)
					break;
			}
		}

		it_dispose(app_instance,&it);
	}
	return 0;
}

static uint _event_move(struct _scan_event* se, cdat cs, uint l)
{
	// lookup event-part (module-level)
	if(se->eventpt.pg != 0 && st_move(se->t,&se->eventpt,cs,l) != 0)
		se->eventpt.pg = 0;

	if(se->syspt.pg != 0 && st_move(0,&se->syspt,cs,l) != 0)
		se->syspt.pg = 0;

	// not found! scan end (or no possible grants)
	return (se->eventpt.pg == 0 && (se->syspt.pg == 0 || se->allowed == 0));
}

static uint _event_mark(struct _scan_event* se, cdat cs, uint len)
{
	cle_syshandler* syshdl = 0;
	st_ptr pt, eventpt;

	if(_event_move(se,cs,len) != 0 || _event_move(se,"",1))
		return 1;

	// lookup allowed roles (if no access yet)
	//if(allowed == 0)
	//	allowed = _access_check(app_instance,eventpt,user_roles);

	// get pipeline-handlers
	// module-level
	pt = se->eventpt;
	if(pt.pg != 0 && st_move(0,&pt,HEAD_HANDLER,HEAD_SIZE) == 0)
		eventpt = pt;
	else
		eventpt.pg = 0;

	// system-level
	pt = se->syspt;
	if(pt.pg != 0 && st_move(0,&pt,HEAD_HANDLER,HEAD_SIZE) == 0)
	{
		if(st_get(0,&pt,(char*)&syshdl,sizeof(cle_syshandler*)) != -1)
			unimplm();	// should never happen
	}

	// mark
	if(syshdl != 0 || eventpt.pg != 0)
	{
		struct _mark_chain* mc = (struct _mark_chain*)tk_alloc(se->t,sizeof(struct _mark_chain),0);
		mc->eventpt = eventpt;
		mc->syshdl  = syshdl;
		mc->next    = 0;

		if(se->last != 0)
		{
			se->last->next = mc;
			se->last = mc;
		}
		else
			se->first = se->last = mc;
	}
	return 0;
}

static uint _event_scan(void* ctx, cdat cs, uint l)
{
	struct _scan_event* se = (struct _scan_event*)ctx;
	uint i;
	for(i = 0; l > 0; l--, se->index++)
	{
		uint c = *cs;
		switch(c)
		{
		case '.':
		case '/':
		case '\\':
		case 0:
			if(se->state & 1 == 0)
				return -1;
			if(_event_mark(se,cs,i))
				return -1;
			se->state = 2;
			cs++;
			i = 0;
			break;
		case '@':	// OID
			if(se->state & 1 == 0)
				return -1;
			if(_event_mark(se,cs,i))
				return -1;
			se->state = 8;
			se->oid_begin = se->index;
			break;
		case '!':	// TRACER-ID
			if(se->state & 1 != 0)
			{
				if(_event_mark(se,cs,i))
					return -1;
				se->state = 0;
			}
			else if(se->state != 8)
				return -1;
			se->state |= 16;
			se->trace_begin = se->index;
			break;
		case ' ':
		case '\n':
		case '\t':
		case '\r':
			se->state |= 4;
			cs++;
			break;
		default:
			// illegal character?
			if(c < '0' || c > 'z' || (c > 'Z' && c < 'a') || (c > '9' && c < 'A'))
				return -1;
			if(se->state == 5)
				return -1;
			else if(se->state & 24 == 0)
			{
				se->state = 1;
				i++;
			}
		}
	}
	return (se->state & 1 != 0)? _event_move(se,cs,i) : 0;
}

static void _register_handler(task* t, event_handler** hdlists, cle_syshandler* syshandler, st_ptr* handler, st_ptr* object, enum handler_type type)
{
	event_handler* hdl;

	if(type == SYNC_REQUEST_HANDLER && hdlists[type] != 0)
		hdl = hdlists[type];
	else
	{
		hdl = (event_handler*)tk_alloc(t,sizeof(struct event_handler),0);
		hdl->next = hdlists[type];
		hdlists[type] = hdl;
	}

	hdl->thehandler = syshandler;
	hdl->handler_data = 0;
	hdl->object = *object;

	if(handler == 0)
		hdl->handler.pg = 0;
	else
		hdl->handler = *handler;
}

static void _setup_handlers(cle_instance inst, struct _mark_chain* mc, event_handler** hdlists, st_ptr* obj)
{
	for(; mc != 0; mc = mc->next)
	{
		if(mc->eventpt.pg != 0)
		{
			it_ptr it;
			it_create(inst.t,&it,&mc->eventpt);

			while(it_next(inst.t,0,&it,sizeof(cle_handler)))
			{
				cle_handler href = *((cle_handler*)it.kdata);
				st_ptr handler,lobj = *obj;
				if(cle_get_handler(inst,href,&lobj,&handler) == 0)
					_register_handler(inst.t,hdlists,&_runtime_handler,&handler,&lobj,href.type);
			}

			it_dispose(inst.t,&it);
		}

		while(mc->syshdl != 0);
		{
			_register_handler(inst.t,hdlists,mc->syshdl,0,obj,mc->syshdl->systype);

			// next in list...
			mc->syshdl = mc->syshdl->next_handler;
		}
	}
}

static void _ready_handler(event_handler* hdl, cle_instance inst, sys_handler_data* sysdata, cle_pipe* response, void* respdata, uint create_object)
{
	hdl->inst = inst;

	//TODO copy event and user data
	hdl->eventdata = sysdata;

	// set output-handler
	hdl->response = response;
	hdl->respdata = respdata;

	hdl->errlength = 0;
	hdl->error = 0;

	if(create_object)
	{
		st_ptr ext = hdl->object;
		cle_new_mem(inst.t,&ext,&hdl->object);
	}

	// prepare for input-stream
	hdl->top = 0;
	cle_standard_push(hdl);
}

static void _init_async_handlers(_ipt* ipt, event_handler* hdl, task* t, uint create_object)
{
	ipt->event_chain_begin = hdl;

	while(hdl != 0)
	{
		cle_instance clone;
		// put in separat tasks/transactions
		clone.t = tk_clone_task(t);
		tk_root_ptr(clone.t,&clone.root);

		// "no output"-handler on all async's
		_ready_handler(hdl,clone,&ipt->sys,&_async_out,hdl,create_object);

		hdl = hdl->next;
	}
}

static void _init_sync_handlers(_ipt* ipt, event_handler* sync_handler, event_handler* response_pipe, cle_instance inst, uint create_object, cle_pipe* response, void* responsedata)
{
	// there can be only one active sync-handler (dont mess-up output with concurrent event-handlers)
	// setup response-handler chain (only make sense with sync handlers)
	if(response_pipe != 0)
	{
		event_handler *last = sync_handler,*hdl = response_pipe;
		// in correct order (most specific handler comes first)
		do
		{
			_ready_handler(last,inst,&ipt->sys,&hdl->thehandler->input,hdl,1);

			last = hdl;
			hdl = hdl->next;
		}
		while(hdl != 0);

		// and finally the original output-target
		_ready_handler(last,inst,&ipt->sys,response,responsedata,create_object);
	}
	else
		_ready_handler(sync_handler,inst,&ipt->sys,response,responsedata,create_object);

	sync_handler->next = ipt->event_chain_begin;
	ipt->event_chain_begin = sync_handler;
}

static void _init_request_pipe(_ipt* ipt, event_handler* hdl, cle_instance inst)
{
	// reverse order (most general handlers comes first)
	event_handler* last;
	cle_pipe* resp;
	void* data = ipt->event_chain_begin;

	// just a single receiver? -> just (hard)chain together
	if(ipt->event_chain_begin->next == 0)
		resp = &ipt->event_chain_begin->thehandler->input;
	else
		resp = &_pipeline_all;

	do
	{
		last = hdl;
		_ready_handler(hdl,inst,&ipt->sys,resp,data,1);

		resp = &hdl->thehandler->input;
		data = hdl;

		hdl = hdl->next;
	}
	while(hdl != 0);

	last->next = 0;
	ipt->event_chain_begin = last;
}

static void _setup_object_and_trace(cle_instance inst, struct _scan_event* se, st_ptr eventid, st_ptr* obj)
{
	obj->pg = 0;

	if(se->state & 8)	// OID
	{
		st_ptr id = eventid;
		st_offset(inst.t,&id,se->oid_begin);

		cle_goto_object(inst,id,obj);
	}

	if(se->state & 16)	// TID
	{
		// TODO Trace
	}
}

_ipt* cle_start(task* app_instance, st_ptr config, st_ptr eventid, st_ptr userid, st_ptr user_roles,
				 cle_pipe* response, void* responsedata)
{
	event_handler* hdlists[4] = {0,0,0,0};
	struct _scan_event se;
	cle_instance inst;
	_ipt* ipt;
	st_ptr obj;

	/* get a root ptr to instance-db */
	inst.t = app_instance;
	tk_root_ptr(inst.t,&inst.root);

	if(response == 0)
		response = &_nil_out;

	memset(&se,0,sizeof(struct _scan_event));
	se.t = inst.t;
	// no username? -> root/sa
	se.allowed = st_is_empty(&userid);

	cle_eventroot(inst.t,&se.eventpt);
	se.syspt = config;

	if(st_map(inst.t,&eventid,_event_scan,&se) != 0 || se.allowed == 0)
	{
		_error(event_not_allowed);
		return 0;
	}

	_setup_object_and_trace(inst,&se,eventid,&obj);

	_setup_handlers(inst,se.first,hdlists,&obj);

	// is there anyone in the other end?
	if(hdlists[SYNC_REQUEST_HANDLER] == 0 && hdlists[ASYNC_REQUEST_HANDLER] == 0)
	{
		_error(event_not_allowed);
		return 0;
	}

	// ipt setup - internal task
	ipt = (_ipt*)tk_alloc(app_instance,sizeof(_ipt),0);
	// default null
	ipt->event_chain_begin = 0;

	// reuse markchain
	ipt->sys.free = (ptr_list*)se.first;
	ipt->sys.config = config;
//			ipt->sys.eventid = ievent;
//			ipt->sys.event_len = event_len;
//			ipt->sys.userid = userid;
//			ipt->sys.userid_len = userid_len;

	_init_async_handlers(ipt,hdlists[ASYNC_REQUEST_HANDLER],inst.t,se.state & 8);

	if(hdlists[SYNC_REQUEST_HANDLER] != 0)
		_init_sync_handlers(ipt,hdlists[SYNC_REQUEST_HANDLER],hdlists[PIPELINE_RESPONSE],inst,se.state & 8,response,responsedata);

	if(hdlists[SYNC_REQUEST_HANDLER] != 0)
		_init_request_pipe(ipt,hdlists[PIPELINE_REQUEST],inst);

	// do start
	cle_notify_start(ipt->event_chain_begin);
	return ipt;
}

void cle_next(_ipt* ipt)
{
	if(ipt == 0)
		return;

	cle_notify_next(ipt->event_chain_begin);
}

void cle_end(_ipt* ipt, cdat code, uint length)
{
	if(ipt == 0)
		return;

	cle_notify_end(ipt->event_chain_begin,code,length);
}

void cle_pop(_ipt* ipt)
{
	if(ipt == 0)
		return;

	if(_pa_pop(ipt->event_chain_begin))
		cle_end(ipt,input_underflow,sizeof(input_underflow));
}

void cle_push(_ipt* ipt)
{
	if(ipt == 0)
		return;

	_pa_push(ipt->event_chain_begin);
}

void cle_data(_ipt* ipt, cdat data, uint length)
{
	if(ipt == 0)
		return;

	_pa_data(ipt->event_chain_begin,data,length);
}
