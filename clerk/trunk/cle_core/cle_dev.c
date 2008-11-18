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
	dev.set.expr.<objectname> , path.path , expr/method/ref
	dev.set.state.<objectname> , state
	dev.set.handler.<objectname> , state, event, handler (-method)
	dev.get.<objectname> , path.path

*/
#include "cle_instance.h"

static const char _new_object_name[] = "dev.new.object";
static const char _new_extends_name[] = "dev.new";
static const char _set_val_name[] = "dev.set.val";
static const char _set_expr_name[] = "dev.set.expr";
static const char _create_state_name[] = "dev.create.state";
static const char _set_handler_name[] = "dev.set.handler";
static const char _get_name[] = "dev.get";
static const char _list_object_name[] = "dev.list.object";
static const char _list_state_name[] = "dev.list.state";
static const char _list_prop_name[] = "dev.list.prop";

static cle_syshandler _new_object;
static cle_syshandler _new_extends;
static cle_syshandler _set_val;
static cle_syshandler _set_expr;
static cle_syshandler _create_state;
static cle_syshandler _set_handler;
static cle_syshandler _get;

static const char _illegal_argument[] = "dev:illegal argument";

static void new_object_next(event_handler* hdl)
{
	if(cle_new_object(hdl->instance_tk,hdl->instance,hdl->top->pt,0))
		cle_stream_fail(hdl,_illegal_argument,sizeof(_illegal_argument));
	else
		cle_stream_end(hdl);
}

static void new_extends_next(event_handler* hdl)
{
	cdat exname = hdl->eventdata->eventid + sizeof(_new_extends_name);
	uint exname_length = hdl->eventdata->event_len - sizeof(_new_extends_name);

	if(cle_new(hdl->instance_tk,hdl->instance,exname,exname_length,hdl->top->pt,0))
		cle_stream_fail(hdl,_illegal_argument,sizeof(_illegal_argument));
	else
		cle_stream_end(hdl);
}

struct _dev_set
{
	st_ptr p1;
	st_ptr p2;
	uint hit;
};

static void _set_val_next(event_handler* hdl)
{
	struct _dev_set* state = (struct _dev_set*)hdl->handler_data;

	// first hit?
	if(state == 0)
	{
		hdl->handler_data = tk_alloc(hdl->instance_tk,sizeof(struct _dev_set));
		state = (struct _dev_set*)hdl->handler_data;

		state->p1= hdl->top->pt;
	}
	else
	{
		cdat obname = hdl->eventdata->eventid + sizeof(_set_val_name);
		uint obname_length = hdl->eventdata->event_len - sizeof(_set_val_name);

		cle_set_value(hdl->instance_tk,hdl->instance,obname,obname_length,state->p1,hdl->top->pt);
		cle_stream_end(hdl);
	}
}

static void _set_expr_next(event_handler* hdl)
{
	struct _dev_set* state = (struct _dev_set*)hdl->handler_data;

	// first hit?
	if(state == 0)
	{
		hdl->handler_data = tk_alloc(hdl->instance_tk,sizeof(struct _dev_set));
		state = (struct _dev_set*)hdl->handler_data;

		state->p1 = hdl->top->pt;
	}
	else
	{
		cdat obname = hdl->eventdata->eventid + sizeof(_set_expr_name);
		uint obname_length = hdl->eventdata->event_len - sizeof(_set_expr_name);

		cle_set_expr(hdl->instance_tk,hdl->instance,obname,obname_length,state->p1,hdl->top->pt,hdl->response,hdl->respdata);
		cle_stream_end(hdl);
	}
}

static void _set_handler_next(event_handler* hdl)
{
	struct _dev_set* state = (struct _dev_set*)hdl->handler_data;

	// first hit?
	if(state == 0)
	{
		hdl->handler_data = tk_alloc(hdl->instance_tk,sizeof(struct _dev_set));
		state = (struct _dev_set*)hdl->handler_data;

		state->p1 = hdl->top->pt;
		state->hit = 0;
	}
	// 2. hit
	else if(state->hit == 0)
	{
		state->p2 = hdl->top->pt;
		state->hit = 1;
	}
	// 3. hit
	else
	{
		cdat obname = hdl->eventdata->eventid + sizeof(_set_handler_name);
		uint obname_length = hdl->eventdata->event_len - sizeof(_set_handler_name);

		cle_set_handler(hdl->instance_tk,hdl->instance,obname,obname_length,state->p1,state->p2,hdl->top->pt,hdl->response,hdl->respdata);
		cle_stream_end(hdl);
	}
}

static void _create_state_next(event_handler* hdl)
{
	cdat obname = hdl->eventdata->eventid + sizeof(_create_state_name);
	uint obname_length = hdl->eventdata->event_len - sizeof(_create_state_name);

	if(cle_create_state(hdl->instance_tk,hdl->instance,obname,obname_length,hdl->top->pt))
		cle_stream_fail(hdl,_illegal_argument,sizeof(_illegal_argument));
	else
		cle_stream_end(hdl);
}

static void _get_next(event_handler* hdl)
{
	st_ptr obj = hdl->instance;
	cdat obname = hdl->eventdata->eventid + sizeof(_get_name);
	uint obname_length = hdl->eventdata->event_len - sizeof(_get_name);

	if(cle_goto_object(hdl->instance_tk,&obj,obname,obname_length))
		cle_stream_fail(hdl,_illegal_argument,sizeof(_illegal_argument));
	else
	{
		char buffer[200];
		int len = st_get(hdl->instance_tk,&hdl->top->pt,buffer,sizeof(buffer));

		if(len <= 0)
			cle_stream_fail(hdl,_illegal_argument,sizeof(_illegal_argument));
		else
		{
			st_ptr orig_obj = obj;
			int i;
			for(i = 0; i < len; i++)
			{
				if(buffer[i] == 0)
					break;
			}

			cle_get_property(hdl->instance_tk,hdl->instance,&obj,buffer,i);
		}
	}
}

void dev_register_handlers(task* config_t, st_ptr* config_root)
{
	_new_object = cle_create_simple_handler(0,new_object_next,0,SYNC_REQUEST_HANDLER);
	cle_add_sys_handler(config_t,*config_root,_new_object_name,sizeof(_new_object_name),&_new_object);

	_new_extends = cle_create_simple_handler(0,new_extends_next,0,SYNC_REQUEST_HANDLER);
	cle_add_sys_handler(config_t,*config_root,_new_extends_name,sizeof(_new_extends_name),&_new_extends);

	_set_val = cle_create_simple_handler(0,_set_val_next,0,SYNC_REQUEST_HANDLER);
	cle_add_sys_handler(config_t,*config_root,_set_val_name,sizeof(_set_val_name),&_set_val);

	_set_expr = cle_create_simple_handler(0,_set_expr_next,0,SYNC_REQUEST_HANDLER);
	cle_add_sys_handler(config_t,*config_root,_set_expr_name,sizeof(_set_expr_name),&_set_expr);

	_create_state = cle_create_simple_handler(0,_create_state_next,0,SYNC_REQUEST_HANDLER);
	cle_add_sys_handler(config_t,*config_root,_create_state_name,sizeof(_create_state_name),&_create_state);

	_set_handler = cle_create_simple_handler(0,_set_handler_next,0,SYNC_REQUEST_HANDLER);
	cle_add_sys_handler(config_t,*config_root,_set_handler_name,sizeof(_set_handler_name),&_set_handler);

	_get = cle_create_simple_handler(0,_get_next,0,SYNC_REQUEST_HANDLER);
	cle_add_sys_handler(config_t,*config_root,_get_name,sizeof(_get_name),&_get);
}
