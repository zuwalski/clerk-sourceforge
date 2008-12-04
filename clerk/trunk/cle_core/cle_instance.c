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
static int _copy_insert(task* t, st_ptr* to, st_ptr from)	// to be replaced by lowlevel copy
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

static int _copy_move(task* t, st_ptr* to, st_ptr from)	// to be replaced by lowlevel copy_move
{
	char buffer[200];

	while(1)
	{
		int length = st_get(t,&from,buffer,sizeof(buffer));

		if(length == 0)
			return 0;

		if(length == -2)
		{
			if(st_move(t,to,buffer,sizeof(buffer)) != 0)
				return 1;
		}
		else
		{
			if(length < 0)
				length = sizeof(buffer);
			return (st_move(t,to,buffer,length) != 0);
		}
	}
}

static int _copy_validate(task* t, st_ptr* to, st_ptr from, const uint do_insert)
{
	int lastinsert = 0,state = 2;

	do
	{
		int i = 0;
		char buffer[100];
		do
		{
			int c = st_scan(t,&from);
			if(c < 0)
			{
				if(state == 2)
					return -2;
				if(state != 1)
					return -1;
				state = -1;
				break;
			}

			if(c == '.')
				c = 0;
			if(c == 0)
			{
				if(state != 0)
					return -1;
				state = 1;
			}
			else
				state = 0;

			buffer[i++] = c;
		}
		while(i < sizeof(buffer));

		if(do_insert)
		{
			lastinsert = st_insert(t,to,buffer,i);
		}
		else if(st_move(t,to,buffer,i) != 0)
			return 1;
	}
	while(state >= 0);

	return (do_insert)? (lastinsert == 0) : 0;
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
	objectheader header;

	st_insert(app_instance,&app_root,HEAD_OID,HEAD_SIZE);

	it_create(app_instance,&it,&app_root);

	it_new(app_instance,&it,obj);

	st_insert(app_instance,&pt,it.kdata,it.kused);

	// reflect oid in object
	pt = *obj;
	st_insert(app_instance,&pt,HEAD_OID,HEAD_SIZE);

	header.level = 0;
	header.state = 0;
	header.next_property_id = 0;
	header.next_state_id = 1;
	// write header
	st_insert(app_instance,&pt,(cdat)&header,sizeof(header));

	st_insert(app_instance,&pt,it.kdata,it.kused);

	it_dispose(app_instance,&it);
}

int cle_new_object(task* app_instance, st_ptr app_root, st_ptr name, st_ptr* obj, ushort level)
{
	it_ptr it;
	st_ptr newobj,pt;
	objectheader header;

	pt = app_root;
	st_insert(app_instance,&pt,HEAD_NAMES,HEAD_SIZE);

	if(_copy_validate(app_instance,&pt,name,1))
		return 1;

	st_insert(app_instance,&app_root,HEAD_OID,HEAD_SIZE);

	it_create(app_instance,&it,&app_root);

	it_new(app_instance,&it,&newobj);

	st_insert(app_instance,&pt,it.kdata,it.kused);

	// reflect name in object
	pt = newobj;
	st_insert(app_instance,&pt,HEAD_NAMES,HEAD_SIZE);

	if(_copy_validate(app_instance,&pt,name,1))
		return 1;

	// reflect oid in object
	pt = newobj;
	st_insert(app_instance,&pt,HEAD_OID,HEAD_SIZE);

	header.level = level;
	header.state = 0;
	header.next_property_id = 0;
	header.next_state_id = 1;
	// write header
	st_insert(app_instance,&pt,(cdat)&header,sizeof(header));

	st_insert(app_instance,&pt,it.kdata,it.kused);

	it_dispose(app_instance,&it);

	if(obj != 0)
		*obj = newobj;
	return 0;
}

int cle_new(task* app_instance, st_ptr app_root, cdat extends_name, uint exname_length, st_ptr name, st_ptr* obj)
{
	st_ptr newobj,pt,pt1;
	objectheader header;

	pt = app_root;
	if(st_move(app_instance,&pt,HEAD_NAMES,HEAD_SIZE) != 0)
		return 1;
	
	if(st_move(app_instance,&pt,extends_name,exname_length) != 0)
		return 1;

	// move to extend-object
	pt1 = app_root;
	if(st_move(app_instance,&pt1,HEAD_OID,HEAD_SIZE) != 0)
		return 1;
	if(_copy_move(app_instance,&pt1,pt) != 0)
		return 1;
	// get header from extend-object
	if(st_move(app_instance,&pt1,HEAD_OID,HEAD_SIZE) != 0)
		return 1;
	if(st_get(app_instance,&pt1,(char*)&header,sizeof(header)) != -2)
		return 1;

	if(cle_new_object(app_instance,app_root,name,&newobj,header.level + 1))
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

int cle_create_state(task* app_instance, st_ptr root, cdat object_name, uint object_length, st_ptr state)
{
	st_ptr pt,pt0,app_root;
	objectheader header;
	char buffer[sizeof(start_state)];

	app_root = root;
	if(st_move(app_instance,&app_root,HEAD_OID,HEAD_SIZE) != 0)
		return 1;

	// reserved state-name
	pt = state;
	if(st_get(app_instance,&pt,buffer,sizeof(buffer)) == -1 && memcmp(start_state,buffer,sizeof(buffer)) == 0)
		return 1;

	if(cle_goto_object(app_instance,&root,object_name,object_length))
		return 1;

	// name must not be used at lower level
	pt0 = root;
	while(1)
	{
		st_ptr pt1;
		// go to super-object (if any)
		if(st_move(app_instance,&pt0,HEAD_EXTENDS,HEAD_SIZE) != 0)
			break;

		pt1 = app_root;
		if(_copy_move(app_instance,&pt1,pt0) != 0)
			break;
		
		pt0 = pt1;
		if(st_move(app_instance,&pt1,HEAD_STATE_NAMES,HEAD_SIZE) == 0)
		{
			// state-name used at lower level!
			if(_copy_move(app_instance,&pt1,state) == 0)
				return 1;
		}
	}

	pt = root;
	st_insert(app_instance,&pt,HEAD_STATE_NAMES,HEAD_SIZE);

	// copy state-name
	if(_copy_validate(app_instance,&pt,state,1))
		return 1;

	st_insert(app_instance,&pt,"\0",1);

	if(st_move(app_instance,&root,HEAD_OID,HEAD_SIZE) != 0)
		return 1;
	pt0 = root;
	if(st_get(app_instance,&root,(char*)&header,sizeof(header)) != -2)
		return 1;

	// link name to new id
	st_insert(app_instance,&pt,(cdat)&header.next_state_id,sizeof(ushort));
	st_insert(app_instance,&pt,(cdat)&header.level,sizeof(ushort));

	header.next_state_id++;
	// save new id
	return st_dataupdate(app_instance,&pt0,(char*)&header,sizeof(header));
}

int cle_set_state(task* app_instance, st_ptr root, cdat object_name, uint object_length, st_ptr state)
{
	st_ptr pt,pt0,app_root;
	objectheader header;
	char buffer[sizeof(start_state)];

	app_root = root;
	if(st_move(app_instance,&app_root,HEAD_OID,HEAD_SIZE) != 0)
		return 1;

	if(cle_goto_object(app_instance,&root,object_name,object_length))
		return 1;

	// get header
	pt0 = root;
	if(st_move(app_instance,&root,HEAD_OID,HEAD_SIZE) != 0)
		return 1;
	pt = root;
	if(st_get(app_instance,&pt,(char*)&header,sizeof(header)) != -2)
		return 1;

	pt = state;
	if(st_get(app_instance,&pt,buffer,sizeof(buffer)) == -1 && memcmp(start_state,buffer,sizeof(buffer)) == 0)
		header.state = 0;
	else
	{
		// find state id
		pt = pt0;
		while(1)
		{
			pt0 = pt;
			if(st_move(app_instance,&pt,HEAD_STATE_NAMES,HEAD_SIZE) == 0)
			{
				// state-name found
				if(_copy_validate(app_instance,&pt,state,0) == 0)
				{
					if(st_move(app_instance,&pt,"\0",1) == 0)
						break;
				}
			}

			// go to super-object (if any)
			pt = pt0;
			if(st_move(app_instance,&pt,HEAD_EXTENDS,HEAD_SIZE) != 0)
				return 1;

			pt0 = app_root;
			if(_copy_move(app_instance,&pt0,pt) != 0)
				return 1;
		}

		// pt points to state-id -> load into object-header
		if(st_get(app_instance,&pt,(char*)&header.state,sizeof(header.state)) != -1)
			return 1;
	}

	return st_dataupdate(app_instance,&root,(char*)&header,sizeof(header));
}

static st_ptr _blank = {0,0,0};

static void _record_source_and_path(task* app_instance, st_ptr dest, st_ptr path, st_ptr expr, char source_prefix)
{
	char buffer[] = "s";
	// insert path
	if(path.pg != 0)
	{
		st_ptr pt = dest;
		st_insert(app_instance,&pt,"p",1);
		_copy_insert(app_instance,&pt,path);
	}

	// insert source
	buffer[1] = source_prefix;
	st_insert(app_instance,&dest,buffer,2);
	_copy_insert(app_instance,&dest,expr);
}

int cle_set_handler(task* app_instance, st_ptr root, cdat object_name, uint object_length, st_ptr state, st_ptr eventname, st_ptr expr, cle_pipe* response, void* data, enum handler_type type)
{
	st_ptr pt,obj_root,app_root = root;
	char buffer[sizeof(start_state)];

	if(cle_goto_object(app_instance,&root,object_name,object_length))
		return 1;

	obj_root = root;
	pt = state;
	if(st_get(app_instance,&pt,buffer,sizeof(buffer)) != -1 || memcmp(start_state,buffer,sizeof(buffer)) != 0)
	{
		// find state
		pt = root;
		if(st_move(app_instance,&pt,HEAD_STATE_NAMES,HEAD_SIZE) != 0)
			return 1;

		// to name
		if(_copy_validate(app_instance,&pt,state,0))
			return 1;

		if(st_move(app_instance,&pt,"\0",1) != 0)
			return 1;

		st_insert(app_instance,&root,HEAD_STATES,HEAD_SIZE);

		_copy_insert(app_instance,&root,pt);
	}
	// start-state
	else
		st_insert(app_instance,&root,"\0s\0\0\0\0",6);

	// insert event-name
	if(_copy_validate(app_instance,&root,eventname,1) < 0)
		return 1;

	while(1)
	{
		switch(st_scan(app_instance,&expr))
		{
		case '(':	// method
			{
				it_ptr it;
				char handlertype[2];
				handlertype[0] = 0;
				handlertype[1] = (char)type;
				st_insert(app_instance,&root,(cdat)&handlertype,2);

				// clear all
				st_delete(app_instance,&root,0,0);

				// insert source
				_record_source_and_path(app_instance,root,_blank,expr,'(');
				// call compiler
				if(cmp_method(app_instance,&root,&expr,response,data))
					return 1;

				// register handler
				st_insert(app_instance,&app_root,HEAD_EVENT,HEAD_SIZE);

				// event-name
				if(_copy_validate(app_instance,&app_root,eventname,1) < 0)
					return 1;

				st_insert(app_instance,&app_root,HEAD_HANDLER,HEAD_SIZE);

				it_create(app_instance,&it,&app_root);

				// new index
				it_new(app_instance,&it,&app_root);

				it_dispose(app_instance,&it);

				// the handler-type
				st_insert(app_instance,&app_root,(cdat)&handlertype[1],1);

				// object-id of hosting object
				st_move(app_instance,&obj_root,HEAD_OID,HEAD_SIZE);

				_copy_insert(app_instance,&app_root,obj_root);
			}
			return 0;
		case ' ':
		case '\t':
		case '\r':
		case '\n':
			break;
		default:
			return 1;
		}
	}
}

int cle_get_handler(task* app_instance, st_ptr root, st_ptr oid, st_ptr* handler, st_ptr* object, cdat eventid, uint eventid_length, enum handler_type type)
{
	st_ptr pt;
	objectheader header;
	char handlertype[2];
	handlertype[0] = 0;
	handlertype[1] = (char)type;

	if(st_move(app_instance,&root,HEAD_OID,HEAD_SIZE) != 0)
		return -1;

	// lookup handler-object
	*handler = root;
	if(_copy_move(app_instance,handler,oid) != 0)
		return -1;

	// if no target -> handler and object are the same
	if(object->pg == 0)
		*object = *handler;
	else
	{
		// verify that target-object extends handler-object
		pt = *object;
		while(pt.pg != handler->pg || pt.key != handler->key || pt.offset != handler->offset)
		{
			// go to super-object (if any)
			st_ptr pt0;
			if(st_move(app_instance,&pt,HEAD_EXTENDS,HEAD_SIZE) != 0)
				return -1;

			pt0 = root;
			if(_copy_move(app_instance,&pt0,pt) != 0)
				return -1;
			
			pt = pt0;
		}
	}

	// get object-header
	pt = *object;
	if(st_move(app_instance,&pt,HEAD_OID,HEAD_SIZE) != 0)
		return -1;
	if(st_get(app_instance,&pt,(char*)&header,sizeof(header)) != -2)
		return -1;

	pt = *handler;
	// target-object must be in a state that allows this event
	if(st_move(app_instance,handler,HEAD_STATES,HEAD_SIZE) != 0)
		return -1;
	if(st_move(app_instance,handler,(cdat)&header.state,sizeof(header.state)) != 0)
		return -1;
	if(st_move(app_instance,handler,eventid,eventid_length) != 0)
		return -1;

	// set handler to the implementing handler-method
	if(st_move(app_instance,handler,(cdat)&handlertype,2) != 0)
		return -1;

	// get object-header - for handler-level
	if(st_move(app_instance,&pt,HEAD_OID,HEAD_SIZE) != 0)
		return -1;
	if(st_get(app_instance,&pt,(char*)&header,sizeof(header)) != -2)
		return -1;

	return header.level;
}

int cle_get_oid(task* app_instance, st_ptr object, char* buffer, int buffersize)
{
	int i = 1;
	if(st_move(app_instance,&object,HEAD_OID,HEAD_SIZE) != 0)
		return 1;
	if(st_offset(app_instance,&object,sizeof(objectheader)) != 0)
		return 1;

	buffer[0] = '#';
	while(i < buffersize - 1)
	{
		int c = st_scan(app_instance,&object);
		if(c <= 0)
		{
			buffer[i] = 0;
			return 0;
		}

		buffer[i++] = (c >> 4) + 'a';
		buffer[i++] = (c & 0xf) + 'a';
	}

	return 1;
}

int cle_get_target(task* app_instance, st_ptr root, st_ptr* object, cdat target_oid, uint target_oid_length)
{
	int i;
	char buffer[50];

	if(target_oid_length*2 >= sizeof(buffer))
		return 1;

	if(st_move(app_instance,&root,HEAD_OID,HEAD_SIZE) != 0)
		return 1;

	// decipher
	for(i = 0; i < target_oid_length; i++)
	{
		char val;

		if(target_oid[i] >= 'a' && target_oid[i] <= 'q')
			val = target_oid[i] - 'a';
		else if(target_oid[i] == 0)
			break;
		else
			return 1;

		if(i & 1)
			buffer[i >> 1] |= val;
		else
			buffer[i >> 1] = val << 4;
	}
	
	i /= 2;
	buffer[i++] = 0;

	// lookup target object
	*object = root;
	return st_move(app_instance,object,buffer,i);
}

int cle_get_property_host(task* app_instance, st_ptr root, st_ptr* object, cdat propname, uint name_length)
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

int cle_get_property(task* app_instance, st_ptr root, cdat object_name, uint object_length, st_ptr path, st_ptr* prop)
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

	buffer[i - 1] = 0;

	// find hosting object
	obj = root;
	c = cle_get_property_host(app_instance,app,&root,buffer,i);
	if(c < 0)
		return 1;

	// remaining path
	i = _copy_validate(app_instance,&root,path,0);
	if(i != 0 && i != -2)
		return 1;

	// not defined in this object? go to prop by index
	if(c != 0)
	{
		if(st_get(app_instance,&root,buffer,HEAD_SIZE + PROPERTY_SIZE) >= 0)
			return 1;

		if(buffer[0] != 0 || buffer[1] != 'y')
			return 1;

		// if object have no value for that property - use default value
		while(1)
		{
			st_ptr pt;

			if(st_move(app_instance,&obj,buffer,HEAD_SIZE + PROPERTY_SIZE) == 0)
			{
				*prop = obj;	// found value in object
				return 0;
			}

			// search for value at lower levels...
			if(st_move(app_instance,&obj,HEAD_EXTENDS,HEAD_SIZE) != 0)
				break;

			pt = app;
			if(st_move(app_instance,&pt,HEAD_OID,HEAD_SIZE) != 0)
				break;
			if(_copy_move(app_instance,&pt,obj))
				break;

			obj = pt;
		}
	}
	else
	{
		// is there a header here?
		obj = root;
		if(st_get(app_instance,&obj,buffer,HEAD_SIZE) >= 0 || buffer[0] != 0)
			return 1;

		switch(buffer[1])
		{
		case 'y':
			// property-def? show default-value
			root = obj;
			st_offset(app_instance,&root,PROPERTY_SIZE);
			break;
		case 'M':
		case 'E':
			// method or expr? show source
			root = obj;
			st_move(app_instance,&root,"s",1);
			break;
		default:
			// some other headertype ..
			return 1;
		}
	}

	*prop = root;
	return 0;
}

int cle_set_property(task* app_instance, st_ptr root, cdat object_name, uint object_length, st_ptr path, st_ptr defaultvalue)
{
	st_ptr pt,pt0;
	objectheader header;

	if(cle_goto_object(app_instance,&root,object_name,object_length))
		return 1;

	pt = root;
	if(_copy_validate(app_instance,&root,path,1))
		return 1;

	// read header
	if(st_move(app_instance,&pt,HEAD_OID,HEAD_SIZE) != 0)
		return 1;
	pt0 = pt;
	if(st_get(app_instance,&pt,(char*)&header,sizeof(header)) != -2)
		return 1;

	// creat property-header
	st_insert(app_instance,&root,HEAD_PROPERTY,HEAD_SIZE);
	// clear
	st_delete(app_instance,&root,0,0);

	st_insert(app_instance,&root,(cdat)&header.level,4);

	header.next_property_id++;

	if(st_dataupdate(app_instance,&pt0,(cdat)&header,sizeof(header)))
		return 1;

	if(st_is_empty(&defaultvalue) == 0)
		st_link(app_instance,&root,app_instance,&defaultvalue);

	return 0;
}

/* values and exprs shadow names defined at a lower level */
int cle_set_expr(task* app_instance, st_ptr app_root, cdat object_name, uint object_length, st_ptr path, st_ptr expr, cle_pipe* response, void* data)
{
	st_ptr pt0,pt = app_root;

	if(cle_goto_object(app_instance,&pt,object_name,object_length))
		return 1;

	if(_copy_validate(app_instance,&pt,path,1))
		return 1;

	// clear old
	st_delete(app_instance,&pt,0,0);

	while(1)
	{
		switch(st_scan(app_instance,&expr))
		{
		case ':':	// ref to named object
			st_insert(app_instance,&pt,HEAD_REF,HEAD_SIZE);

			pt0 = app_root;
			if(st_move(app_instance,&pt0,HEAD_NAMES,HEAD_SIZE) != 0)
				return 1;
			if(_copy_validate(app_instance,&pt0,expr,0))
				return 1;

			// copy oid
			_copy_insert(app_instance,&pt,pt0);
			return 0;
		case '=':	// expr
			st_insert(app_instance,&pt,HEAD_EXPR,HEAD_SIZE);

			_record_source_and_path(app_instance,pt,path,expr,'=');
			// call compiler
			return cmp_expr(app_instance,&pt,&expr,response,data);
		case '(':	// method
			st_insert(app_instance,&pt,HEAD_METHOD,HEAD_SIZE);

			_record_source_and_path(app_instance,pt,path,expr,'(');
			// call compiler
			return cmp_method(app_instance,&pt,&expr,response,data);
		case ' ':
		case '\t':
		case '\r':
		case '\n':
			break;
		case -1:	// empty branch (ok)
			return 0;
		default:
			return 1;
		}
	}
}
