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
	Admin-functions:
	admin.role.add.<event-path> , role
	admin.role.revoke.<event-path> , role
	admin.role.give.<event-path> , role
	admin.get.<objectname> , path.path
*/

#include "cle_instance.h"

static const char _allow_role_name[] = "admin.role.allow";
static const char _revoke_role_name[] = "admin.role.revoke";
static const char _give_role_name[] = "admin.role.give";
static const char _get_name[] = "admin.get";
static const char _list_object_name[] = "admin.list.object";
static const char _list_state_name[] = "admin.list.state";
static const char _list_prop_name[] = "admin.list.prop";

static cle_syshandler _allow_role;
static cle_syshandler _revoke_role;
static cle_syshandler _give_role;
static cle_syshandler _get;

static const char _illegal_argument[] = "admin:illegal argument";

struct _admin_role
{
	cdat eventmask;
	int eventmask_length;
	int role_length;
	char role[256];
};

static int role_get_arguments(event_handler* hdl, struct _admin_role* args, uint mask_cut)
{
	args->eventmask = hdl->eventdata->eventid + mask_cut;
	args->eventmask_length = hdl->eventdata->event_len - mask_cut;

	args->role_length = st_get(hdl->instance_tk,&hdl->top->pt,args->role,255);

	return (args->role_length > 1);
}

static void allow_next(event_handler* hdl)
{
	struct _admin_role args;
	if(role_get_arguments(hdl,&args,sizeof(_allow_role_name)))
	{
		cle_allow_role(hdl->instance_tk,hdl->instance,args.eventmask,args.eventmask_length,args.role,args.role_length);
		cle_stream_end(hdl);
	}
	else
		cle_stream_fail(hdl,_illegal_argument,sizeof(_illegal_argument));
}

static void revoke_next(event_handler* hdl)
{
	struct _admin_role args;
	if(role_get_arguments(hdl,&args,sizeof(_revoke_role_name)))
	{
		cle_revoke_role(hdl->instance_tk,hdl->instance,args.eventmask,args.eventmask_length,args.role,args.role_length);
		cle_stream_end(hdl);
	}
	else
		cle_stream_fail(hdl,_illegal_argument,sizeof(_illegal_argument));
}

static void give_next(event_handler* hdl)
{
	struct _admin_role args;
	if(role_get_arguments(hdl,&args,sizeof(_give_role_name)))
	{
		cle_give_role(hdl->instance_tk,hdl->instance,args.eventmask,args.eventmask_length,args.role,args.role_length);
		cle_stream_end(hdl);
	}
	else
		cle_stream_fail(hdl,_illegal_argument,sizeof(_illegal_argument));
}

static void _get_next(event_handler* hdl)
{
	st_ptr prop;
	cdat obname = hdl->eventdata->eventid + sizeof(_get_name);
	uint obname_length = hdl->eventdata->event_len - sizeof(_get_name);

	if(cle_get_property(hdl->instance_tk,hdl->instance,obname,obname_length,hdl->top->pt,&prop))
		cle_stream_fail(hdl,_illegal_argument,sizeof(_illegal_argument));
	else
	{
		hdl->response->start(hdl->respdata);
		hdl->response->submit(hdl->respdata,hdl->instance_tk,&prop);
		cle_stream_end(hdl);
	}
}

void admin_register_handlers(task* config_t, st_ptr* config_root)
{
	_allow_role = cle_create_simple_handler(0,allow_next,0,SYNC_REQUEST_HANDLER);
	cle_add_sys_handler(config_t,*config_root,_allow_role_name,sizeof(_allow_role_name),&_allow_role);

	_revoke_role = cle_create_simple_handler(0,revoke_next,0,SYNC_REQUEST_HANDLER);
	cle_add_sys_handler(config_t,*config_root,_revoke_role_name,sizeof(_revoke_role_name),&_revoke_role);

	_give_role = cle_create_simple_handler(0,give_next,0,SYNC_REQUEST_HANDLER);
	cle_add_sys_handler(config_t,*config_root,_give_role_name,sizeof(_give_role_name),&_give_role);

	_get = cle_create_simple_handler(0,_get_next,0,SYNC_REQUEST_HANDLER);
	cle_add_sys_handler(config_t,*config_root,_get_name,sizeof(_get_name),&_get);
}
