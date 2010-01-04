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

/*
	Creating instances and managing content
*/

#ifndef __CLE_OBJECT_H__
#define __CLE_OBJECT_H__

#include "cle_clerk.h"
#include "cle_stream.h"

//TODO - root headers have size 3 (\0\0[Identifier])
//object-headers have size 1 ([Identifier])
#define HEAD_SIZE 2
#define HEAD_EVENT "\0e"
#define HEAD_HANDLER "\0h"
#define HEAD_ROLES "\0r"
#define HEAD_OID "\0O"
#define HEAD_NAMES "\0o"
#define HEAD_DEV "\0D"
//#define HEAD_EXTENDS "\0x"
#define HEAD_STATES "\0s"
#define HEAD_STATE_NAMES "\0z"
#define HEAD_PROPERTY "\0y"

#define HEAD_METHOD "\0M"
#define HEAD_EXPR "\0E"
#define HEAD_NUM "\0N"
#define HEAD_REF "\0R"
#define HEAD_COLLECTION "\0C"
#define HEAD_REF_MEM "\0r"


#define OID_HIGH_SIZE 4
typedef struct
{
	segment _low;
	uchar   _high[OID_HIGH_SIZE];
} oid;

// Bit
// [31-22]	Level (0 == system/object)
// [21-0]	Runningnumber (starting from 1)
typedef ulong identity;
// identity == 0 => not valid!

#define IDLEVEL(id)  ((id) >> 22)
#define IDNUMBER(id) ((id) & 0x3FFFFF)
#define IDMAKE(level,number) (((level) << 22) | IDNUMBER(number))

// later: mark-sweep of persisten objects. Flip first bit from mark to mark.
#define IDMARK(id) ((id) & 1)

typedef struct
{
	oid       id;
	oid       ext;
	identity  state;
}
objectheader2;
// followed by object-content

typedef union
{
	struct
	{
		segment zero;
		st_ptr  ext;
	};
	objectheader2 obj;
}
objheader;

#define ISMEMOBJ(header) ((header).zero == 0)
// DEV-hdr: (next)identity
// ... followed by optional name

#define PROPERTY_SIZE (sizeof(identity))

typedef struct
{
	oid      oid;
	identity handler;
	uchar    type;
}
cle_handler;

int cle_new(cle_instance inst, st_ptr name, st_ptr extends, st_ptr* obj);

int cle_new_mem(task* app_instance, st_ptr* extends, st_ptr* newobj);

int cle_goto_object(cle_instance inst, st_str name, st_ptr* obj);

int cle_get_oid(cle_instance inst, st_ptr obj, char* buffer, int buffersize);

int cle_goto_parent(cle_instance inst, st_ptr* child);

int cle_is_related_to(cle_instance inst, st_ptr parent, st_ptr child);

int cle_persist_object(cle_instance inst, st_ptr* obj);

int cle_delete_name(cle_instance inst, st_str name);

int cle_create_state(cle_instance inst, st_ptr obj, st_ptr newstate);

int cle_set_state(cle_instance inst, st_ptr obj, st_ptr state);

int cle_create_property(cle_instance inst, st_ptr obj, st_ptr propertyname);

int cle_create_expr(cle_instance inst, st_ptr obj, st_ptr path, st_ptr expr, cle_pipe* response, void* data);

int cle_create_handler(cle_instance inst, st_ptr obj, st_ptr eventname, st_ptr expr, ptr_list* states, cle_pipe* response, void* data, enum handler_type type);

int cle_get_handler(cle_instance inst, cle_handler href, st_ptr* obj, st_ptr* handler);

int cle_get_property_host(cle_instance inst, st_ptr* obj, st_str propname);

int cle_get_property_host_st(cle_instance inst, st_ptr* obj, st_ptr propname);


#endif
