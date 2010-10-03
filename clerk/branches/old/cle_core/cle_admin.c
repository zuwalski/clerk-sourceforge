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
	admin.set.state.<objectname> , state
*/

#include "cle_stream.h"

static const char _allow_role_name[] = "admin\0role\0allow";
static const char _revoke_role_name[] = "admin\0role\0revoke";
static const char _give_role_name[] = "admin\0role\0give";
static const char _get_name[] = "admin\0get";
static const char _set_state_name[] = "admin\0set\0state";
static const char _list_object_name[] = "admin\0list\0object";
static const char _list_state_name[] = "admin\0list\0state";
static const char _list_prop_name[] = "admin\0list\0prop";

static cle_syshandler _allow_role;
static cle_syshandler _revoke_role;
static cle_syshandler _give_role;
static cle_syshandler _get;

static const char _illegal_argument[] = "admin:illegal argument";

void admin_register_handlers(task* config_t, st_ptr* config_root)
{
}
