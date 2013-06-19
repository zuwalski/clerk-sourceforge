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

#include <string.h>

// Identity Bit layout
// [31-22]	Level (0 == system/object)
// [21-0]	Running count (starting from 1)
#define IDLEVEL(id)  ((id) >> 22)
#define IDNUMBER(id) ((id) & 0x3FFFFF)
#define IDMAKE(level,number) (((level) << 22) | IDNUMBER(number))

typedef struct {
	oid id;
	ulong version;
	oid ext;
} objectheader2;
// followed by object-content

typedef union {
	struct {
		segment zero;
		st_ptr ext;
	};
	objectheader2 obj;
} objheader;

const oid NOOID = { 0, 0, 0, 0, 0 };

#define ISMEMOBJ(header) ((header).zero == 0)
// DEV-hdr: (next)identity
// ... followed by optional name

#define PROPERTY_SIZE (sizeof(identity))

// defines
static const identity names_identity = SYS_NAMES;
static const identity dev_identity = SYS_DEV;
static const identity state_identity = SYS_STATE;

struct _head {
	char zero;
	char type;
};

struct _new_ref {
	struct _head hd;
	char ref[sizeof(oid)];
};

struct _mem_ref {
	struct _head hd;
	char ptr[sizeof(st_ptr)];
};

/****************************************************
 Name scanner
 
 ****************************************************/

int cle_scan_validate(task* t, st_ptr* from, int (*fun)(void*, uchar*, uint), void* ctx) {
	uchar buffer[100];
	int state = 2;
	while (1) {
		int c, i = 0;
		do {
			c = st_scan(t, from);
			switch (c) {
			case 0:
				if (state != 1)
					return -4;
				state = 3;
				break;
			case '/':
			case '\\':
			case '.':
				if (state != 0)
					return -3;
				state = 0;
				c = 0;
				break;
			case ' ':
			case '\t':
			case '\n':
			case '\r':
				continue;
			default:
				if (c < 0) {
					if (state != 3) {
						if (state != 1)
							return (state == 2) ? -2 : -1;
						buffer[i++] = 0;
					}
					return fun(ctx, buffer, i);
				}
				state = 1;
			}
			buffer[i++] = c;
		} while (state != 0 && i < sizeof(buffer));

		if ((i = fun(ctx, buffer, i)))
			return i;
	}
}

struct _val_ctx {
	task* t;
	union {
		st_ptr* ptr;
		const char* chr;
	};
	uint val;
};

static int _mv(void* p, uchar* buffer, uint len) {
	struct _val_ctx* ctx = (struct _val_ctx*) p;
	return st_move(ctx->t, ctx->ptr, buffer, len);
}

static int _ins(void* p, uchar* buffer, uint len) {
	struct _val_ctx* ctx = (struct _val_ctx*) p;
	ctx->val = st_insert(ctx->t, ctx->ptr, buffer, len);
	return 0;
}

static int _cmp(void* p, uchar* buffer, uint len) {
	struct _val_ctx* ctx = (struct _val_ctx*) p;
	int min_len = len > ctx->val ? ctx->val : len;
	if (min_len <= 0 || memcmp(buffer, ctx->chr, min_len) != 0)
		return 1;

	ctx->val -= min_len;
	ctx->chr += min_len;
	return 0;
}

static int _just_validate(void* p, uchar* buffer, uint len) {
	return 0;
}

static int _move_validate(task* t, st_ptr* to, st_ptr from) {
	struct _val_ctx ctx;
	ctx.t = t;
	ctx.ptr = to;
	return cle_scan_validate(t, &from, _mv, &ctx);
}

static int _copy_validate(task* t, st_ptr* to, st_ptr from) {
	struct _val_ctx ctx;
	int ret;
	ctx.t = t;
	ctx.ptr = to;
	ctx.val = 0;
	if ((ret = cle_scan_validate(t, &from, _ins, &ctx)))
		return ret;
	return ctx.val;
}

static int _cmp_validate(task* t, st_ptr from, const char* str, uint len) {
	struct _val_ctx ctx;
	ctx.t = t;
	ctx.val = len;
	ctx.chr = str;
	return cle_scan_validate(t, &from, _cmp, &ctx);
}

/****************************************************
 Util functions
 
 ****************************************************/

static void _getheader(cle_instance inst, st_ptr* obj, objheader* hdr) {
	// get header
	if (st_get(inst.t, obj, (char*) hdr, sizeof(objheader)) >= 0)
		cle_panic(inst.t);
}

static uint _is_memobj(cle_instance inst, st_ptr obj) {
	ushort zero;
	if (st_get(inst.t, &obj, (char*) &zero, sizeof(zero)) >= 0)
		cle_panic(inst.t);
	return (zero == 0);
}

static void _add_value(cle_instance inst, st_ptr obj, identity id, st_ptr* value) {
	objheader header;
	*value = obj;

	_getheader(inst, value, &header);

	st_insert(inst.t, value, (cdat) &id, sizeof(identity));
}

static void _new_value(cle_instance inst, st_ptr obj, identity id, st_ptr* value) {
	_add_value(inst, obj, id, value);

	st_update(inst.t, value, 0, 0);
}

static int _goto_id(cle_instance inst, st_ptr* child, oid id) {
	*child = inst.root;
	return (id._low == 0 || st_move(inst.t, child, (cdat) &id, sizeof(oid)));
}

/****************************************************
 OBJ-BASENAMES */

static const char basenames[] = { 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x4, 'x', 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
		0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xa8, 0x0, 0x0, 0x0, 0x36, 0x0, 'i', 'n', 'i', 't', 0x0, 0x3, 0x0, 0x0, 0x0, 0x0,
		0x0, 0x0, 0x0, 0x3, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x3, 0x0, 0xc8, 0x0, 0x58, 0x0, 0x0, 0x0, 't', 'o', 's',
		't', 'r', 'i', 'n', 'g', 0x0, 0x4, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x4, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x5,
		0x0, 0xc0, 0x0, 0x0, 0x0, 0x0, 0x0, 'm', 'e', 's', 's', 'a', 'g', 'e', 0x0, 0x5, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x3,
		0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 };

/****************************************************
 implementations
 object store */
static oid _new_oid(cle_instance inst, st_ptr* newobj) {
	oid id;
	id._low = tk_segment(inst.t);

	while (1) {
		*newobj = inst.root;
		if (st_insert(inst.t, newobj, (cdat) &id._low, sizeof(segment))) {
			id._high[0] = 0; // first key in segment
			id._high[1] = 0;
			id._high[2] = 0;
			id._high[3] = 1;
			break;
		} else {
			it_ptr it;
			int i;
			it_create(inst.t, &it, newobj);

			it_prev(inst.t, 0, &it, OID_HIGH_SIZE);

			// add one
			for (i = OID_HIGH_SIZE - 1; i >= 0; i--) {
				if (it.kdata[i] == 0xFF)
					it.kdata[i] = 0;
				else {
					it.kdata[i] += 1;
					break;
				}
			}

			memcpy(id._high, it.kdata, OID_HIGH_SIZE);

			it_dispose(inst.t, &it);

			// segment filled
			if (id._high[i] != 0)
				break;

			id._low = tk_new_segment(inst.t);
		}
	}

	// oid
	st_insert(inst.t, newobj, (cdat) &id._high, OID_HIGH_SIZE);
	return id;
}

int cle_new(cle_instance inst, st_ptr name, st_ptr extends, st_ptr* obj) {
	st_ptr newobj, names;
	objectheader2 header;
	identity devid;

	names = inst.root;
	st_insert(inst.t, &names, HEAD_NAMES, IHEAD_SIZE);

	if (_copy_validate(inst.t, &names, name) < 0)
		return __LINE__;

	if (extends.pg == 0) {
		// no extend: root-level object
		memset(&header.ext, 0, sizeof(oid));
		devid = IDMAKE(1,0);
	} else {
		_getheader(inst, &extends, (objheader*) &header);

		// new header.id = extend.id
		header.ext = header.id;

		// get dev.header
		if (st_move(inst.t, &extends, (cdat) &dev_identity, sizeof(identity)) != 0)
			cle_panic(inst.t);

		if (st_get(inst.t, &extends, (char*) &devid, sizeof(identity)) >= 0)
			cle_panic(inst.t);

		// new dev: level + 1
		devid = IDMAKE(IDLEVEL(devid) + 1,0);
	}

	// create base-object: first new id
	header.id = _new_oid(inst, &newobj);

	if (obj != 0)
		*obj = newobj;

	// add object-id too name-set
	st_insert(inst.t, &names, HEAD_OBJECTS, HEAD_SIZE);
	st_insert(inst.t, &names, (cdat) &header.id, sizeof(oid));

	// finish object: write header
	st_append(inst.t, &newobj, (cdat) &header, sizeof(header));

	// reflect name and dev
	st_append(inst.t, &newobj, (cdat) &dev_identity, sizeof(identity));

	st_append(inst.t, &newobj, (cdat) &devid, sizeof(identity));

	st_insert_st(inst.t, &newobj, &name);
	return 0;
}

int cle_goto_id(cle_instance inst, st_ptr* obj, oid id) {
	return _goto_id(inst, obj, id);
}

int cle_goto_object(cle_instance inst, st_ptr name, st_ptr* obj) {
	st_ptr tname = name;
	oid id;

	if (st_scan(inst.t, &tname) == '@') {
		char* boid = (char*) &id;
		int i;

		for (i = 0; i < sizeof(oid) * 2; i++) {
			int val = st_scan(inst.t, &tname) - 'a';
			if (val < 0 || (val & 0xF0))
				return __LINE__;

			if (i & 1)
				boid[i >> 1] |= val;
			else
				boid[i >> 1] = val << 4;
		}
		// may be too long ... hmmm
	} else {
		*obj = inst.root;
		if (st_move(inst.t, obj, HEAD_NAMES, IHEAD_SIZE) != 0)
			return __LINE__;

		if (_move_validate(inst.t, obj, name) != 0)
			return __LINE__;

		if (st_move(inst.t, obj, HEAD_OBJECTS, HEAD_SIZE) != 0)
			return __LINE__;

		if (st_get(inst.t, obj, (char*) &id, sizeof(oid)) != -1)
			return __LINE__;
	}

	return _goto_id(inst, obj, id);
}

oid cle_oid_from_cdat(cle_instance inst, cdat name, uint length) {
	oid id;
	char* boid = (char*) &id;
	int i;

	if (length != sizeof(oid) * 2)
		return NOOID;

	for (i = 0; i < length; i++) {
		int val = name[i] - 'a';
		if (val < 0 || (val & 0xF0))
			return NOOID;

		if (i & 1)
			boid[i >> 1] |= val;
		else
			boid[i >> 1] = val << 4;
	}

	return id;
}

int cle_delete_name(cle_instance inst, st_ptr name) {
	st_ptr obj = inst.root;
	if (st_move(inst.t, &obj, HEAD_NAMES, IHEAD_SIZE) != 0)
		return __LINE__;

	return st_delete_st(inst.t, &obj, &name);
}

int cle_get_oid_str(cle_instance inst, st_ptr obj, oid_str* oidstr) {
	char* buffer = oidstr->chrs;
	objheader header;
	uint i;

	_getheader(inst, &obj, &header);

	if (ISMEMOBJ(header))
		return __LINE__;

	buffer[0] = '@';
	buffer++;
	for (i = 0; i < sizeof(oid); i++) {
		int c = ((char*) &header.obj.id)[i];
		buffer[i * 2] = (c >> 4) + 'a';
		buffer[i * 2 + 1] = (c & 0xf) + 'a';
	}

	return 0;
}

oid cle_get_oid(cle_instance inst, st_ptr obj) {
	objheader header;
	_getheader(inst, &obj, &header);
	return header.obj.id;
}

int cle_goto_parent(cle_instance inst, st_ptr* child) {
	objheader header;

	_getheader(inst, child, &header);

	if (ISMEMOBJ(header)) {
		*child = header.ext;
		return 0;
	}

	return _goto_id(inst, child, header.obj.ext);
}

int cle_is_related_to(cle_instance inst, st_ptr parent, st_ptr child) {
	do {
		// same
		if (parent.pg == child.pg && parent.key == child.key)
			return 1;
	} while (cle_goto_parent(inst, &child) == 0);
	return 0;
}

void cle_new_mem(task* app_instance, st_ptr extends, st_ptr* newobj) {
	objheader header;
	st_ptr pt;

	// new obj
	st_empty(app_instance, newobj);

	pt = *newobj;

	header.zero = 0;
	header.ext = extends;

	st_append(app_instance, &pt, (cdat) &header, sizeof(header));
}

/*
 Identity-functions
 */
static identity _create_identity(cle_instance inst, st_ptr obj) {
	objheader header;
	identity id = 0;

	_getheader(inst, &obj, &header);

	if (ISMEMOBJ(header))
		return 0;

	if (st_insert(inst.t, &obj, (cdat) &dev_identity, sizeof(identity)) != 0) {
		// create dev-handler in object
		// need to find level of this object
		uint level = 1;

		while (header.obj.ext._low != 0) {
			st_ptr pt = inst.root;
			level++;

			st_move(inst.t, &pt, (cdat) &header.obj.ext, sizeof(oid));
			_getheader(inst, &pt, &header);

			if (st_move(inst.t, &pt, (cdat) &dev_identity, sizeof(identity)) == 0) {
				st_get(inst.t, &pt, (char*) id, sizeof(id));
				level += IDLEVEL(id);
				break;
			}
		}

		id = IDMAKE(level,0);

		st_append(inst.t, &obj, (cdat) &id, sizeof(id));
	} else {
		st_ptr pt = obj;
		if (st_get(inst.t, &pt, (char*) &id, sizeof(id)) != -2)
			return 0;

		id = IDMAKE(IDLEVEL(id),IDNUMBER(id) + 1);

		st_dataupdate(inst.t, &obj, (cdat) &id, sizeof(id));
	}

	return id;
}

struct _split_ctx {
	task* t;
	st_ptr* hostpart;
	st_ptr* namepart;
	uint in_host;
};

static int _do_split_name(void* pctx, uchar* chrs, uint len) {
	struct _split_ctx* ctx = (struct _split_ctx*) pctx;

	if (ctx->in_host != 0) {
		int hlen;
		for (hlen = 0; hlen < len; hlen++) {
			if (chrs[hlen] == 0) {
				// from first 0 its namepart
				ctx->in_host = 0;
				hlen++; // incl 0
				break;
			}
		}

		st_append(ctx->t, ctx->hostpart, chrs, hlen);

		len -= hlen;
		if (len == 0)
			return 0;
	}

	st_append(ctx->t, ctx->namepart, chrs, len);
	return 0;
}

static int _split_name(task* t, st_ptr* name, st_ptr hostpart, st_ptr namepart) {
	struct _split_ctx ctx;
	ctx.t = t;
	ctx.hostpart = &hostpart;
	ctx.namepart = &namepart;
	ctx.in_host = 1;

	// prefix host
	st_append(t, &hostpart, (cdat) &names_identity, sizeof(identity));

	return cle_scan_validate(t, name, _do_split_name, &ctx);
}

static identity _identify(cle_instance inst, st_ptr obj, st_ptr name, const enum property_type type, uint create) {
	st_ptr hostpart, namepart, tobj, pt;
	cle_typed_identity _id;
	objheader header;

	st_empty(inst.t, &namepart);
	st_empty(inst.t, &hostpart);

	if (_split_name(inst.t, &name, hostpart, namepart) != 0)
		return 0;

	// lookup host-object
	tobj = obj;
	do {
		pt = tobj;
		_getheader(inst, &pt, &header);

		if (ISMEMOBJ(header))
			return 0;

		if (st_move_st(inst.t, &pt, &hostpart) == 0) {
			// create only on initial object
			if (create != 0 && (tobj.pg != obj.pg || tobj.key != obj.key))
				create = 0;
			break;
		}

		// host-part not found (root of parent-relation)
		if (header.obj.ext._low == 0) {
			// look in fixed names ("object" bound names)
//			st_ptr pt = {basenames, 0, 0};
//			if (st_move_st(inst.t, &pt, &hostpart) == 0)
//				create = 0;
//			else 
			if (create == 0)
				return 0;
			break;
		}
	} while (_goto_id(inst, &tobj, header.obj.ext) == 0);

	// get or create identity for name
	if (create != 0) {
		_id.id = _create_identity(inst, obj);
		_id.type = type;

		st_offset(inst.t, &obj, sizeof(objectheader2));

		st_insert_st(inst.t, &obj, &hostpart);

		st_insert_st(inst.t, &obj, &namepart);

		st_update(inst.t, &obj, (cdat) &_id, sizeof(_id));
	} else {
		if (st_move_st(inst.t, &pt, &namepart) != 0)
			return 0;

		if (st_get(inst.t, &pt, (char*) &_id, sizeof(_id)) != -1)
			return 0;

		if (_id.type != type)
			return 0;
	}
	return _id.id;
}

// TODO: write validator-expr here...
int cle_create_state(cle_instance inst, st_ptr obj, st_ptr newstate) {
	identity id = _identify(inst, obj, newstate, TYPE_STATE, 1);
	return (id == 0) ? __LINE__ : 0;
}

int cle_set_state(cle_instance inst, st_ptr obj, st_ptr state) {
	st_ptr value;
	// name must not be used at lower level
	identity id = _identify(inst, obj, state, TYPE_STATE, 0);
	if (id == 0)
		return __LINE__;

	_add_value(inst, obj, state_identity, &value);

	st_update(inst.t, &value, (cdat) &id, sizeof(identity));
	return 0;
}

int cle_create_property(cle_instance inst, st_ptr obj, st_ptr propertyname, identity* id) {
	*id = _identify(inst, obj, propertyname, TYPE_ANY, 1);
	return (*id == 0) ? __LINE__ : 0;
}

static void _record_source_and_path(task* app_instance, st_ptr dest, st_ptr path, st_ptr expr, char source_prefix) {
	uchar buffer[] = "s";
	// insert path
	st_ptr pt = dest;
	st_insert(app_instance, &pt, (cdat) "p", 1);
	st_insert_st(app_instance, &pt, &path);

	// insert source
	buffer[1] = source_prefix;
	st_insert(app_instance, &dest, buffer, 2);
	st_insert_st(app_instance, &dest, &expr);
}

/* values and exprs shadow names defined at a lower level */
/*
 int cle_create_expr(cle_instance inst, st_ptr obj, st_ptr path, st_ptr expr, cle_pipe* response, void* data) {
 identity id;
 while (1) {
 switch (st_scan(inst.t, &expr)) {
 case ':': // inject named context resource
 id = _identify(inst, obj, path, TYPE_DEPENDENCY, 1);
 if (id == 0)
 return __LINE__;

 _new_value(inst, obj, id, &obj);

 return _copy_validate(inst.t, &obj, expr);
 case '=': // expr
 id = _identify(inst, obj, path, TYPE_EXPR, 1);
 if (id == 0)
 return __LINE__;

 _new_value(inst, obj, id, &obj);

 _record_source_and_path(inst.t, obj, path, expr, '=');
 // call compiler
 return cmp_expr(inst.t, &obj, &expr, response, data);
 case '(': // method
 id = _identify(inst, obj, path, TYPE_METHOD, 1);
 if (id == 0)
 return __LINE__;

 _new_value(inst, obj, id, &obj);

 _record_source_and_path(inst.t, obj, path, expr, '(');
 // call compiler
 return cmp_method(inst.t, &obj, &expr, response, data, 0);
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
 */

int cle_identity_value(cle_instance inst, identity id, st_ptr obj, st_ptr* value) {
	do {
		st_ptr pt = obj;

		if (st_offset(inst.t, &pt, sizeof(objectheader2)) != 0)
			break;

		if (st_move(inst.t, &pt, (cdat) &id, sizeof(identity)) == 0) {
			*value = pt;
			return 0;
		}
	} while (cle_goto_parent(inst, &obj) == 0);

	return __LINE__;
}

int cle_get_property_host(cle_instance inst, st_ptr* obj, cdat str, uint len) {
	int level = 0;
	while (1) {
		st_ptr pt = *obj;

		if (st_offset(inst.t, &pt, sizeof(objectheader2)))
			return -1;

		if (st_move(inst.t, &pt, (cdat) &names_identity, sizeof(identity)) == 0) {
			if (st_move(inst.t, &pt, str, len) == 0) {
				*obj = pt;
				return level;
			}
		}

		if (cle_goto_parent(inst, obj))
			return -1;
		level++;
	}
}

int cle_get_property_host_st(cle_instance inst, st_ptr* obj, st_ptr propname) {
	int level = 0;
	while (1) {
		st_ptr pt = *obj;

		if (st_offset(inst.t, &pt, sizeof(objectheader2)))
			return -1;

		if (st_move(inst.t, &pt, (cdat) &names_identity, sizeof(identity)) == 0) {
			if (st_move_st(inst.t, &pt, &propname) == 0) {
				*obj = pt;
				return level;
			}
		}

		if (cle_goto_parent(inst, obj))
			return -1;
		level++;
	}
}

int cle_probe_identity(cle_instance inst, st_ptr* reader, cle_typed_identity* id) {
	return (st_get(inst.t, reader, (char*) id, sizeof(cle_typed_identity)) != -1);
}

static void _write_prop(task* t, st_ptr* pt, enum property_type type, cdat val, uint valsize) {
	struct _head h;
	h.zero = 0;
	h.type = type;

	st_insert(t, pt, (cdat) &h, sizeof(h));
	st_append(t, pt, val, valsize);
}

int cle_get_property_ref_value(cle_instance inst, st_ptr prop, st_ptr* ref) {
	union {
		struct _mem_ref mref;
		struct _new_ref nref;
	} reftype;

	if ((st_get(inst.t, &prop, (char*) &reftype, sizeof(reftype)) & 0xFE) == 0 || reftype.mref.hd.zero != 0)
		return 1;

	if (reftype.mref.hd.type == TYPE_REF)
		return _goto_id(inst, ref, *((oid*) reftype.nref.ref));

	if (reftype.mref.hd.type != TYPE_REF_MEM)
		return 1;

	*ref = *((st_ptr*) reftype.mref.ptr);
	return 0;
}

int cle_set_property_ref(cle_instance inst, st_ptr obj, identity id, st_ptr ref) {
	st_ptr val;

	tk_ref_ptr(&ref);

	_new_value(inst, obj, id, &val);

	_write_prop(inst.t, &val, TYPE_REF_MEM, (cdat) &ref, sizeof(st_ptr));

	return 0;
}

int cle_get_property_ref(cle_instance inst, st_ptr obj, identity id, st_ptr* ref) {
	if (cle_identity_value(inst, id, obj, ref))
		return 1;

	return cle_get_property_ref_value(inst, *ref, ref);
}

int cle_get_property_num_value(cle_instance inst, st_ptr prop, double* dbl) {
	struct {
		char zero;
		char num_type;
		char value[sizeof(double)];
	} _num;

	if (st_get(inst.t, &prop, (char*) &_num, sizeof(_num)) != -1)
		return 1;

	if (_num.zero != 0 || _num.num_type != TYPE_NUM)
		return 1;

	*dbl = *((double*) _num.value);
	return 0;
}

int cle_set_property_num(cle_instance inst, st_ptr obj, identity id, double dbl) {
	st_ptr val;
	_new_value(inst, obj, id, &val);

	_write_prop(inst.t, &val, TYPE_NUM, (cdat) &dbl, sizeof(dbl));
	return 0;
}

int cle_get_property_num(cle_instance inst, st_ptr obj, identity id, double* dbl) {
	st_ptr prop;

	if (cle_identity_value(inst, id, obj, &prop))
		return 1;

	return cle_get_property_num_value(inst, prop, dbl);
}

int cle_set_property_ptr(cle_instance inst, st_ptr obj, identity id, st_ptr* ptr) {
	_new_value(inst, obj, id, ptr);
	return 0;
}

enum property_type cle_get_property_type(cle_instance inst, st_ptr obj, identity id) {
	st_ptr prop;
	struct _head _head;

	if (cle_identity_value(inst, id, obj, &prop))
		return TYPE_ILLEGAL;

	if (st_get(inst.t, &prop, (char*) &_head, sizeof(_head)) != -2 || _head.zero != 0)
		return TYPE_ANY;

	return (enum property_type) _head.type;
}

enum property_type cle_get_property_type_value(cle_instance inst, st_ptr prop) {
	struct _head _head;

	if (st_get(inst.t, &prop, (char*) &_head, sizeof(_head)) != -2 || _head.zero != 0)
		return TYPE_ANY;

	return (enum property_type) _head.type;
}

//////////// commit V1 ///////////////////

static const char _mem_obj_header[] = { 0, TYPE_REF_MEM };
static const char _ref_obj_header[] = { 0, TYPE_REF };

static void _write_obj_ref(cle_instance inst, st_ptr* target, st_ptr* obj) {
	objheader header;
	st_ptr pt = *obj;

	_getheader(inst, &pt, &header);

	if (ISMEMOBJ(header)) {
		st_insert(inst.t, target, (cdat) _mem_obj_header, sizeof(_mem_obj_header));
		st_insert(inst.t, target, (cdat) obj, sizeof(st_ptr));

		tk_ref_ptr(obj);
	} else {
		st_insert(inst.t, target, (cdat) _ref_obj_header, sizeof(_ref_obj_header));
		st_insert(inst.t, target, (cdat) &header.obj.id, sizeof(oid));
	}
}

// commit all updates to back-end - persist all new objects

// recursively persist this object and all refs
static int _persist_object(cle_instance inst, st_ptr* obj, oid* newid) {
	objheader header;
	st_ptr newobj, ext, pt = *obj;
	it_ptr it;

	if (st_get(inst.t, &pt, (char*) &header, sizeof(header)) >= 0)
		return __LINE__;

	// object needs persisting?
	if (ISMEMOBJ(header) == 0) {
		*newid = header.obj.id;
		return 0;
	}

	// save extends-ptr
	ext = header.ext;

	// get an id for this object
	*newid = header.obj.id = _new_oid(inst, &newobj);

	// write it to prevent endless recursions (obj <-> ext-obj)
	st_dataupdate(inst.t, obj, (cdat) &header.obj.id, sizeof(oid));

	// check and persist extends (if needed)
	if (_persist_object(inst, &ext, &header.obj.ext))
		return __LINE__;

	// state = start

	// update header (all)
	st_dataupdate(inst.t, obj, (cdat) &header, sizeof(header));

	// link content to id
	st_link(inst.t, &newobj, obj);

	// recursivly persist all mem-objects on properties as well
	it_create(inst.t, &it, &pt);

	// for all mem-obj-refs
	while (it_next(inst.t, &pt, &it, PROPERTY_SIZE)) {
		st_ptr upd = pt;
		struct _head _head;

		if (st_get(inst.t, &pt, (char*) &_head, sizeof(_head)) != -2 || _head.zero != 0)
			continue;

		if (_head.type == TYPE_REF_MEM) {
			st_ptr mem_ref;
			struct _new_ref new_ref;

			if (st_get(inst.t, &pt, (char*) &mem_ref, sizeof(mem_ref)) != -1)
				return __LINE__;

			// persist property object...
			if (_persist_object(inst, &mem_ref, (oid*) &new_ref.ref))
				return __LINE__;

			new_ref.hd.zero = 0;
			new_ref.hd.type = TYPE_REF;
			st_update(inst.t, &upd, (cdat) &new_ref, sizeof(new_ref));
		} else if (_head.type == TYPE_COLLECTION) {
			it_ptr colit;
			st_ptr newcollection;

			// iterate mem-collection (prevents update from removing pages)
			it_create(inst.t, &colit, &pt);

			// remove mem-collection
			st_update(inst.t, &upd, (cdat) &_head, sizeof(_head));
			newcollection = upd;

			// persist all objects in collection
			while (it_next(inst.t, 0, &colit, sizeof(st_ptr))) {
				oid _id;
				st_ptr tmp = *((st_ptr*) it.kdata);

				if (_persist_object(inst, &tmp, &_id))
					return __LINE__;

				tmp = newcollection;
				st_insert(inst.t, &tmp, (cdat) &_id, sizeof(_id));
			}

			// and release ref to old collection
			it_dispose(inst.t, &colit);
		}
	}

	it_dispose(inst.t, &it);
	return 0;
}

///////////////////////// tracing/delta-streaming commit V1 //////////////
/**
 * 0--1-------2-------------3---4---3-----(5)--
 *	/OID/{Object-header}/content/content|header
 */
struct _trace_ctx {
	cle_datasource* src;
	void* sdat;
	cle_instance inst;
	st_ptr newgen;
	uint offset;
	uint state;
};

static uint _t_dat(void* p, cdat c, uint l) {
	struct _trace_ctx* ctx = (struct _trace_ctx*) p;
	cdat to = c + l;

	for (to = c + l; c < to; c++) {
		if (ctx->state == 1) {
			ctx->state = 0;
		}
		if (*c == 0)
			ctx->state = 1;
	}
	return ctx->src->commit_data(ctx->sdat, c, l);
}
static uint _t_push(void* p) {
	struct _trace_ctx* ctx = (struct _trace_ctx*) p;
	return ctx->src->commit_push(ctx->sdat);
}
static uint _t_pop(void* p) {
	struct _trace_ctx* ctx = (struct _trace_ctx*) p;
	return ctx->src->commit_pop(ctx->sdat);
}

/**
 * Trace all mem-refs and persist.
 * Stream delta-trees to backend.
 */
int cle_commit_objects(cle_instance inst) {
	struct _trace_ctx ctx;
	st_ptr del, ins;

	st_empty(inst.t, &del);
	st_empty(inst.t, &ins);

	// compute delta
	if (tk_delta(inst.t, &del, &ins) == 0)
		return 0;

	ctx.src->commit_begin(ctx.sdat);

	// stream deletes
	if (!st_is_empty(&del)) {
		ctx.src->commit_deletes(ctx.sdat);

		if (st_map_st(inst.t, &del, ctx.src->commit_data, ctx.src->commit_push, ctx.src->commit_pop, ctx.sdat))
			return -1;
	}

	ctx.inst = inst;

	// trace and stream persistent objects
	for (st_empty(inst.t, &ctx.newgen); !st_is_empty(&ins); ins = ctx.newgen) {
		ctx.offset = 0;
		ctx.state = 0;

		if (st_map_st(inst.t, &ins, _t_dat, _t_push, _t_pop, &ctx)) {
			// send rollback
			return -1;
		}
	}

	// send commit
	return ctx.src->commit_done(ctx.sdat);
}

