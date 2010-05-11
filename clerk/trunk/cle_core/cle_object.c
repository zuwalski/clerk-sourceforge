/* 
    Clerk application and storage engine.
    Copyright (C) 2009  Lars Szuwalski

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
#include "cle_runtime.h"

// defines
static const identity names_identity = SYS_NAMES;
static const identity dev_identity = SYS_DEV;
static const identity start_state_identity = STATE_START;

struct _new_ref
{
	uchar zero;
	uchar head;
	oid   ref;
};

struct _mem_ref
{
	uchar zero;
	uchar head;
	st_ptr ptr;
};

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

			// segment filled
			if(id._high[i] != 0)
				break;

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
		if(st_get(inst.t,&extends,(char*)&header,sizeof(header)) >= 0)
			return __LINE__;

		// new header.id = extend.id
		header.ext = header.id;

		// get dev.header
		if(st_move(inst.t,&extends,(cdat)&dev_identity,sizeof(identity)) != 0)
			return __LINE__;

		if(st_get(inst.t,&extends,(char*)&devid,sizeof(identity)) >= 0)
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

	// finish name
	st_append(inst.t,&names,(cdat)&header.id,sizeof(oid));

	// write header
	st_append(inst.t,&newobj,(cdat)&header,sizeof(header));

	// reflect name and dev
	st_append(inst.t,&newobj,(cdat)&dev_identity,sizeof(identity));

	st_append(inst.t,&newobj,(cdat)&devid,sizeof(identity));

	st_insert_st(inst.t,&newobj,&name);
	return 0;
}

void cle_new_mem(task* app_instance, st_ptr extends, st_ptr* newobj)
{
	objheader header;
	st_ptr    pt;

	// new obj
	st_empty(app_instance,newobj);

	pt = *newobj;

	header.zero = 0;
	header.ext  = extends;

	st_append(app_instance,&pt,(cdat)&header,sizeof(header));
}

int cle_goto_object(cle_instance inst, st_ptr name, st_ptr* obj)
{
	st_ptr tname = name;
	oid id;

	if(st_scan(inst.t,&tname) == '@')
	{
		char* boid = (char*)&id;
		int i;

		for(i = 0; i < sizeof(oid)*2; i++)
		{
			int val = st_scan(inst.t,&tname) - 'a';
			if(val < 0 || (val & 0xF0))
				return __LINE__;

			if(i & 1)
				boid[i >> 1] |= val;
			else
				boid[i >> 1] = val << 4;
		}
		// may be too long ... hmmm
	}
	else
	{
		*obj = inst.root;
		if(st_move(inst.t,obj,HEAD_NAMES,IHEAD_SIZE) != 0)
			return __LINE__;
		
		if(st_move_st(inst.t,obj,&name) != 0)
			return __LINE__;

		if(st_get(inst.t,obj,(char*)&id,sizeof(oid)) != -1)
			return __LINE__;
	}

	*obj = inst.root;
	return st_move(inst.t,obj,(cdat)&id,sizeof(oid));
}

int cle_goto_object_cdat(cle_instance inst, cdat name, uint length, st_ptr* obj)
{
	oid id;
	*obj = inst.root;
	if(st_move(inst.t,obj,HEAD_NAMES,IHEAD_SIZE) != 0)
		return __LINE__;
	
	if(st_move(inst.t,obj,name,length) != 0)
		return __LINE__;

	if(st_get(inst.t,obj,(char*)&id,sizeof(oid)) != -1)
		return __LINE__;

	*obj = inst.root;
	return st_move(inst.t,obj,(cdat)&id,sizeof(oid));
}

int cle_get_oid(cle_instance inst, st_ptr obj, oid_str* oidstr)
{
	char* buffer = oidstr->chrs;
	objheader header;
	uint i;
	if(st_get(inst.t,&obj,(char*)&header,sizeof(header)) >= 0)
		return __LINE__;

	if(ISMEMOBJ(header))
		return __LINE__;

	buffer[0] = '@';
	buffer++;
	for(i = 0; i < sizeof(oid); i++)
	{
		int c = ((char*)&header.obj.id)[i];
		buffer[i*2] = (c >> 4) + 'a';
		buffer[i*2 + 1] = (c & 0xf) + 'a';
	}

	return 0;
}

static int _goto_id(cle_instance inst, st_ptr* child, oid id)
{
	*child = inst.root;
	return (id._low == 0 || st_move(inst.t,child,(cdat)&id,sizeof(oid)));
}

int cle_goto_parent(cle_instance inst, st_ptr* child)
{
	objheader header;

	if(st_get(inst.t,child,(char*)&header,sizeof(header)) >= 0)
		return __LINE__;

	if(ISMEMOBJ(header))
	{
		*child = header.ext;
		return 0;
	}

	return _goto_id(inst,child,header.obj.ext);
}

int cle_is_related_to(cle_instance inst, st_ptr parent, st_ptr child)
{
	do
	{
		// same
		if(parent.pg == child.pg && parent.key == child.key)
			return 1;
	}while(cle_goto_parent(inst,&child) == 0);
	return 0;
}

// recursively persist this object as well as extends and ref-by-property.
static int _persist_object(cle_instance inst, st_ptr* obj)
{
	objheader header;
	st_ptr    newobj,ext,pt = *obj;
	it_ptr    it;

	if(st_get(inst.t,&pt,(char*)&header,sizeof(header)) >= 0)
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
	if(_persist_object(inst,&ext))
		return __LINE__;

	// get ext-id
	if(st_get(inst.t,&ext,(char*)&header.obj.ext,sizeof(oid)) != -2)
		return __LINE__;

	// state = start
	header.obj.state = start_state_identity;

	// update header (all)
	st_dataupdate(inst.t,obj,(cdat)&header,sizeof(header));

	// link content to id
	st_link(inst.t,&newobj,obj);

	// recursivly persist all mem-objects on properties as well
	it_create(inst.t,&it,&pt);

	// for all mem-obj-refs
	while(it_next(inst.t,&pt,&it,PROPERTY_SIZE))
	{
		st_ptr upd = pt;
		struct _mem_ref mem_ref;

		if(st_get(inst.t,&pt,(char*)&mem_ref,sizeof(mem_ref)) == -1 &&
			mem_ref.zero == 0 && mem_ref.head == TYPE_REF_MEM)
		{
			struct _new_ref new_ref;

			// persist property object...
			if(_persist_object(inst,&mem_ref.ptr))
				return __LINE__;

			// update ref
			if(st_get(inst.t,&mem_ref.ptr,(char*)&new_ref.ref,sizeof(oid)) != -2)
				return __LINE__;

			new_ref.zero = 0;
			new_ref.head = TYPE_REF;
			st_update(inst.t,&upd,(cdat)&new_ref,sizeof(new_ref));
		}
	}

	it_dispose(inst.t,&it);
	return 0;
}

int cle_delete_name(cle_instance inst, st_ptr name)
{
	st_ptr obj;

	if(cle_goto_object(inst,name,&obj))
		return __LINE__;

	if(st_offset(inst.t,&obj,sizeof(objectheader2)) != 0)
		return __LINE__;

	if(st_move(inst.t,&obj,(cdat)&dev_identity,sizeof(identity)) != 0)
		return __LINE__;

	st_delete(inst.t,&obj,0,0);

	obj = inst.root;
	if(st_move(inst.t,&obj,HEAD_NAMES,IHEAD_SIZE) != 0)
		return __LINE__;

	st_delete_st(inst.t,&obj,&name);
	return 0;
}

/*
	Identity-functions 
*/
static identity _create_identity(cle_instance inst, st_ptr obj)
{
	objheader header;
	identity  id = 0;

	if(st_get(inst.t,&obj,(char*)&header,sizeof(header)) != -2)
		return 0;

	if(ISMEMOBJ(header))
		return 0;

	if(st_insert(inst.t,&obj,(cdat)&dev_identity,sizeof(identity)) != 0)
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
			if(st_move(inst.t,&pt,(cdat)&dev_identity,sizeof(identity)) == 0)
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
		if(st_get(inst.t,&pt,(char*)&id,sizeof(id)) != -2)
			return 0;

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
				hlen++;	// incl 0
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

static identity _identify(cle_instance inst, st_ptr obj, st_ptr name, enum property_type type, uchar create)
{
	st_ptr hostpart,namepart,tobj,pt;
	cle_typed_identity _id;
	objheader header;

	st_empty(inst.t,&namepart);
	st_empty(inst.t,&hostpart);

	if(_split_name(inst.t,&name,hostpart,namepart) != 0)
		return 0;

	// lookup host-object
	tobj = obj;
	do
	{
		pt = tobj;
		if(st_get(inst.t,&pt,(char*)&header,sizeof(header)) >= 0)
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
	}
	while(_goto_id(inst,&tobj,header.obj.ext) == 0);

	// get or create identity for name
	if(create != 0)
	{
		_id.id = _create_identity(inst,obj);
		_id.type = type;

		st_offset(inst.t,&obj,sizeof(objectheader2));

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

		if(_id.type != type)
			return 0;
	}
	return _id.id;
}

static int _new_value(cle_instance inst, st_ptr obj, identity id, st_ptr* value)
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
	identity id = _identify(inst,obj,newstate,TYPE_STATE,1);
	return (id == 0)? __LINE__ : 0;
}

int cle_set_state(cle_instance inst, st_ptr obj, st_ptr state)
{
	// name must not be used at lower level
	identity id = _identify(inst,obj,state,TYPE_STATE,0);
	if(id == 0)
		return __LINE__;

	st_offset(inst.t,&obj,sizeof(objectheader2) - sizeof(identity));

	st_dataupdate(inst.t,&obj,(cdat)&id,sizeof(identity));
	return 0;
}

int cle_create_property(cle_instance inst, st_ptr obj, st_ptr propertyname)
{
	identity id = _identify(inst,obj,propertyname,TYPE_ANY,1);
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
			id = _identify(inst,obj,path,TYPE_DEPENDENCY,1);
			if(id == 0)
				return __LINE__;

			if(_new_value(inst,obj,id,&obj))
				return __LINE__;

			return _copy_validate(inst.t,&obj,expr);
		case '=':	// expr
			id = _identify(inst,obj,path,TYPE_EXPR,1);
			if(id == 0)
				return __LINE__;

			if(_new_value(inst,obj,id,&obj))
				return __LINE__;

			_record_source_and_path(inst.t,obj,path,expr,'=');
			// call compiler
			return cmp_expr(inst.t,&obj,&expr,response,data);
		case '(':	// method
			id = _identify(inst,obj,path,TYPE_METHOD,1);
			if(id == 0)
				return __LINE__;

			if(_new_value(inst,obj,id,&obj))
				return __LINE__;

			_record_source_and_path(inst.t,obj,path,expr,'(');
			// call compiler
			return cmp_method(inst.t,&obj,&expr,response,data,0);
		case '[':	// collection (expr as index-spec?)
			id = _identify(inst,obj,path,TYPE_COLLECTION,1);
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
		identity id = _identify(inst,obj,states->pt,TYPE_STATE,0);
		if(id == 0)
			return __LINE__;

		pt = handler;
		st_insert(inst.t,&pt,(cdat)&id,sizeof(identity));
	}
	return 0;
}

int cle_create_handler(cle_instance inst, st_ptr obj, st_ptr eventname, st_ptr expr, ptr_list* states, cle_pipe* response, void* data, enum handler_type htype)
{
	st_ptr pt;
	cle_handler href;

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
	href.handler = _identify(inst,obj,eventname,TYPE_HANDLER,1);
	if(href.handler == 0)
		return __LINE__;

	if(_new_value(inst,obj,href.handler,&pt))
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
	if(st_get(inst.t,&obj,(char*)&href.oid,sizeof(oid)) != -2)
		return __LINE__;

	href.type = htype;
	// insert {oid,id (handler),type}
	st_insert(inst.t,&pt,(cdat)&href,sizeof(cle_handler));
	return 0;
}

int cle_identity_value(cle_instance inst, identity id, st_ptr* obj, st_ptr* value)
{
	do
	{
		st_ptr pt = *obj;

		if(st_offset(inst.t,&pt,sizeof(objectheader2)) != 0)
			break;

		if(st_move(inst.t,&pt,(cdat)&id,sizeof(identity)) == 0)
		{
			*value = pt;
			return 0;
		}
	}
	while(cle_goto_parent(inst,obj) == 0);

	return __LINE__;
}

static int _is_handler_allowed(cle_instance inst, cle_handler href, st_ptr handler, objectheader2* header, uint check_relation)
{
	// check if handler is allowed in current state
	if(st_move(inst.t,&handler,"S",1))
		return __LINE__;

	if(st_exsist(inst.t,&handler,(cdat)&header->state,sizeof(identity)) != 0)
		return __LINE__;

	// finish relation check if needed
	while(check_relation)
	{
		if(st_get(inst.t,&handler,(char*)header,sizeof(objectheader2)) >= 0)
			return __LINE__;

		if(memcmp(&header->id,&href.oid,sizeof(oid)) == 0)
			break;

		if(_goto_id(inst,&handler,header->ext) != 0)
			return __LINE__;
	}
	return 0;
}

int cle_get_handler(cle_instance inst, cle_handler href, st_ptr* obj, st_ptr* handler)
{
	objectheader2 header;
	uint check_relation = (obj->pg != 0);

	if(check_relation == 0 && _goto_id(inst,obj,href.oid) != 0)
		return __LINE__;

	do
	{
		if(st_get(inst.t,obj,(char*)&header,sizeof(objectheader2)) >= 0)
			return __LINE__;

		if(st_move(inst.t,obj,(cdat)&href.handler,sizeof(identity)) == 0)
		{
			*handler = *obj;

			return _is_handler_allowed(inst,href,*handler,&header,check_relation);
		}

		if(check_relation && memcmp(&header.id,&href.oid,sizeof(oid)) == 0)
			check_relation = 0;
	}
	while(_goto_id(inst,obj,header.ext) == 0);

	return __LINE__;
}

int cle_get_property_host(cle_instance inst, st_ptr* obj, cdat str, uint len)
{
	int level = 0;
	while(1)
	{
		st_ptr pt = *obj;

		if(st_offset(inst.t,&pt,sizeof(objectheader2)))
			return -1;

		if(st_move(inst.t,&pt,(cdat)&names_identity,sizeof(identity)) == 0)
		{
			if(st_move(inst.t,&pt,str,len) == 0)
			{
				*obj = pt;
				return level;
			}
		}

		if(cle_goto_parent(inst,obj))
			return -1;
		level++;
	}
}

int cle_get_property_host_st(cle_instance inst, st_ptr* obj, st_ptr propname)
{
	int level = 0;
	while(1)
	{
		st_ptr pt = *obj;

		if(st_offset(inst.t,&pt,sizeof(objectheader2)))
			return -1;

		if(st_move(inst.t,&pt,(cdat)&names_identity,sizeof(identity)) == 0)
		{
			if(st_move_st(inst.t,&pt,&propname) == 0)
			{
				*obj = pt;
				return level;
			}
		}

		if(cle_goto_parent(inst,obj))
			return -1;
		level++;
	}
}

int cle_probe_identity(cle_instance inst, st_ptr* reader, cle_typed_identity* id)
{
	return (st_get(inst.t,reader,(char*)id,sizeof(cle_typed_identity)) != -1);
}

int cle_get_property_ref_value(cle_instance inst, st_ptr prop, st_ptr* ref)
{
	union {
		struct _mem_ref mref;
		struct _new_ref nref;
	} reftype;

	if((st_get(inst.t,&prop,(char*)&reftype,sizeof(reftype)) & 0xFE) == 0 ||
		reftype.mref.zero != 0)
		return 1;

	if(reftype.mref.head == TYPE_REF)
		return _goto_id(inst,ref,reftype.nref.ref);

	if(reftype.mref.head != TYPE_REF_MEM)
		return 1;

	*ref = reftype.mref.ptr;
	return 0;
}

int cle_set_property_ref(cle_instance inst, st_ptr obj, cle_typed_identity id, st_ptr ref)
{
	objheader header_ref;
	st_ptr val;
	struct _new_ref new_ref;

	val = ref;	// to throw away
	if(st_get(inst.t,&val,(char*)&header_ref,sizeof(header_ref)) >= 0)
		return 1;

	if(_new_value(inst,obj,id.id,&val))
		return 1;

	if(ISMEMOBJ(header_ref))
	{
		objheader header;

		if(st_get(inst.t,&obj,(char*)&header,sizeof(header)) >= 0)
			cle_panic(inst.t);

		if(ISMEMOBJ(header))
		{
			struct _mem_ref mem_ref;
			mem_ref.zero = 0;
			mem_ref.head = TYPE_REF_MEM;
			mem_ref.ptr = ref;
			st_append(inst.t,&val,(cdat)&mem_ref,sizeof(mem_ref));
			return 0;
		}
		else
		{
			if(_persist_object(inst,&ref))
				return 1;

			if(st_get(inst.t,&ref,(char*)&header_ref,sizeof(header_ref)) >= 0)
				cle_panic(inst.t);
		}
	}

	new_ref.zero = 0;
	new_ref.head = TYPE_REF;
	new_ref.ref = header_ref.obj.id;
	st_append(inst.t,&val,(cdat)&new_ref,sizeof(new_ref));
	return 0;
}

int cle_get_property_ref(cle_instance inst, st_ptr obj, cle_typed_identity id, st_ptr* ref)
{
	if(cle_identity_value(inst,id.id,&obj,ref))
		return -1;

	return cle_get_property_ref_value(inst,*ref,ref);
}

int cle_get_property_num_value(cle_instance inst, st_ptr prop, double* dbl)
{
	struct {
		char zero;
		char num_type;
		double value;
	} _num;

	if(st_get(inst.t,&prop,(char*)&_num,sizeof(_num)) != -1)
		return 1;

	if(_num.zero != 0 || _num.num_type != TYPE_NUM)
		return 1;

	*dbl = _num.value;
	return 0;
}

int cle_set_property_num(cle_instance inst, st_ptr obj, cle_typed_identity id, double dbl)
{
	st_ptr val;
	struct {
		char zero;
		char num_type;
		double value;
	} _num;

	if(_new_value(inst,obj,id.id,&val))
		return 1;

	_num.zero = 0;
	_num.num_type = TYPE_NUM;
	_num.value = dbl;

	st_append(inst.t,&val,(cdat)&_num,sizeof(_num));
	return 0;
}

int cle_get_property_num(cle_instance inst, st_ptr obj, cle_typed_identity id, double* dbl)
{
	st_ptr prop;

	if(cle_identity_value(inst,id.id,&obj,&prop))
		return 1;

	return cle_get_property_num_value(inst,prop,dbl);
}

int cle_set_property_ptr(cle_instance inst, st_ptr obj, cle_typed_identity id, st_ptr* ptr)
{
	return _new_value(inst,obj,id.id,ptr);
}

enum property_type cle_get_property_type(cle_instance inst, st_ptr obj, cle_typed_identity id)
{
	st_ptr prop;
	struct {
		char zero;
		char type;
	} _head;

	if(cle_identity_value(inst,id.id,&obj,&prop))
		return TYPE_ILLEGAL;

	if(st_get(inst.t,&prop,(char*)&_head,sizeof(_head)) != -2 || _head.zero != 0)
		return TYPE_ANY;

	return (enum property_type) _head.type;
}

enum property_type cle_get_property_type_value(cle_instance inst, st_ptr prop)
{
	struct {
		char zero;
		char type;
	} _head;

	if(st_get(inst.t,&prop,(char*)&_head,sizeof(_head)) != -2 || _head.zero != 0)
		return TYPE_ANY;

	return (enum property_type) _head.type;
}

int cle_collection_add_object(cle_instance inst, st_ptr collection, st_ptr ref)
{
	objheader header;

	if(_persist_object(inst,&ref))
		return 1;

	if(st_get(inst.t,&ref,(char*)&header,sizeof(header)) >= 0 || ISMEMOBJ(header))
		return 1;

	st_insert(inst.t,&collection,(cdat)&header.obj.id,sizeof(oid));
	return 0;
}

int cle_collection_remove_object(cle_instance inst, st_ptr collection, st_ptr ref)
{
	objheader header;

	if(st_get(inst.t,&ref,(char*)&header,sizeof(header)) >= 0 || ISMEMOBJ(header))
		return 1;

	st_delete(inst.t,&collection,(cdat)&header.obj.id,sizeof(oid));
	return 0;
}

int cle_collection_test_object(cle_instance inst, st_ptr collection, st_ptr ref)
{
	objheader header;

	if(st_get(inst.t,&ref,(char*)&header,sizeof(header)) >= 0 || ISMEMOBJ(header))
		return 1;

	return (st_exsist(inst.t,&collection,(cdat)&header.obj.id,sizeof(oid)) == 0);
}
