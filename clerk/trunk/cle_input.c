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

#define _error(txt) ipt->sys.response->end(ipt->sys.respdata,txt,sizeof(txt))

// structs
struct _ptr_stack
{
	struct _ptr_stack* prev;
	st_ptr pt;
};

typedef struct _mod_handler
{
	cle_syshandler* thehandler;
	struct _mod_handler* next;
	struct _ptr_stack* current_input;
	struct sys_event_id event_id;
}_mod_handler;

struct _ipt_internal
{
	task* t;
	struct _ptr_stack* top;
	struct _ptr_stack* free;

	sys_handler_data sys;

	struct _ptr_stack* input_chain;
	struct _ptr_stack* input_chain_last;

	_mod_handler* hdlists[4];

	st_ptr current;

	uint maxdepth;
	uint depth;
};

// hook-handler for the core runtimesystem
static cle_syshandler _runtime_handler = {0,0,0,0,0};

// nil-output-handler (used for async-handlers)
static int _nil1(void* v){return 0;}
static int _nil2(void* v,cdat c,uint u){return 0;}
static cle_output _nil_out = {_nil1,_nil2,_nil1,_nil1,_nil2,_nil1};

/* GLOBALS (readonly after init.) */
static task* _global_handler_task = 0;
static st_ptr _global_handler_rootptr;

struct _ptr_stack* _new_ptr_stack(_ipt* ipt, st_ptr* pt, struct _ptr_stack* prev)
{
	struct _ptr_stack* elm;
	if(ipt->free)
	{
		elm = ipt->free;
		ipt->free = elm->prev;
	}
	else
		elm = (struct _ptr_stack*)tk_alloc(ipt->t,sizeof(struct _ptr_stack));

	elm->pt = *pt;
	elm->prev = prev;

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

	st_append(_global_handler_task,&pt,(cdat)&handler,sizeof(cle_syshandler*));
}

// loop through event-handlers and update waiting handlers and ready them
static void _notify_handlers(_ipt* ipt)
{
	// run module-handler setups
	_mod_handler* handler = ipt->hdlists[0];
	while(handler)
	{

		// next handler
		handler = handler->next;
	}
}

// input-functions

_ipt* cle_start(cdat eventid, uint event_len,
				cdat userid, uint userid_len, char* user_roles[],
					cle_output* response, void* responsedata, 
						cle_pagesource* app_source, cle_psrc_data app_source_data, 
							cle_pagesource* session_source, cle_psrc_data session_source_data)
{
	_mod_handler* handler;
	_ipt* ipt;
	task* t;
	int from,i,allowed;

	st_ptr pt,eventpt,syspt;

	// ipt setup - internal task
	t = tk_create_task(0,0);
	ipt = (_ipt*)tk_alloc(t,sizeof(_ipt));
	// default null
	memset(ipt,0,sizeof(_ipt));

	ipt->t = t;

	ipt->sys.response = response;
	ipt->sys.respdata = responsedata;

	ipt->sys.eventid = eventid;
	ipt->sys.event_len = event_len;
	ipt->sys.userid = userid;
	ipt->sys.userid_len = userid_len;

	/* get a root ptr to instance-db */
	ipt->sys.instance_tk = tk_create_task(app_source,app_source_data);
	pt.pg = app_source->root_page(app_source_data);
	pt.key = sizeof(page);
	pt.offset = 0;
	ipt->sys.instance = pt;

	// no username? -> root/sa
	allowed = (userid_len == 0);

	eventpt = ipt->sys.instance;
	if(st_move(ipt->sys.instance_tk,&eventpt,HEAD_EVENT,HEAD_SIZE) != 0)
	{
		eventpt.pg = 0;
		if(allowed == 0)
		{
			// no event-structure in this instance -> exit
			_error(event_not_allowed);
			tk_unref(ipt->sys.instance_tk,eventpt.pg);
			tk_drop_task(ipt->sys.instance_tk);
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
		if(eventpt.pg != 0 && st_move(ipt->sys.instance_tk,&eventpt,eventid + from,i - from) != 0)
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
			if(st_move(ipt->sys.instance_tk,&pt,HEAD_ROLES,HEAD_SIZE) == 0)
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
		if(pt.pg != 0 && st_move(ipt->sys.instance_tk,&pt,HEAD_HANDLER,HEAD_SIZE) == 0)
		{
			it_ptr it;
			it_create(t,&it,&pt);

			// iterate instance-refs / event-handler-id
			while(it_next(t,&pt,&it))
			{
				_mod_handler* hdl;
				struct sys_event_id seid;

				if(st_get(ipt->sys.instance_tk,&pt,(char*)&seid,sizeof(struct sys_event_id)) < 0)
				{
					if(seid.handlertype >= PIPELINE_REQUEST || i + 1 == event_len)
					{
						hdl = (_mod_handler*)tk_alloc(t,sizeof(struct _mod_handler));

						hdl->next = ipt->hdlists[seid.handlertype];
						ipt->hdlists[seid.handlertype] = hdl;

						hdl->thehandler = &_runtime_handler;
						hdl->event_id = seid;
					}
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
					if(syshdl->systype >= PIPELINE_REQUEST || i + 1 == event_len)
					{					
						_mod_handler* hdl = (_mod_handler*)tk_alloc(t,sizeof(struct _mod_handler));

						hdl->next = ipt->hdlists[syshdl->systype];
						ipt->hdlists[syshdl->systype] = hdl;

						hdl->thehandler = syshdl;
					}

					// next in list...
					syshdl = syshdl->next_handler;
				}
				while(syshdl);
			}
		}

		from = i + 1;
	}

	// access allowed? and is there anything anyone in the other end?
	if(allowed == 0 || (ipt->hdlists[SYNC_REQUEST_HANDLER] == 0 && ipt->hdlists[ASYNC_REQUEST_HANDLER] == 0))
	{
		_error(event_not_allowed);
		tk_unref(ipt->sys.instance_tk,eventpt.pg);
		tk_drop_task(ipt->sys.instance_tk);
		tk_drop_task(t);
		return 0;
	}

	_notify_handlers(ipt);

	// prepare for event-stream
	st_empty(t,&ipt->top->pt);
	ipt->current = ipt->top->pt;

	ipt->input_chain = ipt->input_chain_last
		= _new_ptr_stack(ipt,&ipt->current,0);

	return ipt;
}

void cle_next(_ipt* ipt)
{
	struct _handler_list* handlers;
	struct _ptr_stack* elm;
	if(ipt == 0 || ipt->sys.error != 0)
		return;

	if(ipt->top->prev != 0)
	{
		_error(input_incomplete);
		return;
	}

	_notify_handlers(ipt);

	ipt->sys.next_call++;
	ipt->depth = ipt->maxdepth = 0;

	// done processing .. clear and ready for next
	st_empty(ipt->t,&ipt->top->pt);
	ipt->current = ipt->top->pt;

	ipt->input_chain_last->prev = _new_ptr_stack(ipt,&ipt->current,0);
	ipt->input_chain_last = ipt->input_chain_last->prev;
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

	_notify_handlers(ipt);
}

void cle_push(_ipt* ipt)
{
	struct _ptr_stack* elm;
	if(ipt == 0 || ipt->sys.error != 0)
		return;

	elm = _new_ptr_stack(ipt,&ipt->top->pt,ipt->top);

	ipt->top = elm;
	ipt->current = ipt->top->pt;

	ipt->depth++;
	if(ipt->depth > ipt->maxdepth) ipt->maxdepth = ipt->depth;
}

void cle_pop(_ipt* ipt)
{
	struct _ptr_stack* elm;
	if(ipt == 0 || ipt->sys.error != 0)
		return;

	if(ipt->top->prev == 0)
	{
		_error(input_underflow);
		return;
	}

	elm = ipt->top;
	ipt->top = ipt->top->prev;
	ipt->current = ipt->top->pt;

	elm->prev = ipt->free;
	ipt->free = elm;

	ipt->depth--;
}

void cle_data(_ipt* ipt, cdat data, uint length)
{
	if(ipt == 0 || ipt->sys.error != 0)
		return;

	st_append(ipt->t,&ipt->current,data,length);
}
