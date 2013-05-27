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
#include "cle_object.h"
/*
 *	The main input-interface to the running system
 *	Commands and external events are "pumped" in through this set of functions
 */

/* event input functions */

typedef struct handler_node _ipt;

_ipt* cle_open(task* parent, st_ptr config, st_ptr eventid, st_ptr userid, st_ptr user_roles, cle_pipe_inst response);

state cle_close(_ipt* ipt, cdat msg, uint len);
state cle_next(_ipt* ipt);
state cle_pop(_ipt* ipt);
state cle_push(_ipt* ipt);
state cle_data(_ipt* ipt, cdat data, uint len);

struct handler_env {
	cle_instance inst;
	st_ptr event_rest;
	st_ptr event;
	st_ptr roles;
	st_ptr user;
	void* data;
	oid id;
};

void cle_handler_get_env(const void* p, struct handler_env* env);

void cle_handler_set_data(void* p, void* data);
void* cle_handler_get_data(const void* p);

state resp_data(void* p, cdat c, uint l);
state resp_next(void* p);
state resp_push(void* p);
state resp_pop(void* p);
state resp_serialize(void* v, st_ptr pt);

uint cle_config_handler(task* t, st_ptr config, const cle_pipe* handler, enum handler_type type);
cle_pipe cle_basic_handler(state (*start)(void*), state (*next)(void* p, st_ptr ptr), state (*end)(void* p, cdat msg, uint len));

#endif
