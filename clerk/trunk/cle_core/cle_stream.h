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
#ifndef __CLE_STREAM_H__
#define __CLE_STREAM_H__


#include "cle_clerk.h"
#include "cle_pagesource.h"
#include "cle_object.h"
/*
*	The main input-interface to the running system
*	Commands and external events are "pumped" in through this set of functions
*/
#define EVENT_MAX_LENGTH 512

/* event input functions */
typedef struct _ipt_internal _ipt;

_ipt* cle_start(st_ptr config_root, cdat eventid, uint event_len, cdat userid, uint userid_len, char* user_roles[],
				cle_pipe* response, void* responsedata, task* app_instance);

_ipt* cle_start2(task* parent, st_ptr config, st_ptr eventid, st_ptr userid, st_ptr user_roles, cle_pipe_inst response);

void cle_next(_ipt* inpt);
void cle_end(_ipt* inpt, cdat code, uint length);
void cle_pop(_ipt* inpt);
void cle_push(_ipt* inpt);
void cle_data(_ipt* inpt, cdat data, uint length);

// system-event-handlers

typedef struct sys_handler_data
{
	st_ptr config;
	cdat eventid;
	uint event_len;
	cdat userid;
	uint userid_len;
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
	sys_handler_data* eventdata;
	cle_syshandler* thehandler;
	void* handler_data;
	cle_pipe* response;
	void* respdata;
	ptr_list* top;
	ptr_list* free;
	cle_instance inst;
	st_ptr handler;
	st_ptr object;
	st_ptr root;
	cdat error;
	uint errlength;
};

/* event-handler exit-functions */
void cle_stream_fail(event_handler* hdl, cdat msg, uint msglen);
void cle_stream_end(event_handler* hdl);
void cle_stream_leave(event_handler* hdl);

// convenience-functions for implementing the cle_pipe-interface
void cle_standard_next_done(event_handler* hdl);
void cle_standard_pop(event_handler* hdl);
void cle_standard_push(event_handler* hdl);
uint cle_standard_data(event_handler* hdl, cdat data, uint length);
void cle_standard_submit(event_handler* hdl, task* t, st_ptr* st);

void cle_stream_submit(task* t, cle_pipe* recv, void* data, task* t_pt, st_ptr* pt);

cle_syshandler cle_create_simple_handler(void (*start)(void*),void (*next)(void*),void (*end)(void*,cdat,uint),enum handler_type);
/* setup system-level handler */
void cle_add_sys_handler(task* config_task, st_ptr config_root, cdat eventmask, uint mask_length, cle_syshandler* handler);

/* control role-access */
void cle_allow_role(task* app_instance, st_ptr app_root, cdat eventmask, uint mask_length, cdat role, uint role_length);
void cle_revoke_role(task* app_instance, st_ptr app_root, cdat eventmask, uint mask_length, cdat role, uint role_length);
// if access is granted - give (or add) this role to the user (for the rest of the request)
void cle_give_role(task* app_instance, st_ptr app_root, cdat eventmask, uint mask_length, cdat role, uint role_length);

// thread-subsystem hook
void cle_notify_start(event_handler* handler);
void cle_notify_next(event_handler* handler);
void cle_notify_end(event_handler* handler, cdat msg, uint msglength);

// hook-ref for the ignite-interpreter
extern cle_syshandler _runtime_handler;

extern cle_syshandler _object_stream;


// standard handlers
void dev_register_handlers(task* config_t, st_ptr* config_root);

void admin_register_handlers(task* config_t, st_ptr* config_root);

#endif
