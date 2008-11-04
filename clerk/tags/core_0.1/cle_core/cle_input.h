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
#ifndef __CLE_INPUT_H__
#define __CLE_INPUT_H__

#include "cle_clerk.h"
#include "cle_pagesource.h"

/*
*	The main input-interface to the running system
*	Commands and external events are "pumped" in through this set of functions
*/

/* output interface begin */
typedef struct cle_output
{
	int (*start)(void*);
	int (*end)(void*,cdat,uint);
	int (*pop)(void*);
	int (*push)(void*);
	int (*data)(void*,cdat,uint);
	int (*next)(void*);
} cle_output;

/* output interface end */

/* initializer: call this once(!) before anything else */
void cle_initialize_system();

/* event input functions */
typedef struct _ipt_internal _ipt;

_ipt* cle_start(cdat eventid, uint event_len, cdat userid, uint userid_len, char* user_roles[],
				cle_output* response, void* responsedata, 
					cle_pagesource* app_source, cle_psrc_data app_source_data, 
						cle_pagesource* session_source, cle_psrc_data session_source_data);

void cle_data(_ipt* inpt, cdat data, uint length);
void cle_pop(_ipt* inpt);
void cle_push(_ipt* inpt);
void cle_next(_ipt* inpt);
void cle_end(_ipt* inpt, cdat code, uint length);

// system-event-handlers

enum handler_type
{
	SYNC_REQUEST_HANDLER = 0,
	ASYNC_REQUEST_HANDLER,
	PIPELINE_REQUEST,
	PIPELINE_RESPONSE
};

struct sys_event_id
{
	char handlertype;
	char appid[2];
	char objid[5];
};

typedef struct sys_handler_data
{
	ptr_list* input;
	task* mem_tk;
	task* instance_tk;
	st_ptr instance;
	uint next_call;
	cdat eventid;
	uint event_len;
	cdat userid;
	uint userid_len;
	cdat error;
	uint errlength;
}
sys_handler_data;

typedef struct event_handler event_handler;

typedef struct cle_syshandler
{
	struct cle_syshandler* next_handler;
	void (*do_setup)(sys_handler_data*,event_handler*);
	void (*do_next)(sys_handler_data*,event_handler*,uint);
	void (*do_end)(sys_handler_data*,event_handler*,cdat,uint);
	enum handler_type systype;
}
cle_syshandler;

struct event_handler
{
	struct event_handler* next;
	cle_syshandler* thehandler;
	void* handdata;
	cle_output* response;
	void* respdata;
	struct sys_event_id event_id;
};

/* single threaded calls only */
void cle_add_sys_handler(cdat eventmask, uint mask_length, cle_syshandler* handler);

// hook-handler for the core runtimesystem
extern cle_syshandler _runtime_handler;

#endif
