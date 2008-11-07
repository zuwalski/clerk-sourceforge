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
typedef struct cle_pipe
{
	int (*start)(void*);
	int (*next)(void*);
	int (*end)(void*,cdat,uint);
	int (*pop)(void*);
	int (*push)(void*);
	int (*data)(void*,cdat,uint);
	int (*submit)(void*,st_ptr*);
} cle_pipe;

/* output interface end */

/* event input functions */
typedef struct _ipt_internal _ipt;

_ipt* cle_start(st_ptr config_root, cdat eventid, uint event_len, cdat userid, uint userid_len, char* user_roles[],
				cle_pipe* response, void* responsedata, task* app_instance);

void cle_next(_ipt* inpt);
void cle_end(_ipt* inpt, cdat code, uint length);
void cle_pop(_ipt* inpt);
void cle_push(_ipt* inpt);
void cle_data(_ipt* inpt, cdat data, uint length);
void cle_submit(_ipt* inpt, st_ptr* root);

// system-event-handlers

enum handler_type
{
	SYNC_REQUEST_HANDLER = 0,
	ASYNC_REQUEST_HANDLER,
	PIPELINE_REQUEST,
	PIPELINE_RESPONSE
};

struct mod_target
{
	char handlertype;
	char appid[2];
	char objid[5];
};

typedef struct sys_handler_data
{
	st_ptr config;
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
	cle_pipe input;
	enum handler_type systype;
}
cle_syshandler;

struct event_handler
{
	struct event_handler* next;
	cle_syshandler* thehandler;
	sys_handler_data* eventdata;
	void* handler_data;
	cle_pipe* response;
	void* respdata;
	ptr_list* top;
	ptr_list* free;
	task* instance_tk;
	st_ptr instance;
	struct mod_target target;
};

/* setup system-level handler */
void cle_add_sys_handler(task* config_task, st_ptr config_root, cdat eventmask, uint mask_length, cle_syshandler* handler);

/* setup module-level handler */
void cle_add_mod_handler(task* app_instance, cdat eventmask, uint mask_length, struct mod_target* target);

/* control role-access */
void cle_allow_role(task* app_instance, cdat eventmask, uint mask_length, cdat role, uint role_length);

void cle_revoke_role(task* app_instance, cdat eventmask, uint mask_length, cdat role, uint role_length);

void cle_format_instance(task* app_instance);

// convenience-functions for implementing the cle_pipe-interface
int cle_standard_pop(event_handler* hdl);
int cle_standard_push(event_handler* hdl);
int cle_standard_data(event_handler* hdl, cdat data, uint length);
int cle_standard_submit(event_handler* hdl, st_ptr* st);

// thread-subsystem hook
void cle_notify_start(event_handler* handler);
void cle_notify_next(event_handler* handler, ptr_list* nxtelement);
void cle_notify_end(event_handler* handler, cdat msg, uint msglength);

// hook-ref for the ignite-interpreter
extern cle_syshandler _runtime_handler;
#endif
