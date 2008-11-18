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
#include "cle_instance.h"
#include "cle_compile.h"

static const char start_state[] = "start";

// helper-functions
static void _dot_to_null(char* buffer, int size)
{
	while(size-- > 0)
	{
		if(buffer[size] == '.')
			buffer[size] = 0;
	}
}

static int _copy_insert(task* t, st_ptr* to, st_ptr from)
{
	char buffer[200];
	int total = 0;

	while(1)
	{
		int length = st_get(t,&from,buffer,sizeof(buffer));

		if(length == 0)
			break;

		if(length == -2)
		{
			total += sizeof(buffer);
			st_insert(t,to,buffer,sizeof(buffer));
		}
		else
		{
			if(length < 0)
				length = sizeof(buffer);
			total += length;
			st_insert(t,to,buffer,length);
			break;
		}
	}

	return total;
}

static int _copy_move(task* t, st_ptr* to, st_ptr from)
{
	char buffer[200];

	while(1)
	{
		int length = st_get(t,&from,buffer,sizeof(buffer));

		if(length == 0)
			return 0;

		if(length == -2)
		{
			_dot_to_null(buffer,sizeof(buffer));
			if(st_move(t,to,buffer,sizeof(buffer)) != 0)
				return 1;
		}
		else
		{
			if(length < 0)
				length = sizeof(buffer);
			_dot_to_null(buffer,length);			
			return (st_move(t,to,buffer,length) != 0);
		}
	}
}

/****************************************************
	implementations
*/

void cle_format_instance(task* app_instance)
{
	st_ptr root,tmp;
	tk_root_ptr(app_instance,&root);

	// all clear
	st_delete(app_instance,&root,0,0);

	// insert event-hook
	tmp = root;
	st_insert(app_instance,&tmp,HEAD_EVENT,HEAD_SIZE);

	tmp = root;
	st_insert(app_instance,&tmp,HEAD_OID,HEAD_SIZE);

	tmp = root;
	st_insert(app_instance,&tmp,HEAD_NAMES,HEAD_SIZE);
}

/* system event-handler setup */
void cle_add_sys_handler(task* config_task, st_ptr config_root, cdat eventmask, uint mask_length, cle_syshandler* handler)
{
	cle_syshandler* exsisting;

	st_insert(config_task,&config_root,eventmask,mask_length);

	st_insert(config_task,&config_root,HEAD_HANDLER,HEAD_SIZE);

	if(st_get(config_task,&config_root,(char*)&exsisting,sizeof(cle_syshandler*)) == -1)
		// prepend to list
		handler->next_handler = exsisting;
	else
		handler->next_handler = 0;

	st_update(config_task,&config_root,(cdat)&handler,sizeof(cle_syshandler*));
}

/* setup module-level handler */
void cle_add_mod_handler(task* app_instance, st_ptr app_root, cdat eventmask, uint mask_length, struct mod_target* target)
{}

/* control role-access */
void cle_allow_role(task* app_instance, st_ptr root, cdat eventmask, uint mask_length, cdat role, uint role_length)
{
	// max length!
	if(role_length > 255)
		return;

	st_insert(app_instance,&root,HEAD_EVENT,HEAD_SIZE);

	st_insert(app_instance,&root,eventmask,mask_length);

	st_insert(app_instance,&root,HEAD_ROLES,HEAD_SIZE);

	st_insert(app_instance,&root,role,role_length);
}

void cle_revoke_role(task* app_instance, st_ptr app_root, cdat eventmask, uint mask_length, cdat role, uint role_length)
{}

void cle_give_role(task* app_instance, st_ptr app_root, cdat eventmask, uint mask_length, cdat role, uint role_length)
{}

/* object store */

void cle_new_noname(task* app_instance, st_ptr app_root, st_ptr* obj)
{
	it_ptr it;
	st_ptr pt;

	st_insert(app_instance,&app_root,HEAD_OID,HEAD_SIZE);

	it_create(app_instance,&it,&app_root);

	it_new(app_instance,&it,obj);

	st_insert(app_instance,&pt,it.kdata,it.kused);

	// reflect oid in object
	pt = *obj;
	st_insert(app_instance,&pt,HEAD_OID,HEAD_SIZE);

	st_insert(app_instance,&pt,it.kdata,it.kused);

	it_dispose(app_instance,&it);
}

int cle_new_object(task* app_instance, st_ptr app_root, st_ptr name, st_ptr* obj)
{
	it_ptr it;
	st_ptr newobj,pt = app_root;
	int len;

	st_insert(app_instance,&pt,HEAD_NAMES,HEAD_SIZE);

	len = _copy_insert(app_instance,&pt,name);

	// must be unique
	if(len < 2 || st_is_empty(&pt) == 0)
		return 1;

	st_insert(app_instance,&app_root,HEAD_OID,HEAD_SIZE);

	it_create(app_instance,&it,&app_root);

	it_new(app_instance,&it,&newobj);

	st_insert(app_instance,&pt,it.kdata,it.kused);

	// reflect name in object
	pt = newobj;
	st_insert(app_instance,&pt,HEAD_NAMES,HEAD_SIZE);

	_copy_insert(app_instance,&pt,name);

	// reflect oid in object
	pt = newobj;
	st_insert(app_instance,&pt,HEAD_OID,HEAD_SIZE);

	st_insert(app_instance,&pt,it.kdata,it.kused);

	it_dispose(app_instance,&it);

	if(obj != 0)
		*obj = newobj;
	return 0;
}

int cle_new(task* app_instance, st_ptr app_root, cdat extends_name, uint exname_length, st_ptr name, st_ptr* obj)
{
	st_ptr newobj,pt = app_root;

	if(st_move(app_instance,&pt,HEAD_NAMES,HEAD_SIZE) != 0)
		return 1;
	
	if(st_move(app_instance,&pt,extends_name,exname_length) != 0)
		return 1;

	if(cle_new_object(app_instance,app_root,name,&newobj))
		return 1;

	st_insert(app_instance,&newobj,HEAD_EXTENDS,HEAD_SIZE);

	// copy id
	_copy_insert(app_instance,&newobj,pt);

	if(obj != 0)
		*obj = newobj;
	return 0;
}

int cle_goto_object(task* t, st_ptr* root, cdat name, uint name_length)
{
	st_ptr pt = *root;
	if(st_move(t,&pt,HEAD_NAMES,HEAD_SIZE) != 0)
		return 1;
	
	if(st_move(t,&pt,name,name_length) != 0)
		return 1;

	if(st_move(t,root,HEAD_OID,HEAD_SIZE) != 0)
		return 1;

	return _copy_move(t,root,pt);
}

int cle_set_value(task* app_instance, st_ptr root, cdat object_name, uint object_length, st_ptr path, st_ptr value)
{
	if(cle_goto_object(app_instance,&root,object_name,object_length))
		return 1;

	_copy_insert(app_instance,&root,path);

	if(st_is_empty(&value) == 0)
		st_link(app_instance,&root,app_instance,&value);

	return 0;
}

static void _record_source_and_path(task* app_instance, st_ptr dest, st_ptr path, st_ptr expr)
{
	st_ptr pt = dest;
	// insert path
	if(pt.pg != 0)
	{
		st_insert(app_instance,&dest,"p",2);
		_copy_insert(app_instance,&dest,path);
	}

	dest = pt;
	// insert source
	st_insert(app_instance,&dest,"s",2);
	_copy_insert(app_instance,&dest,expr);
}

int cle_set_expr(task* app_instance, st_ptr app_root, cdat object_name, uint object_length, st_ptr path, st_ptr expr, cle_pipe* response, void* data)
{
	st_ptr pt0,pt = app_root;
	int c;
	if(cle_goto_object(app_instance,&pt,object_name,object_length))
		return 1;

	_copy_insert(app_instance,&pt,path);

	// clear old
	st_delete(app_instance,&pt,0,0);

	c = st_scan(app_instance,&expr);
	while(c >= 0)
	{
		switch(c)
		{
		case ':':	// ref to named object
			st_insert(app_instance,&pt,HEAD_REF,HEAD_SIZE);

			pt0 = app_root;
			if(st_move(app_instance,&pt0,HEAD_NAMES,HEAD_SIZE) != 0)
				return 1;
			if(_copy_move(app_instance,&pt0,expr))
				return 1;

			// copy oid
			_copy_insert(app_instance,&pt,pt0);
			return 0;
		case '=':	// expr
			st_insert(app_instance,&pt,HEAD_EXPR,HEAD_SIZE);

			_record_source_and_path(app_instance,pt,path,expr);
			// call compiler
			return cmp_expr(app_instance,&pt,&expr,response,data);
		case '(':	// method
			st_insert(app_instance,&pt,HEAD_METHOD,HEAD_SIZE);

			_record_source_and_path(app_instance,pt,path,expr);
			// call compiler
			return cmp_method(app_instance,&pt,&expr,response,data);
		case ' ':
		case '\t':
		case '\r':
		case '\n':
			break;
		default:
			return 1;
		}

		c = st_scan(app_instance,&expr);
	}

	return 1;
}

int cle_create_state(task* app_instance, st_ptr root, cdat object_name, uint object_length, st_ptr state)
{
	it_ptr it;
	st_ptr pt;
	int len;
	char buffer[sizeof(start_state)];

	if(cle_goto_object(app_instance,&root,object_name,object_length))
		return 1;

	// reserved state-name
	pt = root;
	if(st_get(app_instance,&pt,buffer,sizeof(buffer)) == -1 && memcmp(start_state,buffer,sizeof(buffer)) == 0)
		return 1;

	pt = root;
	st_insert(app_instance,&pt,HEAD_STATE_NAMES,HEAD_SIZE);

	// copy state-name
	len = _copy_insert(app_instance,&pt,state);

	// must be unique
	if(len < 2 || st_is_empty(&pt) == 0)
		return 1;

	// go to state-store
	st_insert(app_instance,&root,HEAD_STATES,HEAD_SIZE);

	it_create(app_instance,&it,&root);

	it_new(app_instance,&it,0);

	// link name to new id
	st_insert(app_instance,&pt,it.kdata,it.kused);

	it_dispose(app_instance,&it);
	return 0;
}

static st_ptr _blank = {0,0,0};

int cle_set_handler(task* app_instance, st_ptr root, cdat object_name, uint object_length, st_ptr state, st_ptr eventname, st_ptr expr, cle_pipe* response, void* data)
{
	st_ptr pt;
	int len,c;
	char buffer[sizeof(start_state)];

	if(cle_goto_object(app_instance,&root,object_name,object_length))
		return 1;

	pt = root;
	if(st_get(app_instance,&pt,buffer,sizeof(buffer)) != -1 || memcmp(start_state,buffer,sizeof(buffer)) != 0)
	{
		// find state
		pt = root;
		if(st_move(app_instance,&pt,HEAD_STATE_NAMES,HEAD_SIZE) != 0)
			return 1;

		// to name
		if(_copy_move(app_instance,&pt,state) != 0)
			return 1;

		if(st_move(app_instance,&root,HEAD_STATES,HEAD_SIZE) != 0)
			return 1;

		// to id
		if(_copy_move(app_instance,&root,pt) != 0)
			return 1;
	}
	else	// start-state
		st_insert(app_instance,&root,"\1",2);

	// insert event-name
	len = _copy_insert(app_instance,&root,eventname);

	if(len < 2)
		return 1;

	c = st_scan(app_instance,&expr);
	while(c >= 0)
	{
		switch(c)
		{
		case '=':	// expr
			st_insert(app_instance,&root,HEAD_EXPR,HEAD_SIZE);
			st_delete(app_instance,&root,0,0);
			// insert source
			_record_source_and_path(app_instance,pt,_blank,expr);
			// call compiler
			return cmp_expr(app_instance,&root,&expr,response,data);
		case '(':	// method
			st_insert(app_instance,&root,HEAD_METHOD,HEAD_SIZE);
			st_delete(app_instance,&root,0,0);
			// insert source
			_record_source_and_path(app_instance,pt,_blank,expr);
			// call compiler
			return cmp_method(app_instance,&root,&expr,response,data);
		case ' ':
		case '\t':
		case '\r':
		case '\n':
			break;
		default:
			return 1;
		}

		c = st_scan(app_instance,&expr);
	}

	return 0;
}

int cle_get_property(task* app_instance, st_ptr root, st_ptr* object, cdat propname, uint name_length)
{
	int super_prop = 0;
	if(st_move(app_instance,&root,HEAD_OID,HEAD_SIZE) != 0)
		return -1;

	// seach inheritance-chain
	while(1)
	{
		st_ptr pt;
		// in this object?
		if(st_move(app_instance,object,propname,name_length) == 0)
			return super_prop;

		// no more extends -> property not found
		if(st_move(app_instance,object,HEAD_EXTENDS,HEAD_SIZE) != 0)
			return -1;

		pt = root;
		if(_copy_move(app_instance,&pt,*object))
			return -1;

		*object = pt;
		super_prop++;
	}
}

int cle_goto_prop(task* app_instance, st_ptr root, cdat object_name, uint object_length, st_ptr path, st_ptr* prop, int create)
{
	st_ptr obj,app = root;
	int c,i;
	char buffer[250];

	if(cle_goto_object(app_instance,&root,object_name,object_length))
		return 1;

	// first part
	i = 0;
	do
	{
		c = st_scan(app_instance,&path);
		buffer[i++] = c;
	}
	while(c > 0 && c != '.' && i < sizeof(buffer));

	if(i < 2 || i == sizeof(buffer))
		return 1;

	buffer[i - 1] = 0;

	obj = root;
	c = cle_get_property(app_instance,app,&root,buffer,i);
	if(c < 0)
		return 1;

	// remaining path
	if(_copy_move(app_instance,&root,path) != 0)
		return 1;

	if(c != 0)	// go to prop by index
	{
		if(create)
			;
		else
		{
			st_move(app_instance,&root,HEAD_PROPERTY,HEAD_SIZE);
		}
	}

	*prop = &root;
	return 0;
}

int cle_get_val(task* app_instance, st_ptr root, cdat object_name, uint object_length, st_ptr path, cle_pipe* response, void* data)
{
	st_ptr pt = root;
	if(cle_goto_object(app_instance,&root,object_name,object_length))
		return 1;

	pt = root;
	if(_copy_move(app_instance,&root,path) != 0)
		return 1;

	response->submit(data,app_instance,&root);

	return 0;
}
