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
	dev.new.<extend> , objectname (if extend == 'object' -> new root object)
	dev.eval.<objectname> , expr
	dev.make.expr.<objectname> , path.path , expr/method/ref/collection
	dev.make.property.<objectname> , path.path 
	dev.make.state.<objectname> , state [, entry-validation expr]
	dev.make.handler.sync.<objectname> , event, method/expr (handler) , state [,states]
	dev.make.handler.asyn.<objectname> , state, event, method/expr (handler)
	dev.make.handler.resp.<objectname> , state, event, method/expr (handler)
	dev.make.handler.reqs.<objectname> , state, event, method/expr (handler)

*/
#include "cle_instance.h"

static const char _new_extends_name[] = "dev\0new";
static const char _set_expr_name[] = "dev\0set\0expr";
static const char _set_property_name[] = "dev\0set\0property";
static const char _create_state_name[] = "dev\0create\0state";
static const char _set_handler_name_sync[] = "dev\0set\0handler\0sync";
static const char _set_handler_name_asyn[] = "dev\0set\0handler\0asyn";
static const char _set_handler_name_resp[] = "dev\0set\0handler\0resp";
static const char _set_handler_name_reqs[] = "dev\0set\0handler\0reqs";

static cle_syshandler _new_extends;
static cle_syshandler _set_expr;
static cle_syshandler _set_property;
static cle_syshandler _create_state;
static cle_syshandler _set_handler_sync;
static cle_syshandler _set_handler_asyn;
static cle_syshandler _set_handler_resp;
static cle_syshandler _set_handler_reqs;

static const char _illegal_argument[] = "dev:illegal argument";

static void new_extends_next(event_handler* hdl)
{
	cdat exname = hdl->eventdata->eventid + sizeof(_new_extends_name);
	uint exname_length = hdl->eventdata->event_len - sizeof(_new_extends_name);

	if(cle_new(hdl->instance_tk,hdl->instance,hdl->root,0,exname,exname_length))
		cle_stream_fail(hdl,_illegal_argument,sizeof(_illegal_argument));
	else
		cle_stream_end(hdl);
}

static void set_property_next(event_handler* hdl)
{
	cdat obname = hdl->eventdata->eventid + sizeof(_set_property_name);
	uint obname_length = hdl->eventdata->event_len - sizeof(_set_property_name);

	if(cle_set_property(hdl->instance_tk,hdl->instance,obname,obname_length,hdl->root))
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

static void _set_expr_next(event_handler* hdl)
{
	struct _dev_set* state = (struct _dev_set*)hdl->handler_data;

	// first hit?
	if(state == 0)
	{
		hdl->handler_data = tk_alloc(hdl->instance_tk,sizeof(struct _dev_set),0);
		state = (struct _dev_set*)hdl->handler_data;

		state->p1 = hdl->root;
		cle_standard_next_done(hdl);
	}
	else
	{
		cdat obname = hdl->eventdata->eventid + sizeof(_set_expr_name);
		uint obname_length = hdl->eventdata->event_len - sizeof(_set_expr_name);

		hdl->response->start(hdl->respdata);
		cle_set_expr(hdl->instance_tk,hdl->instance,obname,obname_length,state->p1,hdl->root,hdl->response,hdl->respdata);
		cle_stream_end(hdl);
	}
}

static void _set_handler_shared(event_handler* hdl, uint namesize, enum handler_type tp)
{
	struct _dev_set* state = (struct _dev_set*)hdl->handler_data;

	// first hit?
	if(state == 0)
	{
		hdl->handler_data = tk_alloc(hdl->instance_tk,sizeof(struct _dev_set),0);
		state = (struct _dev_set*)hdl->handler_data;

		state->p1 = hdl->root;
		state->hit = 0;
		cle_standard_next_done(hdl);
	}
	// 2. hit
	else if(state->hit == 0)
	{
		state->p2 = hdl->root;
		state->hit = 1;
		cle_standard_next_done(hdl);
	}
	// 3. hit
	else
	{
		cdat obname = hdl->eventdata->eventid + namesize;
		uint obname_length = hdl->eventdata->event_len - namesize;

		hdl->response->start(hdl->respdata);
		cle_set_handler(hdl->instance_tk,hdl->instance,obname,obname_length,state->p1,state->p2,hdl->root,hdl->response,hdl->respdata,tp);
		cle_stream_end(hdl);
	}
}

static void _set_handler_sync_next(event_handler* hdl)
{
	_set_handler_shared(hdl,sizeof(_set_handler_name_sync),SYNC_REQUEST_HANDLER);
}
static void _set_handler_asyn_next(event_handler* hdl)
{
	_set_handler_shared(hdl,sizeof(_set_handler_name_asyn),ASYNC_REQUEST_HANDLER);
}
static void _set_handler_resp_next(event_handler* hdl)
{
	_set_handler_shared(hdl,sizeof(_set_handler_name_resp),PIPELINE_RESPONSE);
}
static void _set_handler_reqs_next(event_handler* hdl)
{
	_set_handler_shared(hdl,sizeof(_set_handler_name_reqs),PIPELINE_REQUEST);
}

static void _create_state_next(event_handler* hdl)
{
	cdat obname = hdl->eventdata->eventid + sizeof(_create_state_name);
	uint obname_length = hdl->eventdata->event_len - sizeof(_create_state_name);

	if(cle_create_state(hdl->instance_tk,hdl->instance,obname,obname_length,hdl->root))
		cle_stream_fail(hdl,_illegal_argument,sizeof(_illegal_argument));
	else
		cle_stream_end(hdl);
}

void dev_register_handlers(task* config_t, st_ptr* config_root)
{
	_new_extends = cle_create_simple_handler(0,new_extends_next,0,SYNC_REQUEST_HANDLER);
	cle_add_sys_handler(config_t,*config_root,_new_extends_name,sizeof(_new_extends_name),&_new_extends);

	_set_property = cle_create_simple_handler(0,set_property_next,0,SYNC_REQUEST_HANDLER);
	cle_add_sys_handler(config_t,*config_root,_set_property_name,sizeof(_set_property_name),&_set_property);
	
	_set_expr = cle_create_simple_handler(0,_set_expr_next,0,SYNC_REQUEST_HANDLER);
	cle_add_sys_handler(config_t,*config_root,_set_expr_name,sizeof(_set_expr_name),&_set_expr);

	_create_state = cle_create_simple_handler(0,_create_state_next,0,SYNC_REQUEST_HANDLER);
	cle_add_sys_handler(config_t,*config_root,_create_state_name,sizeof(_create_state_name),&_create_state);

	_set_handler_sync = cle_create_simple_handler(0,_set_handler_sync_next,0,SYNC_REQUEST_HANDLER);
	cle_add_sys_handler(config_t,*config_root,_set_handler_name_sync,sizeof(_set_handler_name_sync),&_set_handler_sync);

	_set_handler_asyn = cle_create_simple_handler(0,_set_handler_asyn_next,0,SYNC_REQUEST_HANDLER);
	cle_add_sys_handler(config_t,*config_root,_set_handler_name_asyn,sizeof(_set_handler_name_asyn),&_set_handler_asyn);

	_set_handler_resp = cle_create_simple_handler(0,_set_handler_resp_next,0,SYNC_REQUEST_HANDLER);
	cle_add_sys_handler(config_t,*config_root,_set_handler_name_resp,sizeof(_set_handler_name_resp),&_set_handler_resp);

	_set_handler_reqs = cle_create_simple_handler(0,_set_handler_reqs_next,0,SYNC_REQUEST_HANDLER);
	cle_add_sys_handler(config_t,*config_root,_set_handler_name_reqs,sizeof(_set_handler_name_reqs),&_set_handler_reqs);
}
