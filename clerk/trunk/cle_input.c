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
static char unknown_user[] = "unknown user";
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

enum run_state
{
	RUNNING,
	WAITING,
	FAILED
};

typedef struct _mod_handler
{
	struct _mod_handler* next;
	struct _ptr_stack* current_input;
	enum run_state run_state;
}_mod_handler;

struct _ipt_internal
{
	struct _ptr_stack* top;
	struct _ptr_stack* free;

	cle_syshandler* system;
	sys_handler_data sys;

	_mod_handler* handler_list;
	struct _ptr_stack* input_chain;
	struct _ptr_stack* input_chain_last;

	task* t;

	cdat _error;
	uint _errlength;

	st_ptr current;

	uint maxdepth;
	uint depth;
};

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

int cle_add_sys_handler(cdat eventmask, uint mask_length, cle_syshandler* handler)
{
	st_ptr pt = _global_handler_rootptr;

	if(!_validate_event_name(eventmask,mask_length))
		return -1;

	if(!st_insert(_global_handler_task,&pt,eventmask,mask_length))
		return -2;

	st_append(_global_handler_task,&pt,HEAD_EVENT,HEAD_SIZE);

	st_append(_global_handler_task,&pt,(cdat)&handler,sizeof(cle_syshandler*));

	return 0;
}

// input-functions

_ipt* cle_start(cdat eventid, uint event_len,
				cdat userid, uint userid_len, 
					cle_output* response, void* responsedata, 
						cle_pagesource* app_source, cle_psrc_data app_source_data, 
							cle_pagesource* session_source, cle_psrc_data session_source_data)
{
	_mod_handler* handler;
	_ipt* ipt;
	task* t;
	int from,i,allowed;

	st_ptr pt,userpt,eventpt;

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

	allowed = 0;
	// validate user - validate event-settings
	// no username? -> root/sa
	if(userid_len > 0)
	{
		userpt = ipt->sys.instance;
		tk_ref(ipt->sys.instance_tk,userpt.pg);
		// user exsist?
		if(st_move(ipt->sys.instance_tk,&userpt,HEAD_USERS,HEAD_SIZE) ||
			st_move(ipt->sys.instance_tk,&userpt,userid,userid_len))
		{
			_error(unknown_user);
			tk_unref(ipt->sys.instance_tk,userpt.pg);
			tk_drop_task(ipt->sys.instance_tk);
			tk_drop_task(t);
			return 0;
		}
	}
	else
		allowed = 1;	// admin-user

	eventpt = ipt->sys.instance;
	if(st_move(ipt->sys.instance_tk,&eventpt,HEAD_EVENT,HEAD_SIZE))
	{
		// no event-structure in this instance -> exit
	}

	from = 0;
	for(i = 0; i < event_len; i++)
	{
		// event-part-boundary
		if(eventid[i] == 0)
		{
			// lookup event-part
			if(st_move(ipt->sys.instance_tk,&eventpt,eventid + from,i - from))
			{
				// not found! No such event allowed/no possible end-handlers
				allowed = 0;
				break;
			}

			// lookup allowed roles (if no access yet)
			if(allowed == 0)
			{
				pt = eventpt;
				if(st_move(ipt->sys.instance_tk,&pt,HEAD_ROLES,HEAD_SIZE) == 0)
				{
					// has allowed-roles
				}
			}

			// get handlers
			pt = eventpt;
			if(st_move(ipt->sys.instance_tk,&pt,HEAD_HANDLER,HEAD_SIZE) == 0)
			{
				it_ptr it;
				it_create(t,&it,&pt);

				// iterate instance-refs / event-handler-id
				while(it_next(t,&pt,&it))
				{
					_mod_handler* hdl;
					
					// TODO: request-pipeline, response-pipeline or end-handler
					// .. Sync & async handlers (/ responding and no-response-handlers)

					hdl = (_mod_handler*)tk_alloc(ipt->t,sizeof(struct _mod_handler));
					hdl->next = ipt->handler_list;
					ipt->handler_list = hdl;
				}

				it_dispose(t,&it);
			}

			from = i;
		}
	}

	if(allowed == 0 || ipt->handler_list == 0)
	{
		_error(event_not_allowed);
		tk_unref(ipt->sys.instance_tk,userpt.pg);
		tk_drop_task(ipt->sys.instance_tk);
		tk_drop_task(t);
		return 0;
	}

	// lookup system-eventhandler
	pt = _global_handler_rootptr;
	if(!st_move(_global_handler_task,&pt,eventid,event_len) &&
		!st_move(_global_handler_task,&pt,HEAD_EVENT,HEAD_SIZE))
	{
		if(st_get(_global_handler_task,&pt,(char*)&ipt->system,sizeof(cle_syshandler*)) != -1)
			ipt->system = 0;
		else
			// run system-handler setup
			ipt->system->do_setup(&ipt->sys);
	}

	// run module-handler setups
	handler = ipt->handler_list;
	while(handler)
	{
		// call runtime to setup/start handlers

		// next handler
		handler = handler->next;
	}

	// prepare for event-stream
	st_empty(t,&ipt->top->pt);
	ipt->current = ipt->top->pt;

	ipt->input_chain = ipt->input_chain_last
		= _new_ptr_stack(ipt,&ipt->current,0);

	return ipt;
}

// loop through event-handlers and update waiting handlers and ready them
static void _check_handlers(_ipt* ipt)
{
	// run module-handler setups
	_mod_handler* handler = ipt->handler_list;
	while(handler)
	{

		// next handler
		handler = handler->next;
	}
}

void cle_next(_ipt* ipt)
{
	struct _handler_list* handlers;
	struct _ptr_stack* elm;
	if(ipt == 0 || ipt->_error != 0)
		return;

	if(ipt->top->prev != 0)
	{
		_error(input_incomplete);
		return;
	}

	if(ipt->system && ipt->system->do_next)
		ipt->system->do_next(&ipt->sys,ipt->top->pt,ipt->maxdepth);

	ipt->sys.next_call++;
	ipt->depth = ipt->maxdepth = 0;

	// done processing .. clear and ready for next
	st_empty(ipt->t,&ipt->top->pt);
	ipt->current = ipt->top->pt;

	ipt->input_chain_last->prev = _new_ptr_stack(ipt,&ipt->current,0);
	ipt->input_chain_last = ipt->input_chain_last->prev;

	_check_handlers(ipt);
}

void cle_end(_ipt* ipt, cdat code, uint length)
{
	if(ipt == 0)
		return;

	if(ipt->_error == 0)
	{
		// reporting an error
		if(code != 0 && length != 0)
		{
			ipt->_error = code;
			ipt->_errlength = length;
		}
		else
			// signal end of input
			ipt->_error = "";
	}

	if(ipt->system && ipt->system->do_end)
		ipt->system->do_end(&ipt->sys,code,length);

	_check_handlers(ipt);
}

void cle_push(_ipt* ipt)
{
	struct _ptr_stack* elm;
	if(ipt == 0 || ipt->_error != 0)
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
	if(ipt == 0 || ipt->_error != 0)
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
	if(ipt == 0 || ipt->_error != 0)
		return;

	st_append(ipt->t,&ipt->current,data,length);
}
