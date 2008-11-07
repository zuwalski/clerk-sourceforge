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

/*
*	The main input-interface to the running system
*	Events/messages are "pumped" in through the exported set of functions
*/

#ifdef CLERK_SINGLE_THREAD

void cle_notify_start(event_handler* handler)
{
	while(handler != 0)
	{
		if(handler->thehandler->input.start != 0)
			handler->thehandler->input.start(handler);

		handler = handler->next;
	}
}

void cle_notify_next(event_handler* handler, ptr_list* nxtelement)
{
	while(handler != 0)
	{
		if(handler->thehandler->input.next != 0)
			handler->thehandler->input.next(handler);

		handler = handler->next;
	}
}

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

#define HEAD_EVENT "\0e"
#define HEAD_HANDLER "\0h"
#define HEAD_ROLES "\0r"

// error-messages
static char input_underflow[] = "stream:input underflow";
static char input_incomplete[] = "stream:input incomplete";
static char event_not_allowed[] = "stream:event not allowed";

enum request_state
{
	INITIAL = 0
};

// structs
struct _ipt_internal
{
	task* mem_tk;

	ptr_list* top;
	ptr_list* free;
	ptr_list* input;

	event_handler* event_chain_begin;

	sys_handler_data sys;

	uint depth;
	enum request_state state;
};

// nil output-handler

static int _nil1(void* v){return 0;}
static int _nil2(void* v,cdat c,uint u){return 0;}
static int _nil3(void* v, st_ptr* st){return 0;}
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
int cle_standard_pop(event_handler* hdl)
{
	ptr_list* elm = hdl->top;
	hdl->top = hdl->top->link;

	elm->link = hdl->free;
	hdl->free = elm;
	return 0;
}

int cle_standard_push(event_handler* hdl)
{
	ptr_list* elm;
	if(hdl->free)
	{
		elm = hdl->free;
		hdl->free = elm->link;
	}
	else
		elm = (ptr_list*)tk_alloc(hdl->instance_tk,sizeof(ptr_list));

	elm->pt = hdl->top->pt;
	elm->link = hdl->top;
	return 0;
}

int cle_standard_data(event_handler* hdl, cdat data, uint length)
{
	st_append(hdl->instance_tk,&hdl->top->pt,data,length);
	return 0;
}

int cle_standard_submit(event_handler* hdl, st_ptr* st)
{
	return 0;
}

// pipeline activate All handlers

static int _pa_start(event_handler* hdl)
{
	cle_notify_start(hdl);
	return 0;
}

static int _pa_end(event_handler* hdl, cdat msg, uint msglength)
{
	cle_notify_end(hdl,msg,msglength);
	return 0;
}

static int _pa_next(event_handler* hdl)
{
	cle_notify_next(hdl,hdl->top);
	return 0;
}

static int _pa_pop(event_handler* hdl)
{
	while(hdl != 0)
	{
		cle_standard_pop(hdl);
		hdl = hdl->next;
	}
	return 0;
}

static int _pa_push(event_handler* hdl)
{
	while(hdl != 0)
	{
		cle_standard_push(hdl);
		hdl = hdl->next;
	}
	return 0;
}

static int _pa_data(event_handler* hdl, cdat data, uint length)
{
	while(hdl != 0)
	{
		cle_standard_data(hdl,data,length);
		hdl = hdl->next;
	}
	return 0;
}

static int _pa_submit(event_handler* hdl, st_ptr* st)
{
	while(hdl != 0)
	{
		cle_standard_submit(hdl,st);
		hdl = hdl->next;
	}
	return 0;
}

static cle_pipe _pipeline_all = {_pa_start,_pa_next,_pa_end,_pa_pop,_pa_push,_pa_data,_pa_submit};

static ptr_list* _new_ptr_stack(_ipt* ipt, st_ptr* pt, ptr_list* link)
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

	return elm;
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

/* setup module-level handler */
void cle_add_mod_handler(task* app_instance, cdat eventmask, uint mask_length, struct mod_target* target)
{}

/* control role-access */
void cle_allow_role(task* app_instance, cdat eventmask, uint mask_length, cdat role, uint role_length)
{
	st_ptr root;

	// max length!
	if(role_length > 255)
		return;

	tk_root_ptr(app_instance,&root);

	if(st_move(app_instance,&root,HEAD_EVENT,HEAD_SIZE) != 0)
		return;

	st_insert(app_instance,&root,eventmask,mask_length);

	st_insert(app_instance,&root,HEAD_ROLES,HEAD_SIZE);

	st_insert(app_instance,&root,role,role_length);
}

void cle_revoke_role(task* app_instance, cdat eventmask, uint mask_length, cdat role, uint role_length)
{}

void cle_format_instance(task* app_instance)
{
	st_ptr root;
	tk_root_ptr(app_instance,&root);

	// all clear
	st_delete(app_instance,&root,0,0);

	// insert event-hook
	st_insert(app_instance,&root,HEAD_EVENT,HEAD_SIZE);
}

// input-functions
#define _error(txt) response->start(responsedata);response->end(responsedata,txt,sizeof(txt))

_ipt* cle_start(st_ptr config, cdat eventid, uint event_len,
				cdat userid, uint userid_len, char* user_roles[],
					cle_pipe* response, void* responsedata, task* app_instance)
{
	_ipt* ipt;
	event_handler* hdlists[4];
	event_handler* hdl;
	st_ptr pt,eventpt,syspt,instance;
	int from,i,allowed;

	// ipt setup - internal task
	ipt = (_ipt*)tk_alloc(app_instance,sizeof(_ipt));
	// default null
	memset(ipt,0,sizeof(_ipt));

	memset(hdlists,0,sizeof(hdlists));

	ipt->mem_tk = app_instance;

	if(response == 0)
		response = &_nil_out;

	ipt->sys.config = config;
	ipt->sys.eventid = eventid;
	ipt->sys.event_len = event_len;
	ipt->sys.userid = userid;
	ipt->sys.userid_len = userid_len;

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
					{
						allowed = 1;
						break;
					}

					for(r++;user_roles[r] != 0;r++)
					{
						cmp = memcmp(user_roles[r] + 1,it.kdata,it.kused < *user_roles[r]?it.kused:*user_roles[r]);
						if(cmp > 0)
							break;
					}

					//do
					//{
					//	int cmp = memcmp(user_roles[r] + 1,it.kdata,it.kused < *user_roles[r]?it.kused:*user_roles[r]);
					//	if(cmp > 0)
					//		break;
					//	else if(cmp == 0)
					//	{
					//		if(it.kused == *user_roles[r])
					//		{
					//			allowed = 1;
					//			break;
					//		}
					//		else if(it.kused < *user_roles[r])
					//			break;
					//	}
					//	
					//	r++;
					//}
					//while(user_roles[r] != 0);
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
				event_handler* hdl;
				struct mod_target target;

				if(st_get(app_instance,&pt,(char*)&target,sizeof(struct mod_target)) < 0)
				{
					// TODO: verify application is activated - dont send events to sleeping apps
					hdl = (event_handler*)tk_alloc(app_instance,sizeof(struct event_handler));

					hdl->next = hdlists[target.handlertype];
					hdlists[target.handlertype] = hdl;

					hdl->thehandler = &_runtime_handler;
					hdl->target = target;
					hdl->eventdata = &ipt->sys;
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
					event_handler* hdl = (event_handler*)tk_alloc(app_instance,sizeof(struct event_handler));

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

				last = hdl;
				hdl = hdl->next;
			}
			while(hdl != 0);

			// and finally the original output-target
			last->response = response;
			last->respdata = responsedata;

			last->instance_tk = app_instance;
			last->instance = instance;
		}
		else
		{
			sync_handler->response = response;
			sync_handler->respdata = responsedata;

			sync_handler->instance_tk = app_instance;
			sync_handler->instance = instance;
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

		// just a single receiver -> just (hard)chain together
		if(ipt->event_chain_begin->next != 0)
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

			resp = &hdl->thehandler->input;
			data = hdl;

			hdl = hdl->next;
		}
		while(hdl != 0);

		last->next = 0;
		ipt->event_chain_begin = last;
	}

	// prepare for event-stream
	st_empty(app_instance,&ipt->top->pt);

	cle_notify_start(ipt->event_chain_begin);
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
		cle_end(ipt,input_incomplete,sizeof(input_incomplete));
		return;
	}

	elm = _new_ptr_stack(ipt,&ipt->top->pt,0);

	if(ipt->input != 0)
		ipt->input->link = elm;

	ipt->input = elm;
	ipt->depth = 0;

	// done processing .. clear and ready for next
	st_empty(ipt->mem_tk,&ipt->top->pt);

	// run handler next's
	cle_notify_next(ipt->event_chain_begin,elm);
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
	cle_notify_end(ipt->event_chain_begin,code,length);
}

void cle_pop(_ipt* ipt)
{
	ptr_list* elm;
	if(ipt == 0 || ipt->sys.error != 0)
		return;

	if(ipt->depth == 0)
	{
		cle_end(ipt,input_underflow,sizeof(input_underflow));
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

	ipt->top = _new_ptr_stack(ipt,&ipt->top->pt,ipt->top);

	ipt->depth++;
}

void cle_data(_ipt* ipt, cdat data, uint length)
{
	if(ipt == 0 || ipt->sys.error != 0)
		return;

	st_append(ipt->mem_tk,&ipt->top->pt,data,length);
}

void cle_submit(_ipt* ipt, st_ptr* root)
{
	if(ipt == 0 || ipt->sys.error != 0)
		return;

	if(ipt->depth == 0 && st_is_empty(&ipt->top->pt))
		ipt->top->pt = *root;
	else
	{
		// pointer to "root" from "top"
	}
}
