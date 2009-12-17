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
#include "cle_object.h"
#include "cle_compile.h"

// defines
static const identity names_identity = 0;
static const identity start_state_identity = 1;

// helper-functions
static int _scan_validate(task* t, st_ptr* from, uint(*fun)(void*,char*,int), void* ctx)
{
	int state = 2;
	while(1)
	{
		int i = 0;
		char buffer[100];
		do
		{
			int c = st_scan(t,from);
			switch(c)
			{
			case 0:
				if(state != 0)
					return -4;
				state = 3;
				break;
			case '.':
				if(state != 0)
					return -3;
				state = 1;
				c = 0;
				break;
			case ' ':
			case '\t':
			case '\n':
			case '\r':
				continue;
			default:
				if(c < 0)
				{
					if(state != 3)
					{
						if(state != 0)
							return (state == 2)? -2 : -1;
						buffer[i++] = 0;
					}
					return fun(ctx,buffer,i);
				}
				state = 0;
			}
			buffer[i++] = c;
		}
		while(i < sizeof(buffer));

		if(i = fun(ctx,buffer,i))
			return i;
	}
}

struct _val_ctx
{
	task* t;
	union 
	{
		st_ptr* ptr;
		const char* chr;
	};
	uint val;
};

static int _mv(void* p, char* buffer, int len)
{
	struct _val_ctx* ctx = (struct _val_ctx*)p;
	return st_move(ctx->t,ctx->ptr,buffer,len);
}

static int _ins(void* p, char* buffer, int len)
{
	struct _val_ctx* ctx = (struct _val_ctx*)p;
	ctx->val = st_insert(ctx->t,ctx->ptr,buffer,len);
	return 0;
}

static _cmp(void* p, char* buffer, int len)
{
	struct _val_ctx* ctx = (struct _val_ctx*)p;
	int min_len = len > ctx->val ? ctx->val : len;
	if(min_len <= 0 || memcmp(buffer,ctx->chr,min_len) != 0)
		return 1;

	ctx->val -= min_len;
	ctx->chr += min_len;
	return 0;
}

static int _just_validate(void* p, char* buffer, int len) {return 0;}

static int _move_validate(task* t, st_ptr* to, st_ptr from)
{
	struct _val_ctx ctx;
	ctx.t = t;
	ctx.ptr = to;
	return _scan_validate(t,&from,_mv,&ctx);
}

static int _copy_validate(task* t, st_ptr* to, st_ptr from)
{
	struct _val_ctx ctx;
	int ret;
	ctx.t = t;
	ctx.ptr = to;
	ctx.val = 0;
	if(ret = _scan_validate(t,&from,_ins,&ctx))
		return ret;
	return ctx.val;
}

static int _cmp_validate(task* t, st_ptr from, const char* str, uint len)
{
	struct _val_ctx ctx;
	ctx.t = t;
	ctx.val = len;
	ctx.chr = str;
	return _scan_validate(t,&from,_cmp,&ctx);
}

/****************************************************
	implementations
	object store */
static oid _new_oid(cle_instance inst, st_ptr* newobj)
{
	oid id;	
	id._low = tk_segment(inst.t);

	while(1)
	{
		*newobj = inst.root;

		if(st_insert(inst.t,newobj,(cdat)&id._low,sizeof(segment)))
		{
			id._high[0] = 0;	// first key in segment
			id._high[1] = 0;
			id._high[2] = 0;
			id._high[3] = 1;
			break;
		}
		else
		{
			it_ptr it;
			int    i;
			it_create(inst.t,&it,newobj);

			it_prev(inst.t,0,&it,OID_HIGH_SIZE);

			// add one
			for(i = OID_HIGH_SIZE - 1; i >= 0; i--)
			{
				if(it.kdata[i] == 0xFF)
					it.kdata[i] = 0;
				else
				{
					it.kdata[i] += 1;
					break;
				}
			}

			memcpy(id._high,it.kdata,OID_HIGH_SIZE);

			it_dispose(inst.t,&it);

			if(id._high[i] != 0)
				break;

			// segment filled
			id._low = tk_new_segment(inst.t);
		}
	}

	// oid
	st_insert(inst.t,newobj,(cdat)&id._high,OID_HIGH_SIZE);
	return id;
}

int cle_new(cle_instance inst, st_ptr name, st_ptr extends, st_ptr* obj)
{
	st_ptr        newobj,names;
	objectheader2 header;
	identity      devid;

	names = inst.root;
	st_insert(inst.t,&names,HEAD_NAMES,IHEAD_SIZE);

	if(_copy_validate(inst.t,&names,name) <= 0)
		return __LINE__;

	if(extends.pg == 0)
	{
		// no extend: root-level object
		memset(&header.ext,0,sizeof(oid));
		devid = IDMAKE(1,0);
	}
	else
	{
		// get extend-header
		if(st_get(inst.t,&extends,(char*)&header,sizeof(header)) != -2)
			return __LINE__;

		// new header.id = extend.id
		header.ext = header.id;

		// get dev.header
		if(st_move(inst.t,&extends,HEAD_DEV,OHEAD_SIZE) != 0)
			return __LINE__;

		if(st_get(inst.t,&extends,(char*)&devid,sizeof(identity)) <= 0)
			return __LINE__;

		// new dev: level + 1
		devid = IDMAKE(IDLEVEL(devid) + 1,0);
	}

	// state = start
	header.state = start_state_identity;

	// create base-object: first new id
	header.id = _new_oid(inst,&newobj);

	if(obj != 0)
		*obj = newobj;

	// write header
	st_append(inst.t,&newobj,(cdat)&header,sizeof(header));

	// finish name
	st_append(inst.t,&names,(cdat)&header.id,sizeof(oid));

	// reflect name and dev
	st_append(inst.t,&newobj,HEAD_DEV,OHEAD_SIZE);

	st_append(inst.t,&newobj,(cdat)&devid,sizeof(identity));

	st_insert_st(inst.t,&newobj,&name);
	return 0;
}

int cle_new_mem(task* app_instance, st_ptr* extends, st_ptr* newobj)
{
	objheader header;
	st_ptr    pt;

	// new obj
	st_empty(app_instance,newobj);

	pt = *newobj;

	header.zero = 0;
	header.ext  = *extends;

	st_append(app_instance,&pt,(cdat)&header,sizeof(header));
}

// handle @
int cle_goto_object(cle_instance inst, st_str name, st_ptr* obj)
{
	st_ptr pt = inst.root;
	oid id;

	if(st_move(inst.t,&pt,HEAD_NAMES,IHEAD_SIZE) != 0)
		return __LINE__;
	
	if(st_move(inst.t,&pt,name.string,name.length) != 0)
		return __LINE__;

	if(st_get(inst.t,&pt,(char*)&id,sizeof(oid)) != -1)
		return __LINE__;

	*obj = inst.root;
	return st_move(inst.t,obj,(cdat)&id,sizeof(id));
}

// TODO: persist collections ?? or share one all the way down?
// recursively persist this object as well as extends and ref-by-property.
int cle_persist_object(cle_instance inst, st_ptr* obj)
{
	objheader header;
	st_ptr    newobj,ext,pt = *obj;
	it_ptr    it;

	if(st_get(inst.t,&pt,(char*)&header,sizeof(header)) != -2)
		return __LINE__;

	// object needs persisting?
	if(ISMEMOBJ(header) == 0)
		return 0;

	// save extends-ptr
	ext = header.ext;

	// get an id for this object
	header.obj.id = _new_oid(inst,&newobj);

	// write it to prevent endless recursions (obj <-> ext-obj)
	st_dataupdate(inst.t,obj,(cdat)&header.obj.id,sizeof(oid));

	// check and persist extends (if needed)
	if(cle_persist_object(inst,&ext))
		return __LINE__;

	// get ext-id
	if(st_get(inst.t,&ext,(char*)&header.obj.ext,sizeof(oid)) != -2)
		return __LINE__;

	// state = start
	header.obj.state = 0;

	// update header (all)
	st_dataupdate(inst.t,obj,(cdat)&header,sizeof(header));

	// link content to id
	st_link(inst.t,&newobj,obj);

	// recursivly persist all mem-objects on properties as well
	it_create(inst.t,&it,&pt);

	// for all mem-obj-refs
	while(it_next(inst.t,&pt,&it,PROPERTY_SIZE))
	{
		st_ptr memobj,upd = pt;
		struct
		{
			uchar head;
			oid   ref;
		} new_ref;

		if(st_scan(inst.t,&pt) != 'r')
			continue;

		if(st_get(inst.t,&pt,(char*)&memobj,sizeof(st_ptr)) != -1)
			return __LINE__;

		// persist property object...
		if(cle_persist_object(inst,&memobj))
			return __LINE__;

		// update ref
		if(st_get(inst.t,&memobj,(char*)&new_ref.ref,sizeof(oid)) != -1)
			return __LINE__;

		new_ref.head = 'R';
		st_update(inst.t,&upd,(cdat)&new_ref,sizeof(new_ref));
	}

	it_dispose(inst.t,&it);
	return 0;
}

int cle_delete_name(cle_instance inst, st_str name)
{
	st_ptr obj;

	if(cle_goto_object(inst,name,&obj))
		return __LINE__;

	if(st_offset(inst.t,&obj,sizeof(objectheader2)) != 0)
		return __LINE__;

	if(st_move(inst.t,&obj,HEAD_DEV,OHEAD_SIZE) != 0)
		return __LINE__;

	st_delete(t,&obj,0,0);

	obj = inst.root;
	if(st_move(inst.t,&obj,HEAD_NAMES,IHEAD_SIZE) != 0)
		return __LINE__;

	st_delete(inst.t,&obj,name.string,name.length);
	return 0;
}

/*
	Identity-functions 
*/
static identity _create_identity(cle_instance inst, st_ptr obj)
{
	objheader header;
	identity  id;

	if(st_get(inst.t,&obj,(char*)&header,sizeof(header)) != -2)
		return 0;

	if(ISMEMOBJ(header))
		return 0;

	if(st_insert(inst.t,&obj,HEAD_DEV,OHEAD_SIZE) != 0)
	{
		// create dev-handler in object
		// need to find level of this object
		uint level = 1;

		while(header.obj.ext._low != 0)
		{
			st_ptr pt = inst.root;
			level++;

			st_move(inst.t,&pt,(cdat)&header.obj.ext,sizeof(oid));
			st_get(inst.t,&pt,(char*)&header,sizeof(header));
			if(st_move(inst.t,&pt,HEAD_DEV,OHEAD_SIZE) == 0)
			{
				st_get(inst.t,&pt,(char*)id,sizeof(id));
				level += IDLEVEL(id);
				break;
			}
		}

		id = IDMAKE(level,0);

		st_append(inst.t,&obj,(cdat)&id,sizeof(id));
	}
	else
	{
		st_ptr pt = obj;
		st_get(inst.t,&pt,(char*)id,sizeof(id));

		id = IDMAKE(IDLEVEL(id),IDNUMBER(id) + 1);

		st_dataupdate(inst.t,&obj,(cdat)&id,sizeof(id));
	}

	return id;
}

struct _split_ctx
{
	task*   t;
	st_ptr* hostpart;
	st_ptr* namepart;
	uint    in_host;
};

static uint _do_split_name(void* pctx, char* chrs, int len)
{
	struct _split_ctx* ctx = (struct _split_ctx*)pctx;

	if(ctx->in_host != 0)
	{
		int hlen;
		for(hlen = 0; hlen < len; hlen++)
		{
			if(chrs[hlen] == 0)
			{
				// from first 0 its namepart
				ctx->in_host = 0;
				break;
			}
		}

		st_append(ctx->t,ctx->hostpart,chrs,hlen);

		len -= hlen;
		if(len == 0)
			return 0;
	}

	st_append(ctx->t,ctx->namepart,chrs,len);
	return 0;
}

static int _split_name(task* t, st_ptr* name, st_ptr hostpart, st_ptr namepart)
{
	struct _split_ctx ctx;
	ctx.t        = t;
	ctx.hostpart = &hostpart;
	ctx.namepart = &namepart;
	ctx.in_host  = 1;

	// prefix host
	st_append(t,&hostpart,(cdat)&names_identity,sizeof(identity));

	return _scan_validate(t,name,_do_split_name,&ctx);
}

static identity _identify(cle_instance inst, st_ptr obj, st_ptr name, uchar typeheader, uchar create)
{
	st_ptr hostpart,namepart,tobj,pt;
	struct {
		identity id;
		uchar    type;
	} _id;

	st_empty(inst.t,&namepart);
	st_empty(inst.t,&hostpart);

	if(_split_name(inst.t,&name,hostpart,namepart) != 0)
		return 0;

	// lookup host-object
	tobj = obj;
	while(1)
	{
		objheader header;

		pt = tobj;
		if(st_get(inst.t,&pt,(char*)&header,sizeof(header)) != -2)
			return 0;

		if(ISMEMOBJ(header))
			return 0;

		if(st_move_st(inst.t,&pt,&hostpart) == 0)
		{
			// create only on initial object
			if(create != 0 && (tobj.pg != obj.pg || tobj.key != obj.key))
				create = 0;
			break;
		}

		// host-part not found (root of parent-relation)
		if(header.obj.ext._low == 0)
		{
			// TODO: look in fixed names ("object" bound names)
			if(create == 0)
				return 0;
			break;
		}

		tobj = inst.root;
		if(st_move(inst.t,&tobj,(cdat)&header.obj.ext,sizeof(oid)) != 0)
			return 0;
	}

	// get or create identity for name
	if(create != 0)
	{
		_id.id = _create_identity(inst,obj);
		_id.type = typeheader;

		st_insert_st(inst.t,&obj,&hostpart);

		st_insert_st(inst.t,&obj,&namepart);

		st_update(inst.t,&obj,(cdat)&_id,sizeof(_id));
	}
	else
	{
		if(st_move_st(inst.t,&pt,&namepart) != 0)
			return 0;
		
		if(st_get(inst.t,&pt,(char*)&_id,sizeof(_id)) != -1)
			return 0;

		if(_id.type != typeheader)
			return 0;
	}
	return _id.id;
}

int _new_value(cle_instance inst, st_ptr obj, identity id, st_ptr* value)
{
	*value = obj;
	if(st_offset(inst.t,value,sizeof(objectheader2)))
		return __LINE__;

	st_insert(inst.t,value,(cdat)&id,sizeof(identity));
	st_delete(inst.t,value,0,0);
	return 0;
}

// TODO: write validator-expr here...
int cle_create_state(cle_instance inst, st_ptr obj, st_ptr newstate)
{
	identity id = _identify(inst,obj,newstate,'S',1);
	return (id == 0)? __LINE__ : 0;
}

int cle_set_state(cle_instance inst, st_ptr obj, st_ptr state)
{
	// name must not be used at lower level
	identity id = _identify(inst,obj,state,'S',0);
	if(id == 0)
		return __LINE__;

	st_offset(inst.t,&obj,sizeof(objectheader2) - sizeof(identity));

	st_dataupdate(inst.t,&obj,(cdat)&id,sizeof(identity));
	return 0;
}

int cle_create_property(cle_instance inst, st_ptr obj, st_ptr propertyname)
{
	identity id = _identify(inst,obj,propertyname,'y',1);
	return (id == 0)? __LINE__ : 0;
}

static void _record_source_and_path(task* app_instance, st_ptr dest, st_ptr path, st_ptr expr, char source_prefix)
{
	char buffer[] = "s";
	// insert path
	st_ptr pt = dest;
	st_insert(app_instance,&pt,"p",1);
	st_insert_st(app_instance,&pt,&path);

	// insert source
	buffer[1] = source_prefix;
	st_insert(app_instance,&dest,buffer,2);
	st_insert_st(app_instance,&dest,&expr);
}

/* values and exprs shadow names defined at a lower level */
int cle_create_expr(cle_instance inst, st_ptr obj, st_ptr path, st_ptr expr, cle_pipe* response, void* data)
{
	identity id;
	while(1)
	{
		switch(st_scan(inst.t,&expr))
		{
		case ':':	// inject named context resource
			id = _identify(inst,obj,path,'x',1);
			if(id == 0)
				return __LINE__;

			if(_new_value(inst,obj,id,&obj))
				return __LINE__;

			return _copy_validate(inst.t,&obj,&expr);
		case '=':	// expr
			id = _identify(inst,obj,path,'E',1);
			if(id == 0)
				return __LINE__;

			if(_new_value(inst,obj,id,&obj))
				return __LINE__;

			_record_source_and_path(inst.t,obj,path,expr,'=');
			// call compiler
			return cmp_expr(inst.t,&obj,&expr,response,data);
		case '(':	// method
			id = _identify(inst,obj,path,'M',1);
			if(id == 0)
				return __LINE__;

			if(_new_value(inst,obj,id,&obj))
				return __LINE__;

			_record_source_and_path(inst.t,obj,path,expr,'(');
			// call compiler
			return cmp_method(inst.t,&obj,&expr,response,data,0);
		case '[':	// collection (expr as index-spec?)
			id = _identify(inst,obj,path,'C',1);
			if(id == 0)
				return __LINE__;

			return _new_value(inst,obj,id,&obj);
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

static int _copy_handler_states(cle_instance inst, st_ptr obj, st_ptr handler, ptr_list* states)
{
	st_ptr pt;
	st_insert(inst.t,&handler,"S",1);

	for(; states != 0; states = states->link)
	{
		identity id = _identify(inst,obj,states->pt,'S',0);
		if(id == 0)
			return __LINE__;

		pt = handler;
		st_insert(inst.t,&pt,(cdat)&id,sizeof(identity));
	}
	return 0;
}

int cle_create_handler(cle_instance inst, st_ptr obj, st_ptr eventname, st_ptr expr, ptr_list* states, cle_pipe* response, void* data, enum handler_type type)
{
	st_ptr pt;
	struct {
		oid       object_id;
		identity  hid;
		uchar     type;
	} _handler_ptr;

	do
	{
		switch(st_scan(inst.t,&expr))
		{
		case '(':	// parameters begin
			break;
		case ' ':
		case '\t':
		case '\r':
		case '\n':
			continue;
		default:
			return __LINE__;
		}
	} while(0);

	// create handler in object
	_handler_ptr.hid = _identify(inst,obj,eventname,'H',1);
	if(_handler_ptr.hid == 0)
		return __LINE__;

	if(_new_value(inst,obj,_handler_ptr.hid,&pt))
		return __LINE__;

	_record_source_and_path(inst.t,pt,eventname,expr,'(');

	// open for these states
	if(_copy_handler_states(inst,obj,pt,states))
		return __LINE__;

	// compile handler
	if(cmp_method(inst.t,&pt,&expr,response,data,1))
		return __LINE__;

	pt = inst.root;
	// register handler in instance
	st_insert(inst.t,&pt,HEAD_EVENT,IHEAD_SIZE);

	// event-name
	if(_copy_validate(inst.t,&pt,eventname) < 0)
		return __LINE__;

	st_insert(inst.t,&pt,HEAD_HANDLER,HEAD_SIZE);

	// get oid
	if(st_get(inst.t,&obj,(char*)&_handler_ptr.object_id,sizeof(oid)) != -2)
		return __LINE__;

	_handler_ptr.type = type;
	// insert {oid,id (handler),type}
	st_insert(inst.t,&pt,(cdat)&_handler_ptr,sizeof(_handler_ptr));
	return 0;
}


int cle_get_handler(task* app_instance, st_ptr root, st_ptr oid, st_ptr* handler, st_ptr* object, cdat eventid, uint eventid_length, enum handler_type type)
{
	ulong state = 0;	// no object -> "start"
	char handlertype[2] = {0,(char)type};

	if(st_move(app_instance,&root,HEAD_OID,HEAD_SIZE) != 0)
		return -1;

	// lookup handler-object
	*handler = root;
	if(st_move_st(app_instance,handler,&oid) != 0)
		return -1;

	// is there a target-object?
	if(object->pg != 0 && type < PIPELINE_REQUEST)
	{
		st_ptr pt;
		objectheader header;
		// verify that target-object extends handler-object
		pt = *object;
		while(pt.pg != handler->pg || pt.key != handler->key)
		{
			// go to super-object (if any)
			st_ptr pt0;
			if(st_move(app_instance,&pt,HEAD_EXTENDS,HEAD_SIZE) != 0)
				return -1;

			pt0 = root;
			if(st_move_st(app_instance,&pt0,&pt) != 0)
				return -1;
			
			pt = pt0;
		}

		// get object-header
		pt = *object;
		if(st_move(app_instance,&pt,HEAD_OID,HEAD_SIZE) != 0)
			return -1;
		if(st_get(app_instance,&pt,(char*)&header,sizeof(header)) != -2)
			return -1;

		state = header.state;
	}
	else
		*object = *handler;

	// target-object must be in a state that allows this event
	if(st_move(app_instance,handler,HEAD_STATES,HEAD_SIZE) != 0)
		return -1;
	if(st_move(app_instance,handler,(cdat)&state,sizeof(state)) != 0)
		return -1;
	if(st_move(app_instance,handler,eventid,eventid_length) != 0)
		return -1;

	// set handler to the implementing handler-method
	if(st_move(app_instance,handler,(cdat)&handlertype,2) != 0)
		return -1;

	return 0;
}

int cle_get_oid(task* app_instance, st_ptr object, char* buffer, int buffersize)
{
	int i = 1;
	if(st_move(app_instance,&object,HEAD_OID,HEAD_SIZE) != 0)
		return 0;
	if(st_offset(app_instance,&object,sizeof(objectheader)) != 0)
		return 0;

	buffer[0] = '@';
	while(i < buffersize - 1)
	{
		int c = st_scan(app_instance,&object);
		if(c <= 0)
		{
			buffer[i] = 0;
			return i;
		}

		buffer[i++] = (c >> 4) + 'a';
		buffer[i++] = (c & 0xf) + 'a';
	}

	return 0;
}

// TODO target_oid -> st_ptr
int cle_get_target(task* app_instance, st_ptr root, st_ptr* object, cdat target_oid, uint target_oid_length)
{
	int i;
	char buffer[50];

	if(st_move(app_instance,&root,HEAD_OID,HEAD_SIZE) != 0)
		return 0;

	// decipher
	for(i = 0; i < target_oid_length; i++)
	{
		char val;
		if(i >= sizeof(buffer)*2)
			return 0;

		if(target_oid[i] >= 'a' && target_oid[i] <= 'q')
			val = target_oid[i] - 'a';
		else
			break;

		if(i & 1)
			buffer[i >> 1] |= val;
		else
			buffer[i >> 1] = val << 4;
	}
	
	i /= 2;
	buffer[i++] = 0;

	if(i != buffer[0] + 2)
		return 0;

	// lookup target object
	*object = root;
	if(st_move(app_instance,object,buffer,i))
		return 0;

	return (st_exsist(app_instance,object,HEAD_OID,HEAD_SIZE) == 0)? 0 : i;
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
		if(st_move_st(app_instance,&pt,object))
			return -1;

		*object = pt;
		super_prop++;
	}
}

int cle_get_property_host_st(task* app_instance, st_ptr root, st_ptr* object, st_ptr propname)
{
	int super_prop = 0;
	if(st_move(app_instance,&root,HEAD_OID,HEAD_SIZE) != 0)
		return -1;

	// seach inheritance-chain
	while(1)
	{
		st_ptr pt;
		// in this object?
		if(st_move_st(app_instance,object,&propname) == 0)
			return super_prop;

		// no more extends -> property not found
		if(st_move(app_instance,object,HEAD_EXTENDS,HEAD_SIZE) != 0)
			return -1;

		pt = root;
		if(st_move_st(app_instance,&pt,object))
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
	if(cle_get_property_host(app_instance,app,&root,buffer,i) < 0)
		return 1;

	// remaining path
	i = _move_validate(app_instance,&root,path);
	if(i != 0 && i != -2)
		return 1;

	// get prop-index
	if(st_get(app_instance,&root,buffer,HEAD_SIZE + PROPERTY_SIZE) >= 0 ||
		buffer[0] != 0 || buffer[1] != 'y')
	{
		// is there a header here?
		obj = root;
		if(st_get(app_instance,&obj,buffer,HEAD_SIZE) >= 0 || buffer[0] != 0)
			return 1;

		switch(buffer[1])
		{
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
	else
	{
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
			if(st_move_st(app_instance,&pt,&obj))
				break;

			obj = pt;
		}
	}

	*prop = root;
	return 0;
}
