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

/*
*	The main input-interface to the running system
*	Events/messages are "pumped" in through the exported set of functions
*/

#define HEAD_TYPE "\0T"
#define HEAD_APPS  "\0A"
#define HEAD_USERS "\0u"
#define HEAD_EVENT "\0e"
#define HEAD_IMPORT "\0i"
#define HEAD_EXTENDS "\0x"

// error-messages
static char unknown_user[] = "unknown user";

#define _error(txt) ipt->sys.response->end(ipt->sys.respdata,txt,sizeof(txt))

// structs
struct _ptr_stack
{
	struct _ptr_stack* prev;
	st_ptr pt;
};

typedef struct _task_chain
{
	struct _task_chain* next;
	task* tk;
	ulong refs;
	uint depth;
}_task_chain;

enum run_state
{
	RUNNING,
	WAITING,
	FAILED
};

typedef struct _mod_handler
{
	struct _mod_handler* next;
	struct _task_chain* current_input;
	enum run_state run_state;
}_mod_handler;

struct _ipt_internal
{
	struct _ptr_stack* top;
	struct _ptr_stack* free;

	cle_syshandler* system;
	sys_handler_data sys;

	_mod_handler* handler_list;
	_task_chain* input_chain_last;

	task* t;

	cdat _error;
	uint _errlength;

	st_ptr current;

	uint maxdepth;
	uint depth;
};

/* GLOBALS */
static task* _global_handler_task = 0;
static st_ptr _global_handler_rootptr;

static int _validate_event_name(cdat name, uint length)
{
	int state = 0;
	while(length-- > 0)
	{
		switch(*name++)
		{
		}
	}
	return 1;
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
	st_ptr pt,userpt,eventpt;
	task* t;
	_ipt* ipt;
	_mod_handler* handler;

	// valid eventid?
	if(!_validate_event_name(eventid,event_len))
		return 0;

	// ipt setup - internal task
	t = tk_create_task(0,0);
	ipt = (_ipt*)tk_alloc(t,sizeof(_ipt));
	ipt->free = 0;
	ipt->system = 0;
	ipt->t = t;
	ipt->_errlength = 0;

	ipt->depth = ipt->maxdepth = 0;

	ipt->sys.data = 0;
	ipt->sys.next_call = 0;

	/* get a root ptr to instance-db */
	ipt->sys.instance_tk = tk_create_task(app_source,app_source_data);
	pt.pg = app_source->root_page(app_source_data);
	pt.key = sizeof(page);
	pt.offset = 0;
	ipt->sys.instance = pt;

	ipt->sys.response = response;
	ipt->sys.respdata = responsedata;

	// validate user allowed to fire event
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
		{
			st_ptr evptr = ipt->sys.instance;
			tk_ref(ipt->sys.instance_tk,evptr.pg);
			

			// settings for this event
			if(st_move(ipt->sys.instance_tk,&evptr,HEAD_EVENT,HEAD_SIZE) ||
				st_move(ipt->sys.instance_tk,&evptr,eventid,event_len))
			{
			}


			it_ptr roles;
			it_create(&roles,&pt);

			// match roles
			if(it_next(t,0,&roles))
			{
			}

			// iterate user-roles
			while(it_next(t,0,&roles))
			{
				roles.kdata;
				roles.ksize;
			}

			it_dispose(t,&roles);
		}
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

	// lookup module-eventhandlers
	pt = ipt->sys.instance;

	if(!st_move(t,&pt,HEAD_EVENT,HEAD_SIZE) &&
		!st_move(t,&pt,eventid,event_len))
	{
		it_ptr it;
		it_create(&it,&pt);

		// iterate instance-refs / event-handler-id
		while(it_next(t,&pt,&it))
		{
		}

		it_dispose(t,&it);
	}

	// run module-handler setups
	handler = ipt->handler_list;
	while(handler)
	{
		// call runtime to setup/start handlers

		// next handler
		handler = handler->next;
	}

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
	if(ipt == 0)
		return;

	if(ipt->top->prev != 0)
	{
		_error();
		return;
	}

	if(ipt->system && ipt->system->do_next)
		ipt->system->do_next(&ipt->sys,ipt->top->pt,ipt->maxdepth);

	ipt->sys.next_call++;
	ipt->depth = ipt->maxdepth = 0;

	// done processing .. clear and ready for next
	st_empty(ipt->t,&ipt->top->pt);
	ipt->current = ipt->top->pt;

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
	if(ipt == 0 || ipt->input_chain_last == 0)
		return;

	if(ipt->free)
	{
		elm = ipt->free;
		ipt->free = elm->prev;
	}
	else
		elm = (struct _ptr_stack*)tk_alloc(ipt->t,sizeof(struct _ptr_stack));

	elm->pt = ipt->top->pt;

	elm->prev = ipt->top;
	ipt->top = elm;
	ipt->current = ipt->top->pt;

	ipt->depth++;
	if(ipt->depth > ipt->maxdepth) ipt->maxdepth = ipt->depth;
}

void cle_pop(_ipt* ipt)
{
	struct _ptr_stack* elm;
	if(ipt == 0 || ipt->input_chain_last == 0)
		return;

	if(ipt->top->prev == 0)
	{
		_error("error");
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
	if(ipt == 0 || ipt->input_chain_last == 0)
		return;

	st_append(ipt->input_chain_last->tk,&ipt->current,data,length);
}
