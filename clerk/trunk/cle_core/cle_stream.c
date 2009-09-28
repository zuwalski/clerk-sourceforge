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
#include "cle_instance.h"

/*
*	The main input-interface to the running system
*	Events/messages are "pumped" in through the exported set of functions
*/

/* TODO: 
		handle end: make sure all receive the event
		debugger id in event-string e.g. path.path[#objid][@debuggerId]
		kill submit or rework st_link etc.
		share ptr_list free-list across task-handlers
*/

// error-messages
static char input_underflow[] = "stream:input underflow";
static char input_incomplete[] = "stream:input incomplete";
static char event_not_allowed[] = "stream:event not allowed";

#ifdef CLERK_SINGLE_THREAD

void cle_notify_start(event_handler* handler)
{
	while(handler != 0)
	{
		handler->thehandler->input.start(handler);
		handler = handler->next;
	}
}

void cle_notify_next(event_handler* handler)
{
	while(handler != 0)
	{
		// TODO: implement these checks elsewhere...
		if(handler->top->link != 0)
			cle_stream_fail(handler,input_underflow,sizeof(input_underflow));
		else
		{
			handler->thehandler->input.next(handler);

			if(handler->error == 0)
			{
				// done processing .. clear and ready for next input-stream
				st_empty(handler->instance_tk,&handler->top->pt);
				handler->root = handler->top->pt;
			}
		}

		handler = handler->next;
	}
}

// TODO: make sure end is called on all handlers - and the message gets to the caller
void cle_notify_end(event_handler* handler, cdat msg, uint msglength)
{
	while(handler != 0)
	{
		handler->thehandler->input.end(handler,msg,msglength);
		handler = handler->next;
	}
}

#endif

// structs
struct _ipt_internal
{
	event_handler* event_chain_begin;

	sys_handler_data sys;

	uint depth;
};

// nil output-handler
static void _nil1(void* v){}
static void _nil2(void* v,cdat c,uint u){}
static uint _nil2x(void* v,cdat c,uint u){return 0;}
static void _nil3(void* v,st_ptr* st){}
static cle_pipe _nil_out = {_nil1,_nil1,_nil2,_nil1,_nil1,_nil2x,_nil3};

// async output-handler
static int _async_end(event_handler* hdl, cdat c, uint clen)
{
	// end async-task
	if(clen == 0)
		return tk_commit_task(hdl->instance_tk);

	tk_drop_task(hdl->instance_tk);
	return 0;
}

static cle_pipe _async_out = {_nil1,_nil1,_async_end,_nil1,_nil1,_nil2x,_nil3};

// convenience functions for implementing the cle_pipe-interface
void cle_standard_pop(event_handler* hdl)
{
	ptr_list* elm = hdl->top;
	hdl->top = hdl->top->link;

	elm->link = hdl->free;
	hdl->free = elm;
}

static ptr_list* _alloc_elem(event_handler* hdl)
{
	if(hdl->free)
	{
		ptr_list* elm = hdl->free;
		hdl->free = elm->link;
		return elm;
	}

	return (ptr_list*)tk_alloc(hdl->instance_tk,sizeof(ptr_list),0);
}

void cle_standard_push(event_handler* hdl)
{
	ptr_list* elm = _alloc_elem(hdl);

	if(hdl->top != 0)
		elm->pt = hdl->top->pt;
	else
	{
		st_empty(hdl->instance_tk,&elm->pt);
		hdl->root = elm->pt;
	}

	elm->link = hdl->top;
	hdl->top = elm;
}

uint cle_standard_data(event_handler* hdl, cdat data, uint length)
{
	return st_append(hdl->instance_tk,&hdl->top->pt,data,length);
}

void cle_standard_submit(event_handler* hdl, st_ptr* from)
{
	// toplevel?
	if(hdl->top->link == 0)
		hdl->root = hdl->top->pt = *from;	// subst
	else
		st_link(hdl->instance_tk,&hdl->top->pt,from);
}

// pipeline activate All handlers
static void _pa_pop(event_handler* hdl)
{
	do
	{
		cle_standard_pop(hdl);
		hdl = hdl->next;
	}
	while(hdl != 0);
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
		cle_standard_data(hdl,data,length);
		hdl = hdl->next;
	}
	while(hdl != 0);
	return 0;
}

static void _pa_submit(event_handler* hdl, st_ptr* st)
{
	do
	{
		cle_standard_submit(hdl,st);
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
static void _cpy_pop(event_handler* hdl) {hdl->response->pop(hdl->respdata);}
static void _cpy_push(event_handler* hdl) {hdl->response->push(hdl->respdata);}
static uint _cpy_data(event_handler* hdl,cdat c,uint u) {return hdl->response->data(hdl->respdata,c,u);}
static void _cpy_submit(event_handler* hdl,st_ptr* s) {hdl->response->submit(hdl->respdata,s);}

static cle_syshandler _copy_handler = {0,{_cpy_start,_cpy_next,_cpy_end,_cpy_pop,_cpy_push,_cpy_data,_cpy_submit},0};

void cle_stream_leave(event_handler* hdl)
{
	hdl->thehandler = &_copy_handler;
}

static cle_syshandler _nil_handler = {0,{_nil1,_nil1,_nil2,_nil1,_nil1,_nil2x,_nil3},0};

void cle_stream_fail(event_handler* hdl, cdat msg, uint msglen)
{
	hdl->thehandler = &_nil_handler;

	hdl->error = msg;
	hdl->errlength = msglen;

	hdl->response->end(hdl->respdata,msg,msglen);
}

void cle_stream_end(event_handler* hdl)
{
	cle_stream_fail(hdl,"",0);
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

static void _register_handler(task* app_instance, event_handler** hdlists, cle_syshandler* syshandler, st_ptr* handler, st_ptr* object, enum handler_type type)
{
	event_handler* hdl = (event_handler*)tk_alloc(app_instance,sizeof(struct event_handler),0);

	hdl->next = hdlists[type];
	hdlists[type] = hdl;
	hdl->thehandler = syshandler;
	hdl->object = *object;
	hdl->handler_data = 0;

	if(handler == 0)
		hdl->handler.pg = 0;
	else
		hdl->handler = *handler;
}

static void _ready_handler(event_handler* hdl, task* inst_tk, st_ptr* instance, sys_handler_data* sysdata, cle_pipe* response, void* respdata, uint targetset)
{
	hdl->instance_tk = inst_tk;
	hdl->instance = *instance;

	//TODO copy event and user data
	hdl->eventdata = sysdata;

	// set output-handler
	hdl->response = response;
	hdl->respdata = respdata;

	hdl->errlength = 0;
	hdl->error = 0;

	if(targetset == 0 && hdl->handler.pg != 0)
		cle_new_mem(inst_tk,&hdl->object,hdl->object);

	// prepare for input-stream
	hdl->top = hdl->free = 0;
	cle_standard_push(hdl);
}

// sync-handler-chain
struct _sync_chain
{
	event_handler* synch;
	ptr_list*      input;
	char           create_object;
	char           started;
	char           failed;
};

static void _sync_start_rs(struct _sync_chain* hdl)
{
	if(hdl->started == 0)
	{
		hdl->started = 1;
		hdl->synch->response->start(hdl->synch->respdata);
	}
}

static void _sync_end_rs(struct _sync_chain* hdl,cdat c,uint u)
{
	if(u > 0)
	{
		hdl->failed = 1;
		hdl->synch->response->end(hdl->synch->respdata,c,u);
	}
}

static void _sync_next_rs(struct _sync_chain* hdl) {hdl->synch->response->next(hdl->synch->respdata);}
static void _sync_pop_rs(struct _sync_chain* hdl) {hdl->synch->response->pop(hdl->synch->respdata);}
static void _sync_push_rs(struct _sync_chain* hdl) {hdl->synch->response->push(hdl->synch->respdata);}
static uint _sync_data_rs(struct _sync_chain* hdl,cdat c,uint u) {return hdl->synch->response->data(hdl->synch->respdata,c,u);}
static void _sync_submit_rs(struct _sync_chain* hdl,st_ptr* s) {hdl->synch->response->submit(hdl->synch->respdata,s);}

static cle_pipe _sync_response = {_sync_start_rs,_sync_next_rs,_sync_end_rs,_sync_pop_rs,_sync_push_rs,_sync_data_rs,_sync_submit_rs};

// startup chain of sync-handlers
static void _sync_start(event_handler* hdl)
{
	struct _sync_chain* sc = (struct _sync_chain*)hdl->handler_data;

	_ready_handler(sc->synch,hdl->instance_tk,&hdl->instance,hdl->eventdata,&_sync_response,sc,sc->create_object);
	// start first handler
	sc->synch->thehandler->input.start(sc->synch);
}

static void _sync_next(event_handler* hdl)
{
	struct _sync_chain* sc = (struct _sync_chain*)hdl->handler_data;
	if(sc->failed == 0)
	{
		ptr_list* elm = _alloc_elem(hdl);
		// save input
		elm->pt = hdl->root;
		// push
		elm->link = sc->input;
		sc->input = elm;
		// next first handler
		sc->synch->thehandler->input.next(sc->synch);
	}
}

static void _sync_end(event_handler* hdl, cdat c, uint u)
{
	struct _sync_chain* sc = (struct _sync_chain*)hdl->handler_data;
	event_handler* chn = sc->synch->next;

	// end first
	sc->synch->thehandler->input.end(sc->synch,c,u);

	// replay data on sync-chain
	sc->input = ptr_list_reverse(sc->input);
	do
	{
		ptr_list* now = sc->input;

		_ready_handler(sc->synch,hdl->instance_tk,&hdl->instance,hdl->eventdata,&_sync_response,sc,sc->create_object);

		chn->thehandler->input.start(chn);
		while(now != 0 && sc->failed == 0)
		{
			chn->thehandler->input.submit(chn,&now->pt);
			chn->thehandler->input.next(chn);
			now = now->link;
		}

		if(sc->failed != 0)
			return;

		chn->thehandler->input.end(chn,c,u);

		chn = chn->next;
	}
	while(chn != 0);

	// call real response-end
	hdl->response->end(hdl->respdata,c,u);
}

static cle_syshandler _sync_chain_handler = {0,{_sync_start,_sync_next,_sync_end,cle_standard_pop,cle_standard_push,cle_standard_data,cle_standard_submit},0};

// input-functions
#define _error(txt) response->end(responsedata,txt,sizeof(txt))

static int _validate_eventid(cdat eventid, uint event_len, char* ievent)
{
	uint i,state = 0,to = 0;
	for(i = 0; i < event_len; i++)
	{
		switch(eventid[i])
		{
		case '.':
		case 0:
			if(state != 1)
				return -1;
			ievent[to++] = 0;
			state = 2;
			break;
		case '#':
		case '@':
			if(state != 1)
				return -1;
			ievent[to++] = 0;
			return to;
		case ' ':
		case '\n':
		case '\t':
		case '\r':
			break;
		default:
			// illegal character?
			if(eventid[i] < '0' || eventid[i] > 'z' || (eventid[i] > 'Z' && eventid[i] < 'a') || (eventid[i] > '9' && eventid[i] < 'A'))
				return -1;

			ievent[to++] = eventid[i];
			state = 1;
		}
	}
	ievent[to++] = 0;
	return (state == 1)? to : -1;
}

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

			cmp = it_next_eq(app_instance,&pt,&it);
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

_ipt* cle_start(st_ptr config, cdat eventid, uint event_len,
				cdat userid, uint userid_len, char* user_roles[],
					cle_pipe* response, void* responsedata, task* app_instance)
{
	_ipt* ipt;
	event_handler* hdlists[4] = {0,0,0,0};
	event_handler* hdl;
	st_ptr pt,eventpt,syspt,instance,object;
	int from,i,allowed;
	char* ievent;

	if(event_len >= EVENT_MAX_LENGTH)
	{
		_error(event_not_allowed);
		return 0;
	}

	ievent = (char*)tk_alloc(app_instance,event_len + 1,0);

	if(response == 0)
		response = &_nil_out;

	/* get a root ptr to instance-db */
	tk_root_ptr(app_instance,&instance);

	// no username? -> root/sa
	allowed = (userid_len == 0);

	eventpt = instance;
	if(st_move(app_instance,&eventpt,HEAD_EVENT,HEAD_SIZE) != 0)
	{
		eventpt.pg = 0;
		if(allowed == 0)
		{
			// no event-structure in this instance -> exit
			_error(event_not_allowed);
			return 0;
		}
	}

	// validate event-id and get target-oid (if any)
	i = _validate_eventid(eventid,event_len,ievent);
	if(i < 0)
	{
		// illegal event-name
		_error(event_not_allowed);
		return 0;
	}
	event_len = i;

	object.pg = 0;
	if(eventid[i] == '#' && cle_get_target(app_instance,instance,&object,ievent + i + 1,event_len - i))
	{
		// target not found
		_error(event_not_allowed);
		return 0;
	}

	// ipt setup - internal task
	ipt = (_ipt*)tk_alloc(app_instance,sizeof(_ipt),0);
	// default null
	memset(ipt,0,sizeof(_ipt));

	ipt->sys.eventid = ievent;
	ipt->sys.event_len = event_len;
	ipt->sys.userid = userid;
	ipt->sys.userid_len = userid_len;

	syspt = config;
	from = 0;

	for(i = 0; i < event_len; i++)
	{
		// event-part-boundary
		if(ievent[i] != 0)
			continue;

		// lookup event-part (module-level)
		if(eventpt.pg != 0 && st_move(app_instance,&eventpt,ievent + from,i + 1 - from) != 0)
		{
			eventpt.pg = 0;
			// not found! scan end (or no possible grants)
			if(syspt.pg == 0 || allowed == 0)
				break;
		}

		// lookup system-level handlers
		if(syspt.pg != 0 && st_move(0,&syspt,ievent + from,i + 1 - from) != 0)
		{
			syspt.pg = 0;
			// not found! scan end
			if(eventpt.pg == 0)
				break;
		}

		// lookup allowed roles (if no access yet)
		if(allowed == 0)
			allowed = _access_check(app_instance,eventpt,user_roles);

		// get pipeline-handlers
		// module-level
		pt = eventpt;
		if(pt.pg != 0 && st_move(app_instance,&pt,HEAD_HANDLER,HEAD_SIZE) == 0)
		{
			it_ptr it;
			it_create(app_instance,&it,&pt);

			// iterate instance-refs / event-handler-id
			while(it_next(app_instance,&pt,&it))
			{
				st_ptr handler, obj = object;
				int handlertype = st_scan(app_instance,&pt);

				if(cle_get_handler(app_instance,instance,pt,&handler,&obj,ievent,i + 1,handlertype) == 0)
					_register_handler(app_instance,hdlists,&_runtime_handler,&handler,&obj,handlertype);
			}

			it_dispose(app_instance,&it);
		}

		// system-level
		pt = syspt;
		if(pt.pg != 0 && st_move(0,&pt,HEAD_HANDLER,HEAD_SIZE) == 0)
		{
			cle_syshandler* syshdl;
			if(st_get(0,&pt,(char*)&syshdl,sizeof(cle_syshandler*)) == -1)
			{
				do
				{
					_register_handler(app_instance,hdlists,syshdl,0,&object,syshdl->systype);

					// next in list...
					syshdl = syshdl->next_handler;
				}
				while(syshdl != 0);
			}
		}

		from = i + 1;
	}

	// access allowed? and is there anyone in the other end?
	if(allowed == 0 || (hdlists[SYNC_REQUEST_HANDLER] == 0 && hdlists[ASYNC_REQUEST_HANDLER] == 0))
	{
		_error(event_not_allowed);
		return 0;
	}

	// init async-handlers
	if(hdlists[ASYNC_REQUEST_HANDLER] != 0)
	{
		hdl = hdlists[ASYNC_REQUEST_HANDLER];
		ipt->event_chain_begin = hdl;

		do
		{
			// put in separat tasks/transactions
			st_ptr clone_instance;
			task* clone = tk_clone_task(app_instance);
			tk_root_ptr(clone,&clone_instance);

			// "no output"-handler on all async's
			_ready_handler(hdl,clone,&clone_instance,&ipt->sys,&_async_out,hdl,object.pg != 0);

			hdl = hdl->next;
		}
		while(hdl != 0);
	}

	// setup sync-handler-chain
	if(hdlists[SYNC_REQUEST_HANDLER] != 0)
	{
		event_handler* sync_handler = hdlists[SYNC_REQUEST_HANDLER];

		// more than one sync-handler?
		if(sync_handler->next != 0)
		{
			struct _sync_chain* sc = (struct _sync_chain*)tk_alloc(app_instance,sizeof(struct _sync_chain),0);

			_register_handler(app_instance,hdlists,&_sync_chain_handler,0,&object,SYNC_REQUEST_HANDLER);

			sc->synch = sync_handler;
			sc->create_object = (object.pg != 0);
			sc->started = 0;
			sc->failed = 0;
			sc->input = 0;

			sync_handler = hdlists[SYNC_REQUEST_HANDLER];
			sync_handler->handler_data = sc;
		}

		// there can be only one active sync-handler (dont mess-up output with concurrent event-handlers)
		// setup response-handler chain (only make sense with sync handlers)
		if(hdlists[PIPELINE_RESPONSE] != 0)
		{
			event_handler* last;
			// in correct order (most specific handler comes first)
			hdl = hdlists[PIPELINE_RESPONSE];

			last = sync_handler;
			do
			{
				_ready_handler(last,app_instance,&instance,&ipt->sys,&hdl->thehandler->input,hdl,object.pg != 0);

				last = hdl;
				hdl = hdl->next;
			}
			while(hdl != 0);

			// and finally the original output-target
			_ready_handler(last,app_instance,&instance,&ipt->sys,response,responsedata,object.pg != 0);
		}
		else
			_ready_handler(sync_handler,app_instance,&instance,&ipt->sys,response,responsedata,object.pg != 0);

		sync_handler->next = ipt->event_chain_begin;
		ipt->event_chain_begin = sync_handler;
	}

	// setup request-handler chain
	if(hdlists[PIPELINE_REQUEST] != 0)
	{
		// reverse order (most general handlers comes first)
		event_handler* last;
		cle_pipe* resp;
		void* data = ipt->event_chain_begin;

		// just a single (sync-receiver?) -> just (hard)chain together
		// ipt->event_chain_begin == hdlists[SYNC_REQUEST_HANDLER] && 
		if(ipt->event_chain_begin->next == 0)
			resp = &ipt->event_chain_begin->thehandler->input;
		else
			resp = &_pipeline_all;

		hdl = hdlists[PIPELINE_REQUEST];

		do
		{
			last = hdl;
			_ready_handler(hdl,app_instance,&instance,&ipt->sys,resp,data,object.pg != 0);

			resp = &hdl->thehandler->input;
			data = hdl;

			hdl = hdl->next;
		}
		while(hdl != 0);

		last->next = 0;
		ipt->event_chain_begin = last;
	}

	// do start
	cle_notify_start(ipt->event_chain_begin);

	return ipt;
}

void cle_next(_ipt* ipt)
{
	if(ipt == 0)
		return;

	if(ipt->depth != 0)
		cle_end(ipt,input_incomplete,sizeof(input_incomplete));
	else
		// do next
		cle_notify_next(ipt->event_chain_begin);
}

void cle_end(_ipt* ipt, cdat code, uint length)
{
	if(ipt == 0)
		return;

	if(length == 0 && ipt->depth != 0)
	{
		code = input_incomplete;
		length = sizeof(input_incomplete);
	}

	cle_notify_end(ipt->event_chain_begin,code,length);
}

void cle_pop(_ipt* ipt)
{
	if(ipt == 0)
		return;

	if(ipt->depth == 0)
		cle_end(ipt,input_underflow,sizeof(input_underflow));
	else
	{
		_pa_pop(ipt->event_chain_begin);

		ipt->depth--;
	}
}

void cle_push(_ipt* ipt)
{
	if(ipt == 0)
		return;

	_pa_push(ipt->event_chain_begin);

	ipt->depth++;
}

void cle_data(_ipt* ipt, cdat data, uint length)
{
	if(ipt == 0)
		return;

	_pa_data(ipt->event_chain_begin,data,length);
}
