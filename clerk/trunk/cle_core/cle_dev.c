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
#include "cle_object.h"
#include "cle_stream.h"

static const char _new_extends_name[] = "dev\0new";
static const char _new_object_name[] = "dev\0new\0object";
static const char _set_expr_name[] = "dev\0set\0expr";
static const char _create_state_name[] = "dev\0create\0state";
static const char _set_handler_name_sync[] = "dev\0set\0handler\0sync";
static const char _set_handler_name_asyn[] = "dev\0set\0handler\0asyn";
static const char _set_handler_name_resp[] = "dev\0set\0handler\0resp";
static const char _set_handler_name_reqs[] = "dev\0set\0handler\0reqs";

static cle_syshandler _new_extends;
static cle_syshandler _new_object;
static cle_syshandler _set_expr;
static cle_syshandler _create_state;
static cle_syshandler _set_handler_sync;
static cle_syshandler _set_handler_asyn;
static cle_syshandler _set_handler_resp;
static cle_syshandler _set_handler_reqs;

static const char _illegal_argument[] = "dev:illegal argument";

static void new_extends_next(event_handler* hdl)
{
	st_ptr extends;
	
	if(cle_obj_from_event(hdl,sizeof(_new_extends_name),&extends) || cle_new(hdl->inst,hdl->root,extends,0))
		cle_stream_fail(hdl,_illegal_argument,sizeof(_illegal_argument));
	else
		cle_stream_end(hdl);
}

static void new_object_next(event_handler* hdl)
{
	st_ptr extends;
	extends.pg = 0;
	if(cle_new(hdl->inst,hdl->root,extends,0))
		cle_stream_fail(hdl,_illegal_argument,sizeof(_illegal_argument));
	else
		cle_stream_end(hdl);
}

static void _set_expr_next(event_handler* hdl)
{
	st_ptr* first = (st_ptr*)hdl->handler_data;

	// first hit?
	if(first == 0)
	{
		hdl->handler_data = tk_alloc(hdl->inst.t,sizeof(st_ptr),0);
		first = (st_ptr*)hdl->handler_data;

		*first = hdl->root;
		cle_standard_next_done(hdl);
	}
	else
	{
		st_ptr obj;

		if(cle_obj_from_event(hdl,sizeof(_set_expr_name),&obj)
			|| cle_create_expr(hdl->inst,obj,*first,hdl->root,hdl->response,hdl->respdata))
			cle_stream_fail(hdl,_illegal_argument,sizeof(_illegal_argument));
		else
			cle_stream_end(hdl);
	}
}

static void _set_handler_shared(event_handler* hdl, uint namelength, cdat msg, uint msglen, enum handler_type tp)
{
	while(msglen == 0)
	{
		ptr_list* states = ptr_list_reverse((ptr_list*)hdl->handler_data);
		st_ptr obj,eventname,expr;

		msg = _illegal_argument;
		msglen = sizeof(_illegal_argument);

		if(states == 0)
			break;

		eventname = states->pt;
		states = states->link;
		if(states == 0)
			break;

		expr = states->pt;
		states = states->link;
		if(states == 0)
			break;

		if(cle_obj_from_event(hdl,namelength,&obj)
			|| cle_create_handler(hdl->inst,obj,eventname,expr,states,hdl->response,hdl->respdata,tp))
			break;

		msg = 0; msglen = 0;
		break;
	}
	cle_stream_fail(hdl,msg,msglen);
}

static void _set_handler_sync_end(event_handler* hdl, cdat m, uint l)
{
	_set_handler_shared(hdl,sizeof(_set_handler_name_sync),m,l,SYNC_REQUEST_HANDLER);
}
static void _set_handler_asyn_end(event_handler* hdl, cdat m, uint l)
{
	_set_handler_shared(hdl,sizeof(_set_handler_name_asyn),m,l,ASYNC_REQUEST_HANDLER);
}
static void _set_handler_resp_end(event_handler* hdl, cdat m, uint l)
{
	_set_handler_shared(hdl,sizeof(_set_handler_name_resp),m,l,PIPELINE_RESPONSE);
}
static void _set_handler_reqs_end(event_handler* hdl, cdat m, uint l)
{
	_set_handler_shared(hdl,sizeof(_set_handler_name_reqs),m,l,PIPELINE_REQUEST);
}

static void _create_state_next(event_handler* hdl)
{
	st_ptr obj;

	if(cle_obj_from_event(hdl,sizeof(_set_expr_name),&obj)
		|| cle_create_state(hdl->inst,obj,hdl->root))
		cle_stream_fail(hdl,_illegal_argument,sizeof(_illegal_argument));
	else
		cle_stream_end(hdl);
}

void dev_register_handlers(task* config_t, st_ptr* config_root)
{
	_new_extends = cle_create_simple_handler(0,new_extends_next,0,SYNC_REQUEST_HANDLER);
	cle_add_sys_handler(config_t,*config_root,_new_extends_name,sizeof(_new_extends_name),&_new_extends);

	_new_object = cle_create_simple_handler(0,new_object_next,0,SYNC_REQUEST_HANDLER);
	cle_add_sys_handler(config_t,*config_root,_new_object_name,sizeof(_new_object_name),&_new_object);
	
	_set_expr = cle_create_simple_handler(0,_set_expr_next,0,SYNC_REQUEST_HANDLER);
	cle_add_sys_handler(config_t,*config_root,_set_expr_name,sizeof(_set_expr_name),&_set_expr);

	_create_state = cle_create_simple_handler(0,_create_state_next,0,SYNC_REQUEST_HANDLER);
	cle_add_sys_handler(config_t,*config_root,_create_state_name,sizeof(_create_state_name),&_create_state);
/* FIXME
	_set_handler_sync = cle_create_simple_handler(0,cle_collect_params_next,_set_handler_sync_end,SYNC_REQUEST_HANDLER);
	cle_add_sys_handler(config_t,*config_root,_set_handler_name_sync,sizeof(_set_handler_name_sync),&_set_handler_sync);

	_set_handler_asyn = cle_create_simple_handler(0,cle_collect_params_next,_set_handler_asyn_end,SYNC_REQUEST_HANDLER);
	cle_add_sys_handler(config_t,*config_root,_set_handler_name_asyn,sizeof(_set_handler_name_asyn),&_set_handler_asyn);

	_set_handler_resp = cle_create_simple_handler(0,cle_collect_params_next,_set_handler_resp_end,SYNC_REQUEST_HANDLER);
	cle_add_sys_handler(config_t,*config_root,_set_handler_name_resp,sizeof(_set_handler_name_resp),&_set_handler_resp);

	_set_handler_reqs = cle_create_simple_handler(0,cle_collect_params_next,_set_handler_reqs_end,SYNC_REQUEST_HANDLER);
	cle_add_sys_handler(config_t,*config_root,_set_handler_name_reqs,sizeof(_set_handler_name_reqs),&_set_handler_reqs);
	*/
}
