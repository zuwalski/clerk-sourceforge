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
*	Commands and external events are "pumped" in through this set of functions
*/

#define HEAD_TYPE "\0T"
#define HEAD_APPS  "\0A"

#define HEAD_EVENT "\0e"
#define HEAD_IMPORT "\0i"
#define HEAD_EXTENDS "\0x"

// structs
struct _ptr_stack
{
	struct _ptr_stack* prev;
	st_ptr pt;
};

struct _ipt_internal
{
	struct _ptr_stack* top;
	struct _ptr_stack* free;

	cle_syshandler* system;
	sys_handler_data sys;

	st_ptr current;

	uint maxdepth;
	uint depth;
};

/* GLOBALS */
static task* _global_handler_task = 0;
static st_ptr _global_handler_rootptr;

static int _validate_event_name(cdat name, uint length)
{
	return 1;
}

/* initializers */
int cle_initialize_system(int argc, char *argv[])
{
	// setup system event-handlers
	_global_handler_task = tk_create_task(0,0);

	st_empty(_global_handler_task,&_global_handler_rootptr);

	return 0;
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
	st_ptr pt;
	task* t;
	_ipt* ipt;

	// valid eventid?
	if(!_validate_event_name(eventid,event_len))
		return 0;

	// validate user allowed to fire event

	// ipt setup
	t = tk_create_task(0,0);
	ipt = (_ipt*)tk_alloc(t,sizeof(_ipt));
	ipt->free = 0;
	ipt->system = 0;

	ipt->depth = ipt->maxdepth = 0;

	ipt->sys.data = 0;
	ipt->sys.next_call = 0;
	ipt->sys.t = t;

	/* get a root ptr */
	pt.pg = app_source->root_page(app_source_data);
	pt.key = sizeof(page);
	pt.offset = 0;
	ipt->sys.instance = pt;

	ipt->sys.response = response;
	ipt->sys.respdata = responsedata;

	// lookup system-eventhandler
	pt = _global_handler_rootptr;
	if(!st_move(_global_handler_task,&pt,eventid,event_len) &&
		!st_move(_global_handler_task,&pt,HEAD_EVENT,HEAD_SIZE))
	{
		if(st_get(_global_handler_task,&pt,(char*)&ipt->system,sizeof(cle_syshandler*)) == -1)
		{
		}
		else
			ipt->system = 0;
	}

	// lookup module-eventhandlers
	pt = ipt->sys.instance;

	if(!st_move(x,&pt,HEAD_EVENT,HEAD_SIZE) &&
		!st_move(x,&pt,eventid,event_len)
	{
	}

	// setup and ready all handlers
	if(ipt->system)
		ipt->system->do_setup(&ipt->sys);

	return ipt;
}

/*
_ipt* cle_start(cle_input* inpt, cle_output* response, void* responsedata)
{
	_ipt* ipt;
	task* t;

	// validate
	if(inpt == 0)
		return 0;

	if(inpt->eventid == 0 || inpt->evnt_len == 0)
		return 0;

	// setup for work
	t = tk_create_task(0);
	ipt = (_ipt*)tk_alloc(t,sizeof(_ipt));

	ipt->free = 0;
	ipt->system = 0;

	ipt->depth = ipt->maxdepth = 0;

	ipt->sys.data = 0;
	ipt->sys.instance = instance;
	ipt->sys.next_call = 0;
	ipt->sys.t = t;

	// if no response-handler. Just throw away output
	ipt->sys.response = (response != 0)? response : &_def_output_handler;
	ipt->sys.respdata = responsedata;

	// first: Does anyone want this event? And are the sender allowed?
	// if not just throw it away

	// check for system-events. Application-name 0-length
	if(inpt->app_len == 0)
	{
		cle_syshandler* handler = _sys_handler;

		while(handler)
		{
			if(handler->id_length == inpt->evnt_len
				&& memcmp(handler->handlerid,inpt->eventid,inpt->evnt_len) == 0)
				break;

			handler = handler->next;
		}

		if(handler == 0)
			return 0;

		if(handler->do_setup)
			handler->do_setup(&ipt->sys);

		ipt->system = handler;
	}
	// application event-handlers/filters
	else
	{
		st_ptr app = ipt->sys.instance;

		if(st_move(&app,HEAD_APPS,HEAD_SIZE))
			return 0;
		else if(st_move(&app,inpt->appid,inpt->app_len))
			return 0;
		else if(st_move(&app,HEAD_EVENT,HEAD_SIZE))
			return 0;
		else if(st_move(&app,inpt->eventid,inpt->evnt_len))
			return 0;
		else
		{
		}
	}

	// create blank toplevel element
	ipt->top = (struct _ptr_stack*)tk_alloc(t,sizeof(struct _ptr_stack));

	st_empty(t,&ipt->top->pt);
	ipt->top->prev = 0;

	ipt->current = ipt->top->pt;

	return ipt;
}
*/

int cle_end(_ipt* ipt, cdat code, uint length)
{
	task* t;
	int rcode = 0;
	if(ipt == 0)
		return -1;

	t = ipt->sys.t;

	// reporting an error
	if(length != 0 && code != 0)
	{}

	if(ipt->system)
	{
		if(ipt->system->do_end)
			rcode = ipt->system->do_end(&ipt->sys,code,length);
	}
	else
	{}

	tk_drop_task(t);

	return rcode;
}

int cle_push(_ipt* ipt)
{
	struct _ptr_stack* elm;
	if(ipt == 0)
		return -1;

	if(ipt->free)
	{
		elm = ipt->free;
		ipt->free = elm->prev;
	}
	else
		elm = (struct _ptr_stack*)tk_alloc(ipt->sys.t,sizeof(struct _ptr_stack));

	elm->pt = ipt->top->pt;

	elm->prev = ipt->top;
	ipt->top = elm;
	ipt->current = ipt->top->pt;

	ipt->depth++;
	if(ipt->depth > ipt->maxdepth) ipt->maxdepth = ipt->depth;

	return 0;
}

int cle_pop(_ipt* ipt)
{
	struct _ptr_stack* elm;
	if(ipt == 0)
		return -1;

	if(ipt->top->prev == 0)
		return -2;

	elm = ipt->top;
	ipt->top = ipt->top->prev;
	ipt->current = ipt->top->pt;

	elm->prev = ipt->free;
	ipt->free = elm;

	ipt->depth--;
	return 0;
}

int cle_data(_ipt* ipt, cdat data, uint length)
{
	if(ipt == 0)
		return -1;

	return -st_append(ipt->sys.t,&ipt->current,data,length);
}

int cle_next(_ipt* ipt)
{
	struct _handler_list* handlers;
	int rcode;
	if(ipt == 0)
		return -1;

	if(ipt->top->prev != 0)
		return -2;

	if(ipt->system)	// system-event
	{
		if(ipt->system->do_next)
			rcode = ipt->system->do_next(&ipt->sys,ipt->top->pt,ipt->maxdepth);
		else
			rcode = -3;		// operation doent use next
	}
	else
	{
		// send data-structure to all handlers
		rcode = 0;
	}

	ipt->sys.next_call++;
	ipt->depth = ipt->maxdepth = 0;

	// done processing .. clear and ready for next
	st_empty(ipt->sys.t,&ipt->top->pt);
	ipt->current = ipt->top->pt;

	return rcode;
}
