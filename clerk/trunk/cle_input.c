/* 
   Copyright 2005-2007 Lars Szuwalski

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/
#include "cle_input.h"

/*
*	The main input-interface to the running system
*	Commands and external events are "pumped" in through this set of functions
*/

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

	uint maxdepth;
	uint depth;
};

// default output-handler (used if none given)
static int _def_1(void* t) { return 0; }
static int _def_2(void* t, cdat c, uint u) { return 0; }

static cle_output _def_output_handler = {
	_def_1 /*start*/,
	_def_2 /*end*/,
	_def_1 /*pop*/,
	_def_1 /*push*/,
	_def_2  /*data*/,
	_def_1  /*next*/
};

// sys-handlers
// calls should be single threaded during setup/app.init
static cle_syshandler* _sys_handler = 0;

int cle_add_sys_handler(cle_syshandler* handler)
{
	handler->next = _sys_handler;
	_sys_handler = handler;

	return 0;
}

// input-functions

// TEST TEST TEST TEST
static st_ptr instance;
static int instance_setup = 1;

_ipt* cle_start(cle_input* inpt, cle_output* response, void* responsedata)
{
	_ipt* ipt;
	task* t;

	// validate
	if(inpt == 0)
		return 0;

	if(inpt->eventid == 0 || inpt->evnt_len == 0)
		return 0;

	// TEST TEST TEST TEST
	if(instance_setup)
	{
		instance_setup = 0;
		t = tk_create_task(0);
		st_empty(t,&instance);
	}

	// setup for work
	t = tk_create_task(0);
	ipt = (_ipt*)tk_malloc(sizeof(_ipt));

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
		if(!st_move(&app,HEAD_APPS,HEAD_SIZE))
		{
			if(!st_move(&app,inpt->appid,inpt->app_len))
			{
				if(!st_move(&app,HEAD_EVENT,HEAD_SIZE))
				{
					if(!st_move(&app,inpt->eventid,inpt->evnt_len))
					{
					}
				}
			}
		}
	}

	// create blank toplevel element
	ipt->top = (struct _ptr_stack*)tk_alloc(t,sizeof(struct _ptr_stack));

	st_empty(t,&ipt->top->pt);
	ipt->top->prev = 0;

	return ipt;
}

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

	while(ipt->top)
	{
		struct _ptr_stack* elm = ipt->top;
		ipt->top = ipt->top->prev;

		tk_mfree(elm);
	}

	while(ipt->free)
	{
		struct _ptr_stack* elm = ipt->free;
		ipt->free = ipt->free->prev;

		tk_mfree(elm);
	}

	tk_mfree(ipt);

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
		elm = (struct _ptr_stack*)tk_malloc(sizeof(struct _ptr_stack));

	elm->pt = ipt->top->pt;

	elm->prev = ipt->top;
	ipt->top = elm;

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

	elm->prev = ipt->free;
	ipt->free = elm;

	ipt->depth--;
	return 0;
}

int cle_data(_ipt* ipt, cdat data, uint length)
{
	if(ipt == 0)
		return -1;

	return -st_append(ipt->sys.t,&ipt->top->pt,data,length);
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
	return rcode;
}
