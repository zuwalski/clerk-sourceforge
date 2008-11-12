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
	dev.new.<extend-objectname> , objectname
	dev.set.val.<objectname> , path.path , value
	dev.set.expr.<objectname> , path.path , expr
	dev.set.state.<objectname> , state
	dev.set.handler.<objectname> , state, event, handler

*/
#include "cle_instance.h"

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

static const char _illegal_argument[] = "dev:illegal argument";

static void new_object_next(event_handler* hdl)
{
	if(cle_new_object(hdl->instance_tk,hdl->instance,hdl->top->pt,0) < 2)
		cle_stream_fail(hdl,_illegal_argument,sizeof(_illegal_argument));
	else
		cle_stream_end(hdl);
}

static void new_extends_name_next(event_handler* hdl)
{
	cdat exname = hdl->eventdata->eventid + sizeof(_new_extends_name);
	uint exname_length = hdl->eventdata->event_len - sizeof(_new_extends_name);

	if(cle_new(hdl->instance_tk,hdl->instance,exname,exname_length,hdl->top->pt,0) < 2)
		cle_stream_fail(hdl,_illegal_argument,sizeof(_illegal_argument));
	else
		cle_stream_end(hdl);
}


void dev_register_handlers(task* config_t, st_ptr* config_root)
{
	_new_object = cle_create_simple_handler(0,new_object_next,0,SYNC_REQUEST_HANDLER);
	cle_add_sys_handler(config_t,*config_root,_new_object_name,sizeof(_new_object_name),&_new_object);

	_new_extends = cle_create_simple_handler(0,new_extends_name_next,0,SYNC_REQUEST_HANDLER);
	cle_add_sys_handler(config_t,*config_root,_new_extends_name,sizeof(_new_extends_name),&_new_extends);
}
