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

// error-messages
static char input_underflow[] = "stream:input underflow";
static char input_incomplete[] = "stream:input incomplete";
static char event_not_allowed[] = "stream:event not allowed";

#ifdef CLERK_SINGLE_THREAD

void cle_notify_start(event_handler* handler)
{
	while(handler != 0 && handler->eventdata->error == 0)
	{
		if(handler->thehandler->input.start != 0)
			handler->thehandler->input.start(handler);

		handler = handler->next;
	}
}

void cle_notify_next(event_handler* handler)
{
	while(handler != 0 && handler->eventdata->error == 0)
	{
		if(handler->top->link != 0)
			cle_stream_fail(handler,input_underflow,sizeof(input_underflow));
		else
		{
			if(handler->thehandler->input.next != 0)
				handler->thehandler->input.next(handler);

			// done processing .. clear and ready for next input-stream
			st_empty(handler->instance_tk,&handler->top->pt);
		}

		handler = handler->next;
	}
}

// TODO: make sure end is called on all handlers - and the message gets to the caller
void cle_notify_end(event_handler* handler, cdat msg, uint msglength)
{
	while(handler != 0)
	{
		if(handler->thehandler->input.end != 0)
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
static void _nil3(void* v,task* t,st_ptr* st){}
static cle_pipe _nil_out = {_nil1,_nil1,_nil2,_nil1,_nil1,_nil2,_nil3};

// async output-handler
static int _async_end(event_handler* hdl, cdat c, uint clen)
{
	// end async-task
	if(clen == 0)
		return tk_commit_task(hdl->instance_tk);

	tk_drop_task(hdl->instance_tk);
	return 0;
}

static cle_pipe _async_out = {_nil1,_nil1,_async_end,_nil1,_nil1,_nil2,_nil3};

// convenience functions for implementing the cle_pipe-interface
void cle_standard_pop(event_handler* hdl)
{
	ptr_list* elm = hdl->top;
	hdl->top = hdl->top->link;

	elm->link = hdl->free;
	hdl->free = elm;
}

void cle_standard_push(event_handler* hdl)
{
	ptr_list* elm;
	if(hdl->free)
	{
		elm = hdl->free;
		hdl->free = elm->link;
	}
	else
		elm = (ptr_list*)tk_alloc(hdl->instance_tk,sizeof(ptr_list));

	if(hdl->top != 0)
		elm->pt = hdl->top->pt;
	else
		st_empty(hdl->instance_tk,&elm->pt);

	elm->link = hdl->top;
	hdl->top = elm;
}

void cle_standard_data(event_handler* hdl, cdat data, uint length)
{
	st_append(hdl->instance_tk,&hdl->top->pt,data,length);
}

void cle_standard_submit(event_handler* hdl, task* t_from, st_ptr* from)
{
	// toplevel?
	if(hdl->top->link == 0 && hdl->instance_tk == t_from)
		hdl->top->pt = *from;	// subst
	else
		st_link(hdl->instance_tk,&hdl->top->pt,t_from,from);
}

// TODO: could use up alot of RAM for no reason - write better streamer
// FAIL: data-leaf can not have null-char !
void cle_stream_submit_beta(task* t, cle_pipe* recv, void* data, task* t_pt, st_ptr* pt)
{
	if(st_is_empty(pt) == 0)
	{
		it_ptr it;
		st_ptr tpt;

		it_create(t_pt,&it,pt);

		recv->push(data);
		while(it_next(t_pt,&tpt,&it))
		{
			recv->data(data,it.kdata,it.kused);

			cle_stream_submit_beta(t,recv,data,t_pt,&tpt);
		}
		recv->pop(data);

		it_dispose(t_pt,&it);
	}
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

static void _pa_data(event_handler* hdl, cdat data, uint length)
{
	do
	{
		cle_standard_data(hdl,data,length);
		hdl = hdl->next;
	}
	while(hdl != 0);
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
static void _cpy_pop(event_handler* hdl) {hdl->response->pop(hdl->respdata);}
static void _cpy_push(event_handler* hdl) {hdl->response->push(hdl->respdata);}
static void _cpy_data(event_handler* hdl,cdat c,uint u) {hdl->response->data(hdl->respdata,c,u);}
static void _cpy_submit(event_handler* hdl,task* t,st_ptr* s) {hdl->response->submit(hdl->respdata,t,s);}

static cle_syshandler _copy_handler = {0,{_cpy_start,_cpy_next,_cpy_end,_cpy_pop,_cpy_push,_cpy_data,_cpy_submit},0};

void cle_stream_leave(event_handler* hdl)
{
	hdl->thehandler = &_copy_handler;
}

void cle_stream_fail(event_handler* hdl, cdat msg, uint msglen)
{
	if(hdl->eventdata->error == 0)
	{
		hdl->eventdata->error = msg;
		hdl->eventdata->errlength = msglen;
	}
	cle_stream_leave(hdl);
}

void cle_stream_end(event_handler* hdl)
{
	cle_stream_fail(hdl,"",0);
	cle_stream_leave(hdl);
}

cle_syshandler cle_create_simple_handler(void (*start)(void*),void (*next)(void*),void (*end)(void*,cdat,uint),enum handler_type type)
{
	cle_syshandler hdl;
	hdl.next_handler = 0;
	hdl.input.start = start;
	hdl.input.next = next;
	hdl.input.end = end;
	hdl.input.pop = cle_standard_pop;
	hdl.input.push = cle_standard_push;
	hdl.input.data = cle_standard_data;
	hdl.input.submit = cle_standard_submit;
	hdl.systype = type;
	return hdl;
}

// input-functions
#define _error(txt) response->end(responsedata,txt,sizeof(txt))

static int _validate_eventid(cdat eventid, uint event_len)
{
	uint i,state = 0,from = 0;
	for(i = 0; i < event_len; i++)
	{
		switch(eventid[i])
		{
		case 0:
			if(state != 0 || i == 0)
				return -1;
			state = 1;
			break;
		case '#':
			if(state != 1)
				return -1;
			from = i + 1;
			state = 2;
			break;
		default:
			// illegal character?
			if(state & 2)
			{
				if(eventid[i] > 'a' && eventid[i] < 'q')
					return -1;
			}
			else if(eventid[i] < '0' || eventid[i] > 'z' || (eventid[i] > 'Z' && eventid[i] < 'a') || (eventid[i] > '9' && eventid[i] < 'A'))
				return -1;
			else
				state = 0;
		}
	}

	return from;
}

_ipt* cle_start(st_ptr config, cdat eventid, uint event_len,
				cdat userid, uint userid_len, char* user_roles[],
					cle_pipe* response, void* responsedata, task* app_instance)
{
	_ipt* ipt;
	event_handler* hdlists[4];
	event_handler* hdl;
	st_ptr pt,eventpt,syspt,instance,object;
	int from,i,allowed;

	// ipt setup - internal task
	ipt = (_ipt*)tk_alloc(app_instance,sizeof(_ipt));
	// default null
	memset(ipt,0,sizeof(_ipt));

	memset(hdlists,0,sizeof(hdlists));

	if(response == 0)
		response = &_nil_out;

	ipt->sys.config = config;
	ipt->sys.eventid = eventid;
	ipt->sys.event_len = event_len;
	ipt->sys.userid = userid;
	ipt->sys.userid_len = userid_len;

	/* get a root ptr to instance-db */
	tk_root_ptr(app_instance,&instance);

	// validate event-id and get target-oid (if any)
	i = _validate_eventid(eventid,event_len);
	if(i != 0)
	{
		if(i < 0 || cle_get_target(app_instance,instance,&object,eventid + i,event_len - i))
		{
			// target not found
			_error(event_not_allowed);
			return 0;
		}
	}
	else
		object.pg = 0;

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

	syspt = config;
	from = 0;

	for(i = 0; i < event_len; i++)
	{
		// event-part-boundary
		if(eventid[i] != 0)
			continue;

		// lookup event-part (module-level)
		if(eventpt.pg != 0 && st_move(app_instance,&eventpt,eventid + from,i + 1 - from) != 0)
		{
			eventpt.pg = 0;
			// not found! scan end (or no possible grants)
			if(syspt.pg == 0 || allowed == 0)
				break;
		}

		// lookup system-level handlers
		if(syspt.pg != 0 && st_move(0,&syspt,eventid + from,i + 1 - from) != 0)
		{
			syspt.pg = 0;
			// not found! scan end
			if(eventpt.pg == 0)
				break;
		}

		// lookup allowed roles (if no access yet)
		if(allowed == 0)
		{
			pt = eventpt;
			if(st_move(app_instance,&pt,HEAD_ROLES,HEAD_SIZE) == 0)
			{
				// has allowed-roles
				it_ptr it;
				int r = 0;

				it_create(app_instance,&it,&pt);

				while(allowed == 0 && user_roles[r] != 0)
				{
					int cmp;
					it_load(app_instance,&it,user_roles[r] + 1,*user_roles[r]);

					cmp = it_next_eq(app_instance,&pt,&it);
					if(cmp == 0)
						break;
					else if(cmp == 2)
					{
						allowed = 1;
						break;
					}

					for(r++;user_roles[r] != 0;r++)
					{
						cmp = memcmp(user_roles[r] + 1,it.kdata,it.kused < *user_roles[r]?it.kused:*user_roles[r]);
						if(cmp == 0)
						{
							allowed = 1;
							break;
						}
						else if(cmp > 0)
							break;
					}
				}

				it_dispose(app_instance,&it);
			}
		}

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
				char handlertype;

				if(st_get(app_instance,&pt,(char*)&handlertype,1) != -2)
				{
					st_ptr handler,obj;

					if(handlertype < PIPELINE_REQUEST)
						obj = object;
					else
						obj.pg = 0;

					if(cle_get_handler(app_instance,instance,pt,&handler,&obj,eventid,i,handlertype) == 0)
					{
						hdl = (event_handler*)tk_alloc(app_instance,sizeof(struct event_handler));

						hdl->next = hdlists[handlertype];
						hdlists[handlertype] = hdl;

						hdl->thehandler = &_runtime_handler;
						hdl->eventdata = &ipt->sys;
						hdl->handler_data = 0;

						hdl->handler = handler;
						hdl->object = obj;
					}
				}
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
					hdl = (event_handler*)tk_alloc(app_instance,sizeof(struct event_handler));

					hdl->next = hdlists[syshdl->systype];
					hdlists[syshdl->systype] = hdl;
					hdl->thehandler = syshdl;
					hdl->eventdata = &ipt->sys;
					hdl->handler_data = 0;

					hdl->object = object;
					hdl->handler.pg = 0;

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
			hdl->instance_tk = tk_clone_task(app_instance);
			tk_root_ptr(hdl->instance_tk,&hdl->instance);

			// "no output"-handler on all async's
			hdl->response = &_async_out;
			hdl->respdata = hdl;

			// prepare for input-stream
			hdl->top = hdl->free = 0;
			cle_standard_push(hdl);

			hdl = hdl->next;
		}
		while(hdl != 0);
	}

	// setup sync-handler-chain
	if(hdlists[SYNC_REQUEST_HANDLER] != 0)
	{
		event_handler* sync_handler = hdlists[SYNC_REQUEST_HANDLER];

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
				last->response = &hdl->thehandler->input;
				last->respdata = hdl;

				// same task as request-handler
				last->instance_tk = app_instance;
				last->instance = instance;

				// prepare for input-stream
				last->top = last->free = 0;
				cle_standard_push(last);

				last = hdl;
				hdl = hdl->next;
			}
			while(hdl != 0);

			// and finally the original output-target
			last->response = response;
			last->respdata = responsedata;

			last->instance_tk = app_instance;
			last->instance = instance;

			// prepare for input-stream
			last->top = last->free = 0;
			cle_standard_push(last);
		}
		else
		{
			sync_handler->response = response;
			sync_handler->respdata = responsedata;

			sync_handler->instance_tk = app_instance;
			sync_handler->instance = instance;

			// prepare for input-stream
			sync_handler->top = sync_handler->free = 0;
			cle_standard_push(sync_handler);
		}

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

		// just a single sync-receiver -> just (hard)chain together
		if(ipt->event_chain_begin == hdlists[SYNC_REQUEST_HANDLER] && ipt->event_chain_begin->next == 0)
			resp = &ipt->event_chain_begin->thehandler->input;
		else
			resp = &_pipeline_all;

		hdl = hdlists[PIPELINE_REQUEST];

		do
		{
			last = hdl;
			hdl->response = resp;
			hdl->respdata = data;

			hdl->instance_tk = app_instance;
			hdl->instance = instance;

			// prepare for input-stream
			hdl->top = hdl->free = 0;
			cle_standard_push(hdl);

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
	if(ipt == 0 || ipt->sys.error != 0)
		return;

	if(ipt->depth != 0)
		cle_end(ipt,input_incomplete,sizeof(input_incomplete));
	else
		// do next
		cle_notify_next(ipt->event_chain_begin);
}

void cle_end(_ipt* ipt, cdat code, uint length)
{
	// only handle one end-event
	if(ipt == 0 || ipt->sys.error != 0)
		return;

	// reporting an error
	if(code != 0 && length != 0)
	{
		ipt->sys.error = code;
		ipt->sys.errlength = length;
	}
	else
		// signal end of input
		ipt->sys.error = "";

	cle_notify_end(ipt->event_chain_begin,code,length);
}

void cle_pop(_ipt* ipt)
{
	if(ipt == 0 || ipt->sys.error != 0)
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
	if(ipt == 0 || ipt->sys.error != 0)
		return;

	_pa_push(ipt->event_chain_begin);

	ipt->depth++;
}

void cle_data(_ipt* ipt, cdat data, uint length)
{
	if(ipt == 0 || ipt->sys.error != 0)
		return;

	_pa_data(ipt->event_chain_begin,data,length);
}

void cle_submit(_ipt* ipt, task* t, st_ptr* root)
{
	if(ipt == 0 || ipt->sys.error != 0)
		return;

	_pa_submit(ipt->event_chain_begin,t,root);
}
