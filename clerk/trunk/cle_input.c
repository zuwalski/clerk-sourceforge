/* 
   Copyright 2005-2006 Lars Szuwalski

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

typedef struct cle_input
{
	cle_output* response;
	void* outputdata;
	void* internaldata;
	cdat instance;
	uint inst_len;
	cdat appid;
	uint app_len;
	cdat eventid;
	uint evnt_len;
	cdat sessionid;
	uint ses_len;
} cle_input;

typedef struct cle_eventhandler
{
	void* (*setup)(cle_output*,void*,cdat,uint);
	int (*next)(void*,st_ptr*);
	int (*end)(void*,cdat,uint);
} cle_eventhandler;

/*
	Errors
*/
static const char pop_no_match[] = "pop no match";

// structs
static struct _handler_list
{
	struct _handler_list* next;
	cle_eventhandler* handler;
	void* data;
};

static struct _ptr_stack
{
	struct _ptr_stack* prev;
	st_ptr pt;
};

static struct _ipt_internal
{
	struct _handler_list* eventhandlers;	// NO -> list of data but direct call to runtime
	cle_syshandler* system;
	struct _ptr_stack* top;
	struct _ptr_stack* free;
	task* t;
	void* sysdata;
};

// default output-handler (used if none given)
static int _def_1(task* t) { return 0; }
static int _def_2(task* t, cdat c, uint u) { return 0; }

static cle_output _def_output_handler = {
	_def_1 /*start*/,
	_def_1 /*end*/,
	_def_1 /*pop*/,
	_def_1 /*push*/,
	_def_2  /*data*/,
	_def_1  /*next*/
};

// sys-handlers
// calls should be single threaded during setup/app.init
static cle_syshandler* _sys_handler = 0;

void cle_add_sys_handler(cle_syshandler* handler)
{
	handler->next = _sys_handler;
	_sys_handler = handler;
}

// input-functions
int cle_next(cle_input* inpt)
{
	struct _handler_list* handlers;
	struct _ipt_internal* ipt = (struct _ipt_internal*)inpt->internaldata;
	int rcode;
	if(ipt == 0)
		return -1;

	if(ipt->top->prev != 0)
		return -2;

	if(ipt->system)	// system-event
	{
		rcode = ipt->system->do_next(ipt->sysdata,&ipt->top->pt);
	}
	else
	{
		// send data-structure to all handlers
		rcode = 0;
		handlers = ipt->eventhandlers;
		while(handlers)
		{
			if(handlers->data)
			{
				rcode = handlers->handler->next(handlers->data,&ipt->top->pt);
				if(rcode) break;
				rcode = inpt->response->next(ipt->t);
				if(rcode) break;
			}

			handlers = handlers->next;
		}
	}

	// done processing .. clear and ready for next
	st_empty(ipt->t,&ipt->top->pt);
	return rcode;
}

int cle_start(cle_input* inpt)
{
	struct _handler_list* handlers;
	struct _ipt_internal* ipt;
	task* t;

	// validate
	if(inpt == 0)
		return -1;

	if(inpt->instance == 0 || inpt->inst_len == 0)
		return -2;

	if(inpt->appid == 0 || inpt->app_len == 0)
		return -3;

	if(inpt->eventid == 0 || inpt->evnt_len == 0)
		return -4;

	// if no response-handler. Just throw output away
	if(inpt->response == 0)
		inpt->response = &_def_output_handler;

	// first: Does anyone want this event? And are the sender allowed?
	// if not just throw it away

	// ...

	// someone can receive this .. setup for work
	t = tk_create_task(0);
	ipt = (struct _ipt_internal*)tk_alloc(t,sizeof(struct _ipt_internal));

	ipt->t = t;
	ipt->free = 0;

	ipt->eventhandlers = 0;
	ipt->system = 0;

	// check for system-events. Application-name starts with a dot
	if(inpt->appid[0] == '.')
	{
		cdat appid  = inpt->appid - 1;
		uint applen = inpt->app_len - 1;

		cle_syshandler* handler = _sys_handler;

		while(handler)
		{
			if(handler->id_length == inpt->app_len
				&& memcmp(appid,handler->handlerid,applen) == 0)
				break;

			handler = handler->next;
		}

		if(handler == 0)
			return -5;

		ipt->sysdata = handler->do_setup(inpt->response,inpt->outputdata,inpt->eventid,inpt->evnt_len);
		ipt->system = handler;
	}
	else
	{
	}

	// create blank toplevel element
	ipt->top = (struct _ptr_stack*)tk_alloc(t,sizeof(struct _ptr_stack));

	st_empty(t,&ipt->top->pt);
	ipt->top->prev = 0;

	// setup eventhandlers
	ipt->eventhandlers = 0;

	handlers = ipt->eventhandlers;
	while(handlers)
	{
		handlers->data = handlers->handler->setup(inpt->response,inpt->outputdata,inpt->eventid,inpt->evnt_len);
		handlers = handlers->next;
	}

	inpt->internaldata = (void*)ipt;
	return 0;
}

int cle_end(cle_input* inpt, cdat code, uint length)
{
	struct _ipt_internal* ipt = (struct _ipt_internal*)inpt->internaldata;
	int rcode = 0;
	if(ipt == 0)
		return -1;

	inpt->internaldata = 0;

	if(length == 0)
		rcode = cle_next(inpt);
	else	// reporting an error
	{}

	tk_drop_task(ipt->t);

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

	return rcode;
}

int cle_push(cle_input* inpt)
{
	struct _ipt_internal* ipt = (struct _ipt_internal*)inpt->internaldata;
	struct _ptr_stack* elm;
	if(ipt == 0)
		return -1;

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

	return 0;
}

int cle_pop(cle_input* inpt)
{
	struct _ipt_internal* ipt = (struct _ipt_internal*)inpt->internaldata;
	struct _ptr_stack* elm;
	if(ipt == 0)
		return -1;

	if(ipt->top->prev == 0)
		return cle_end(inpt,pop_no_match,sizeof(pop_no_match));

	elm = ipt->top;
	ipt->top = ipt->top->prev;

	elm->prev = ipt->free;
	ipt->free = elm;
	return 0;
}

int cle_data(cle_input* inpt, cdat data, uint length)
{
	struct _ipt_internal* ipt = (struct _ipt_internal*)inpt->internaldata;
	if(ipt == 0)
		return -1;

	return st_append(ipt->t,&ipt->top->pt,data,length);
}
