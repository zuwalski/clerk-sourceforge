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

/*
	Creating instances and managing content
*/

#ifndef __CLE_INSTANCE_H__
#define __CLE_INSTANCE_H__

#include "cle_clerk.h"
#include "cle_stream.h"

#define HEAD_SIZE 2
#define HEAD_EVENT "\0e"
#define HEAD_HANDLER "\0h"
#define HEAD_ROLES "\0r"
#define HEAD_OID "\0O"
#define HEAD_NAMES "\0o"
#define HEAD_EXTENDS "\0x"
#define HEAD_STATES "\0s"
#define HEAD_STATE_NAMES "\0z"

#define HEAD_METHOD "\0M"
#define HEAD_EXPR "\0E"
#define HEAD_INT "\0I"
#define HEAD_STR "\0S"
#define HEAD_REF "\0R"

/* create blank instance (destroys any content) */
void cle_format_instance(task* app_instance);

/* setup system-level handler */
void cle_add_sys_handler(task* config_task, st_ptr config_root, cdat eventmask, uint mask_length, cle_syshandler* handler);

/* setup module-level handler */
void cle_add_mod_handler(task* app_instance, st_ptr app_root, cdat eventmask, uint mask_length, struct mod_target* target);

/* control role-access */
void cle_allow_role(task* app_instance, st_ptr app_root, cdat eventmask, uint mask_length, cdat role, uint role_length);

void cle_revoke_role(task* app_instance, st_ptr app_root, cdat eventmask, uint mask_length, cdat role, uint role_length);

// if access is granted - give (or add) this role to the user (for the rest of the request)
void cle_give_role(task* app_instance, st_ptr app_root, cdat eventmask, uint mask_length, cdat role, uint role_length);

/* object-store */
void cle_new_noname(task* app_instance, st_ptr app_root, st_ptr* obj);
int cle_new_object(task* app_instance, st_ptr app_root, st_ptr name, st_ptr* obj);
int cle_new(task* app_instance, st_ptr app_root, cdat extends_name, uint exname_length, st_ptr name, st_ptr* obj);

int cle_goto_object(task* t, st_ptr* root, cdat name, uint name_length);

int cle_create_state(task* app_instance, st_ptr app_root, cdat object_name, uint object_length, st_ptr state);

int cle_set_value(task* app_instance, st_ptr app_root, cdat object_name, uint object_length, st_ptr path, st_ptr value);

int cle_set_expr(task* app_instance, st_ptr app_root, cdat object_name, uint object_length, st_ptr path, st_ptr expr, cle_pipe* response, void* data);

int cle_set_handler(task* app_instance, st_ptr app_root, cdat object_name, uint object_length, st_ptr state, st_ptr eventname, st_ptr meth, cle_pipe* response, void* data);

int cle_get_property(task* app_instance, st_ptr root, st_ptr* object, cdat propname, uint name_length);

#endif
