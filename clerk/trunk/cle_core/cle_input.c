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
#include "cle_input.h"
#include "cle_runtime.h"

/*
*	The main input-interface to the running system
*	Events/messages are "pumped" in through the exported set of functions
*/

#define HEAD_TYPE "\0T"
#define HEAD_APPS  "\0A"
#define HEAD_USERS "\0u"
#define HEAD_EVENT "\0e"
#define HEAD_HANDLER "\0h"
#define HEAD_ROLES "\0r"

// error-messages
static char input_underflow[] = "input underflow";
static char input_incomplete[] = "input incomplete";
static char event_not_allowed[] = "event not allowed";

#define _error(txt) ipt->response->end(ipt->respdata,txt,sizeof(txt))

/* GLOBALS (readonly after init.) */
static task* _global_handler_task = 0;
static st_ptr _global_handler_rootptr;

// structs
struct _ipt_internal
{
	ptr_list* top;
	ptr_list* free;

	cle_output* response;
	void* respdata;

	event_handler* event_chain_begin;

	ptr_list* input;
	task* mem_tk;

	sys_handler_data sys;

	uint depth;
};

// nil-output-handler (used for async-handlers)
static int _nil1(void* v){return 0;}
static int _nil2(void* v,cdat c,uint u){return 0;}
static cle_output _nil_out = {_nil1,_nil2,_nil1,_nil1,_nil2,_nil1};

// pipeline-output-to-input (chain functions)

static int _po_start(void* hdl)
{
	event_handler* ehdl = (event_handler*)hdl;
	if(ehdl->thehandler->do_setup != 0)
		ehdl->thehandler->do_setup(ehdl);
	return 0;
}

static int _po_end(void* hdl, cdat msg, uint msglength)
{
	event_handler* ehdl = (event_handler*)hdl;
	if(ehdl->thehandler->do_end != 0)
		ehdl->thehandler->do_end(ehdl,msg,msglength);
	return 0;
}

static int _po_pop(void* hdl)
{
	event_handler* ehdl = (event_handler*)hdl;
	return 0;
}

static int _po_push(void* hdl)
{
	event_handler* ehdl = (event_handler*)hdl;
	return 0;
}

static int _po_data(void* hdl, cdat data, uint length)
{
	event_handler* ehdl = (event_handler*)hdl;
	return 0;
}

static int _po_next(void* hdl)
{
	event_handler* ehdl = (event_handler*)hdl;
	return 0;
}

static cle_output _pipeline_out = {_po_start,_po_end,_po_pop,_po_push,_po_data,_po_next};

// pipeline activate all handlers

static int _pa_start(void* hdl)
{
	event_handler* ehdl = (event_handler*)hdl;
	if(ehdl->thehandler->do_setup != 0)
		ehdl->thehandler->do_setup(ehdl);
	return 0;
}

static int _pa_end(void* hdl, cdat msg, uint msglength)
{
	event_handler* ehdl = (event_handler*)hdl;
	if(ehdl->thehandler->do_end != 0)
		ehdl->thehandler->do_end(ehdl,msg,msglength);
	return 0;
}

static int _pa_pop(void* hdl)
{
	event_handler* ehdl = (event_handler*)hdl;
	return 0;
}

static int _pa_push(void* hdl)
{
	event_handler* ehdl = (event_handler*)hdl;
	return 0;
}

static int _pa_data(void* hdl, cdat data, uint length)
{
	event_handler* ehdl = (event_handler*)hdl;
	return 0;
}

static int _pa_next(void* hdl)
{
	event_handler* ehdl = (event_handler*)hdl;
	return 0;
}

static cle_output _pipeline_all = {_pa_start,_pa_end,_pa_pop,_pa_push,_pa_data,_pa_next};


static ptr_list* _new_ptr_stack(_ipt* ipt, task* t, st_ptr* pt, ptr_list* link)
{
	ptr_list* elm;
	if(ipt->free)
	{
		elm = ipt->free;
		ipt->free = elm->link;
	}
	else
		elm = (ptr_list*)tk_alloc(ipt->mem_tk,sizeof(ptr_list));

	elm->pt = *pt;
	elm->link = link;
	elm->t = t;

	return elm;
}

/* initializers */
void cle_initialize_system()
{
	// setup system event-handlers
	_global_handler_task = tk_create_task(0,0);

	st_empty(_global_handler_task,&_global_handler_rootptr);

	tk_ref(_global_handler_task,_global_handler_rootptr.pg);
}

void cle_add_sys_handler(cdat eventmask, uint mask_length, cle_syshandler* handler)
{
	cle_syshandler* exsisting;
	st_ptr pt = _global_handler_rootptr;

	st_insert(_global_handler_task,&pt,eventmask,mask_length);

	st_insert(_global_handler_task,&pt,HEAD_EVENT,HEAD_SIZE);

	if(st_get(_global_handler_task,&pt,(char*)&exsisting,sizeof(cle_syshandler*)) == -1)
		// prepend to list
		handler->next_handler = exsisting;
	else
		handler->next_handler = 0;

	st_update(_global_handler_task,&pt,(cdat)&handler,sizeof(cle_syshandler*));
}

// input-functions

_ipt* cle_start(cdat eventid, uint event_len,
				cdat userid, uint userid_len, char* user_roles[],
					cle_output* response, void* responsedata, 
						cle_pagesource* app_source, cle_psrc_data app_source_data, 
							cle_pagesource* session_source, cle_psrc_data session_source_data)
{
	task* t,*ti;
	_ipt* ipt;
	event_handler* hdlists[4];
	event_handler* hdl;
	int from,i,allowed;

	st_ptr pt,eventpt,syspt,instance;

	// ipt setup - internal task
	t = tk_create_task(0,0);
	ipt = (_ipt*)tk_alloc(t,sizeof(_ipt));
	// default null
	memset(ipt,0,sizeof(_ipt));

	ipt->mem_tk = t;

	if(response == 0)
		response = &_nil_out;

	ipt->response = response;
	ipt->respdata = responsedata;

	ipt->sys.eventid = eventid;
	ipt->sys.event_len = event_len;
	ipt->sys.userid = userid;
	ipt->sys.userid_len = userid_len;

	/* get a root ptr to instance-db */
	ti = tk_create_task(app_source,app_source_data);

	tk_root_ptr(ti,&instance);

	// no username? -> root/sa
	allowed = (userid_len == 0);

	eventpt = instance;
	if(st_move(ti,&eventpt,HEAD_EVENT,HEAD_SIZE) != 0)
	{
		eventpt.pg = 0;
		if(allowed == 0)
		{
			// no event-structure in this instance -> exit
			_error(event_not_allowed);
			tk_drop_task(ti);
			tk_drop_task(t);
			return 0;
		}
	}

	syspt = _global_handler_rootptr;
	from = 0;

	for(i = 0; i < event_len; i++)
	{
		// event-part-boundary
		if(eventid[i] != 0)
			continue;

		// lookup event-part (module-level)
		if(eventpt.pg != 0 && st_move(ti,&eventpt,eventid + from,i - from) != 0)
		{
			eventpt.pg = 0;
			// not found! No such event allowed/no possible end-handlers
			if(syspt.pg == 0)
			{
				allowed = 0;
				break;
			}
		}

		// lookup system-level handlers
		if(syspt.pg != 0 && st_move(_global_handler_task,&syspt,eventid + from,i - from) != 0)
		{
			syspt.pg = 0;
			// not found! No such event allowed/no possible end-handlers
			if(eventpt.pg == 0)
			{
				allowed = 0;
				break;
			}
		}

		// lookup allowed roles (if no access yet)
		if(allowed == 0)
		{
			pt = eventpt;
			if(st_move(ti,&pt,HEAD_ROLES,HEAD_SIZE) == 0)
			{
				// has allowed-roles
				it_ptr it;
				int r = 0;

				it_create(t,&it,&pt);

				while(allowed == 0 && user_roles[r] != 0)
				{
					it_load(t,&it,user_roles[r] + 1,*user_roles[r]);

					if(it_next_eq(t,&pt,&it) == 0)
						break;

					do
					{
						int cmp = memcmp(user_roles[r] + 1,it.kdata,it.kused < *user_roles[r]?it.kused:*user_roles[r]);
						if(cmp > 0)
							break;
						else if(cmp == 0)
						{
							if(it.kused == *user_roles[r])
							{
								allowed = 1;
								break;
							}
							else if(it.kused < *user_roles[r])
								break;
						}
						
						r++;
					}
					while(user_roles[r] != 0);
				}

				it_dispose(t,&it);
			}
		}

		// get pipeline-handlers
		// module-level
		pt = eventpt;
		if(pt.pg != 0 && st_move(ti,&pt,HEAD_HANDLER,HEAD_SIZE) == 0)
		{
			it_ptr it;
			it_create(t,&it,&pt);

			// iterate instance-refs / event-handler-id
			while(it_next(t,&pt,&it))
			{
				event_handler* hdl;
				struct sys_event_id seid;

				if(st_get(ti,&pt,(char*)&seid,sizeof(struct sys_event_id)) < 0)
				{
					// TODO: verify application is activated - dont send events to sleeping apps
					hdl = (event_handler*)tk_alloc(t,sizeof(struct event_handler));

					hdl->next = hdlists[seid.handlertype];
					hdlists[seid.handlertype] = hdl;

					hdl->thehandler = &_runtime_handler;
					hdl->event_id = seid;
					hdl->eventdata = &ipt->sys;
				}
			}

			it_dispose(t,&it);
		}

		// system-level
		pt = syspt;
		if(pt.pg != 0 && st_move(_global_handler_task,&pt,HEAD_HANDLER,HEAD_SIZE) == 0)
		{
			cle_syshandler* syshdl;
			if(st_get(_global_handler_task,&pt,(char*)&syshdl,sizeof(cle_syshandler*)) == -1)
			{
				do
				{
					event_handler* hdl = (event_handler*)tk_alloc(t,sizeof(struct event_handler));

					hdl->next = hdlists[syshdl->systype];
					hdlists[syshdl->systype] = hdl;
					hdl->thehandler = syshdl;
					hdl->eventdata = &ipt->sys;

					// next in list...
					syshdl = syshdl->next_handler;
				}
				while(syshdl != 0);
			}
		}

		from = i + 1;
	}

	// access allowed? and is there anyone in the other end?
	if(allowed == 0 || ((uint)hdlists[SYNC_REQUEST_HANDLER]|(uint)hdlists[ASYNC_REQUEST_HANDLER]) == 0)
	{
		_error(event_not_allowed);
		tk_drop_task(ti);
		tk_drop_task(t);
		return 0;
	}

	// init async-handlers
	if(hdlists[ASYNC_REQUEST_HANDLER] != 0)
	{
		hdl = hdlists[ASYNC_REQUEST_HANDLER];
		ipt->event_chain_begin = hdl;

		do
		{
			// "no output"-handler on all async's
			hdl->response = &_nil_out;

			// put in separat tasks/transactions
			hdl->instance_tk = tk_create_task(app_source,app_source_data);
			tk_root_ptr(hdl->instance_tk,&hdl->instance);

			hdl = hdl->next;
		}
		while(hdl != 0);
	}

	// setup sync-handler-chain
	if(hdlists[SYNC_REQUEST_HANDLER] != 0)
	{
		event_handler* sync_handler = hdlists[SYNC_REQUEST_HANDLER];

		sync_handler->instance_tk = ti;
		sync_handler->instance = instance;

		// there can be only one active sync-handler (dont mess-up output with concurrent event-handlers)
		// setup response-handler chain (only make sense with sync handlers)
		if(hdlists[PIPELINE_RESPONSE] != 0)
		{
			event_handler* last;
			// in correct order (most specific handler comes first)
			hdl = hdlists[PIPELINE_RESPONSE];

			// sync-handler outputs through this chain
			sync_handler->response = &_pipeline_out;
			sync_handler->respdata = hdl;

			do
			{
				last = hdl;
				hdl->response = &_pipeline_out;
				hdl->respdata = hdl->next;

				// same task as request-handler
				hdl->instance_tk = ti;
				hdl->instance = instance;

				hdl = hdl->next;
			}
			while(hdl != 0);

			// and finally the original output-target
			last->response = response;
			last->respdata = responsedata;
		}
		else
		{
			sync_handler->response = response;
			sync_handler->respdata = responsedata;
		}

		sync_handler->next = ipt->event_chain_begin;
		ipt->event_chain_begin = sync_handler;
	}

	// setup request-handler chain
	if(hdlists[PIPELINE_REQUEST] != 0)
	{
		// inverse order (most general handlers comes first)
		event_handler* last;
		cle_output* resp;
		void* data = ipt->event_chain_begin;

		// just a single receiver -> just (hard)chain together
		if(ipt->event_chain_begin->next == 0)
			resp = &_pipeline_out;
		else
			resp = &_pipeline_all;

		hdl = hdlists[PIPELINE_REQUEST];

		do
		{
			last = hdl;
			hdl->response = resp;
			hdl->respdata = data;

			hdl->instance_tk = ti;
			hdl->instance = instance;

			resp = &_pipeline_out;
			data = hdl;

			hdl = hdl->next;
		}
		while(hdl != 0);

		last->next = 0;
		ipt->event_chain_begin = last;
	}

	// not taken by sync or pipeline? kill instance-task now
	if((uint)hdlists[SYNC_REQUEST_HANDLER]|(uint)hdlists[PIPELINE_REQUEST] == 0)
		tk_drop_task(ti);

	for(hdl = ipt->event_chain_begin; hdl != 0; hdl = hdl->next)
	{
		if(cle_notify_start(hdl,&ipt->sys))
			break;
	}

	// prepare for event-stream
	st_empty(t,&ipt->top->pt);
	return ipt;
}

void cle_next(_ipt* ipt)
{
	event_handler* hdl;
	ptr_list* elm;
	if(ipt == 0 || ipt->sys.error != 0)
		return;

	if(ipt->depth != 0)
	{
		_error(input_incomplete);
		return;
	}

	elm = _new_ptr_stack(ipt,ipt->mem_tk,&ipt->top->pt,0);

	if(ipt->input != 0)
		ipt->input->link = elm;

	ipt->input = elm;

	// run handler donext's
	for(hdl = ipt->event_chain_begin; hdl != 0; hdl = hdl->next)
	{
		if(cle_notify_next(hdl,&ipt->sys,elm))
			break;
	}

	if(ipt->sys.error != 0)
		return;

	ipt->sys.next_call++;
	ipt->depth = 0;

	// done processing .. clear and ready for next
	st_empty(ipt->mem_tk,&ipt->top->pt);
}

void cle_submit(_ipt* ipt, task* t, st_ptr* root)
{
	event_handler* hdl;
	ptr_list* elm;

	if(ipt == 0 || ipt->sys.error != 0)
		return;

	elm = _new_ptr_stack(ipt,t,root,0);

	if(ipt->input != 0)
		ipt->input->link = elm;

	ipt->input = elm;

	// run handler donext's
	for(hdl = ipt->event_chain_begin; hdl != 0; hdl = hdl->next)
	{
		if(cle_notify_next(hdl,&ipt->sys,elm))
			break;
	}

	if(ipt->sys.error != 0)
		return;
}

void cle_end(_ipt* ipt, cdat code, uint length)
{
	event_handler* hdl;
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

	// run handlers end
	for(hdl = ipt->event_chain_begin; hdl != 0; hdl = hdl->next)
		cle_notify_end(hdl,&ipt->sys);

	tk_drop_task(ipt->mem_tk);
}

void cle_pop(_ipt* ipt)
{
	ptr_list* elm;
	if(ipt == 0 || ipt->sys.error != 0)
		return;

	if(ipt->depth == 0)
	{
		_error(input_underflow);
		return;
	}

	elm = ipt->top;
	ipt->top = ipt->top->link;

	elm->link = ipt->free;
	ipt->free = elm;

	ipt->depth--;
}

void cle_push(_ipt* ipt)
{
	if(ipt == 0 || ipt->sys.error != 0)
		return;

	ipt->top = _new_ptr_stack(ipt,ipt->mem_tk,&ipt->top->pt,ipt->top);

	ipt->depth++;
}

void cle_data(_ipt* ipt, cdat data, uint length)
{
	if(ipt == 0 || ipt->sys.error != 0)
		return;

	st_append(ipt->mem_tk,&ipt->top->pt,data,length);
}
