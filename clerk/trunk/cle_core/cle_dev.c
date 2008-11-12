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
	Dev-functions:
	dev.new.object , objectname
	dev.new [.<extend-objectname>] , objectname
	dev.set.<val|expr>.<objectname> , path.path , value	'if expr -> value is compiled else raw-copy

*/
#include "cle_stream.h"

static const char _new_object_name[] = "dev.new.object";
static const char _new_extends_name[] = "dev.new";
static const char _set_val_name[] = "dev.set.val";
static const char _set_expr_name[] = "dev.set.expr";
static const char _set_state_name[] = "dev.set.state";
static const char _set_handler_name[] = "dev.set.handler";

static cle_syshandler _new_object;
static cle_syshandler _new_extends;
static cle_syshandler _set_val;
static cle_syshandler _set_expr;
static cle_syshandler _set_state;
static cle_syshandler _set_handler;


void dev_register_handlers(task* config_t, st_ptr* config_root)
{
	//_allow_role = cle_create_simple_handler(0,allow_next,0,SYNC_REQUEST_HANDLER);
	//_revoke_role = cle_create_simple_handler(0,revoke_next,0,SYNC_REQUEST_HANDLER);
	//_give_role = cle_create_simple_handler(0,give_next,0,SYNC_REQUEST_HANDLER);

	//cle_add_sys_handler(config_t,*config_root,_allow_role_name,sizeof(_allow_role_name),&_allow_role);
	//cle_add_sys_handler(config_t,*config_root,_revoke_role_name,sizeof(_revoke_role_name),&_revoke_role);
	//cle_add_sys_handler(config_t,*config_root,_give_role_name,sizeof(_give_role_name),&_give_role);
}
