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
	Creating instances and managing content
*/

#ifndef __CLE_OBJECT_H__
#define __CLE_OBJECT_H__

#include "cle_clerk.h"

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

#define ISMEMOBJ(header) (((objheader)(header)).zero == 0)
// DEV-hdr: (next)identity
// ... followed by optional name

#define PROPERTY_SIZE (sizeof(identity))

typedef struct
{
	task*  t;
	st_ptr root;
}
cle_instance;

/* object-store */
int cle_new(task* app_instance, st_ptr app_root, st_ptr name, st_ptr* obj, cdat extends_name, uint exname_length);
int cle_new_mem(task* app_instance, st_ptr* newobj, st_ptr extends);

int cle_goto_object(task* t, st_ptr* root, cdat name, uint name_length);

int cle_set_expr(task* app_instance, st_ptr app_root, cdat object_name, uint object_length, st_ptr path, st_ptr expr, cle_pipe* response, void* data);

int cle_get_property_host(task* app_instance, st_ptr root, st_ptr* object, cdat propname, uint name_length);

int cle_get_property_host_st(task* app_instance, st_ptr root, st_ptr* object, st_ptr propname);

int cle_get_property(task* app_instance, st_ptr root, cdat object_name, uint object_length, st_ptr path, st_ptr* prop);

int cle_set_property(task* app_instance, st_ptr root, cdat object_name, uint object_length, st_ptr path);

int cle_create_state(task* app_instance, st_ptr app_root, cdat object_name, uint object_length, st_ptr state);

int cle_set_state(task* app_instance, st_ptr root, cdat object_name, uint object_length, st_ptr state);

int cle_set_handler(task* app_instance, st_ptr app_root, cdat object_name, uint object_length, st_ptr state, st_ptr eventname, st_ptr meth, cle_pipe* response, void* data, enum handler_type type);

int cle_get_handler(task* app_instance, st_ptr root, st_ptr oid, st_ptr* handler, st_ptr* object, cdat eventid, uint eventid_length, enum handler_type type);

int cle_get_target(task* app_instance, st_ptr root, st_ptr* object, cdat target_oid, uint target_oid_length);

int cle_get_oid(task* app_instance, st_ptr object, char* buffer, int buffersize);

#endif