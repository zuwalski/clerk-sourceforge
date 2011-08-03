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
#include "test.h"
#include "../cle_core/cle_stream.h"
#include "../cle_core/cle_object.h"
#include <stdio.h>
#include <memory.h>
#include <time.h>

// objects
const char objone[] = "object\0one";
const char objtwo[] = "object\0two";
const char objthree[] = "object\0three";

// states
const char state1[] = "state1";
const char state2[] = "state2";
const char state_start[] = "start";

const char testevent[] = "some\0event";

const char testmeth[] = "() 'hello world'";

const char prop1[] = "prop1";
const char prop2[] = "prop2";
const char propColl[] = "propC";

const char exprpath[] = "expr\0expr";

void test_instance_c()
{
	cle_instance inst;
	st_ptr root,name,pt,eventname,meth,object,empty,object1,object2,object3,propname,mobj1,mobj2;
	cle_typed_identity id1,id2;
	task* t = tk_create_task(0,0);
	ptr_list list;
	oid_str oidstr,oidstr2;
	double dbl;

	// setup
	puts("\nRunning test_instance_c\n");

	st_empty(t,&root);
	st_empty(t,&name);

	inst.t = t;
	inst.root = root;
	empty.pg = 0;

	// create object-family One <- Two <- Three
	pt = name;
	st_insert(t,&pt,objone,sizeof(objone));

	ASSERT(cle_new(inst,name,empty,0) == 0);

	ASSERT(cle_goto_object(inst,name,&object1) == 0);

	pt = name;
	st_update(t,&pt,objtwo,sizeof(objtwo));

	ASSERT(cle_new(inst,name,object1,0) == 0);

	ASSERT(cle_goto_object(inst,name,&object2) == 0);

	pt = name;
	st_update(t,&pt,objthree,sizeof(objthree));

	ASSERT(cle_new(inst,name,object2,0) == 0);

	ASSERT(cle_goto_object(inst,name,&object3) == 0);

	pt = name;
	st_update(t,&pt,objone,sizeof(objone));
	ASSERT(cle_goto_object(inst,name,&object) == 0);

	ASSERT(cle_get_oid(inst,object,&oidstr) == 0);
	ASSERT(memcmp(oidstr.chrs,"@abaaaaaaaaab",13) == 0);

	pt = name;
	st_update(t,&pt,objtwo,sizeof(objtwo));
	ASSERT(cle_goto_object(inst,name,&object) == 0);

	ASSERT(cle_get_oid(inst,object,&oidstr) == 0);
	ASSERT(memcmp(oidstr.chrs,"@abaaaaaaaaac",13) == 0);

	pt = name;
	st_update(t,&pt,objthree,sizeof(objthree));
	ASSERT(cle_goto_object(inst,name,&object) == 0);

	ASSERT(cle_get_oid(inst,object,&oidstr) == 0);
	ASSERT(memcmp(oidstr.chrs,"@abaaaaaaaaad",13) == 0);

	object = object3;
	ASSERT(cle_goto_parent(inst,&object) == 0);
	ASSERT(object2.pg == object.pg && object2.key == object.key);

	ASSERT(cle_goto_parent(inst,&object) == 0);
	ASSERT(object1.pg == object.pg && object1.key == object.key);

	ASSERT(cle_is_related_to(inst,object1,object2) != 0);

	// states
	pt = name;
	st_update(t,&pt,state1,sizeof(state1));

	ASSERT(cle_create_state(inst,object1,name) == 0);

	ASSERT(cle_create_state(inst,object2,name) == 0);

	ASSERT(cle_create_state(inst,object3,name) == 0);

	// but still owned by object 1
	pt = object3;
	ASSERT(cle_get_property_host(inst,&pt,state1,sizeof(state1)) == 2);

	pt = name;
	st_update(t,&pt,state2,sizeof(state2));

	ASSERT(cle_create_state(inst,object2,name) == 0);

//	pt = name;
//	st_update(t,&pt,state_start,sizeof(state_start));

//	ASSERT(cle_create_state(t,root,objthree,sizeof(objthree),name) != 0);

	list.link = 0;
	list.pt = name;

	// the event
	st_empty(t,&eventname);
	pt = eventname;
	st_insert(t,&pt,testevent,sizeof(testevent));

	// method body
	st_empty(t,&meth);
	pt = meth;
	st_insert(t,&pt,testmeth,sizeof(testmeth) - 1);

	// sync-handler for start-state
	ASSERT(cle_create_handler(inst,object2,eventname,meth,&list,&_test_pipe_stdout,0,SYNC_REQUEST_HANDLER) == 0);

	// sync-handler for state1
	// set state 
	pt = name;
	st_update(t,&pt,state1,sizeof(state2));

	list.pt = name;

	ASSERT(cle_create_handler(inst,object1,eventname,meth,&list,&_test_pipe_stdout,0,SYNC_REQUEST_HANDLER) == 0);

	// PROPERTIES

	st_empty(t,&propname);
	pt = propname;
	st_update(t,&pt,(cdat)prop1,sizeof(prop1));

	ASSERT(cle_create_property(inst,object1,propname,&id2.id) == 0);

	pt = object1;
	ASSERT(cle_get_property_host(inst,&pt,(cdat)prop1,sizeof(prop1)) == 0);

	ASSERT(cle_probe_identity(inst,&pt,&id1) == 0);

	ASSERT(id1.type == TYPE_ANY);

	ASSERT(id1.id == id2.id);

	ASSERT(cle_set_property_num(inst,object1,id1.id,5) == 0);

	ASSERT(cle_get_property_num(inst,object1,id1.id,&dbl) == 0);

	ASSERT(dbl == 5);

	ASSERT(cle_get_property_num(inst,object3,id1.id,&dbl) == 0);

	ASSERT(dbl == 5);

	ASSERT(cle_set_property_num(inst,object2,id1.id,10) == 0);

	ASSERT(cle_get_property_num(inst,object2,id1.id,&dbl) == 0);

	ASSERT(dbl == 10);

	ASSERT(cle_get_property_num(inst,object3,id1.id,&dbl) == 0);

	ASSERT(dbl == 10);

	ASSERT(cle_get_property_type(inst,object1,id1.id) == TYPE_NUM);

	// PERSISTENCE

	cle_new_mem(inst.t,object1,&object);

	ASSERT(cle_get_oid(inst,object,&oidstr) != 0);

	cle_new_mem(inst.t,object,&mobj1);

	ASSERT(cle_get_oid(inst,mobj1,&oidstr) != 0);

	cle_new_mem(inst.t,object2,&mobj2);

	ASSERT(cle_get_oid(inst,mobj2,&oidstr) != 0);

	// mobj to mobj doesn't persist
	ASSERT(cle_set_property_ref(inst,object,id1.id,mobj1) == 0);

	ASSERT(cle_get_oid(inst,object,&oidstr) != 0);

	ASSERT(cle_get_oid(inst,mobj1,&oidstr) != 0);

	ASSERT(cle_get_property_ref(inst,object,id1.id,&pt) == 0);

	ASSERT(pt.pg == mobj1.pg && pt.key == mobj1.key && pt.offset == mobj1.offset);
	
	// make object persistent by ass. to object1

	ASSERT(cle_set_property_ref(inst,object1,id1.id,object) == 0);

	ASSERT(cle_get_oid(inst,mobj1,&oidstr) == 0);

	ASSERT(cle_get_oid(inst,object,&oidstr) == 0);

	ASSERT(cle_get_property_ref(inst,object1,id1.id,&pt) == 0);

	ASSERT(cle_get_oid(inst,pt,&oidstr2) == 0);

	// same id - but not same ptr
	ASSERT(memcmp(&oidstr,&oidstr2,sizeof(oidstr)) == 0);

	// COLLECTIONS
	{
		// can not override other types with collection
		ASSERT(cle_collection_add_object(inst,object1,id1.id,object1));

		st_empty(t,&propname);
		pt = propname;
		st_update(t,&pt,(cdat)propColl,sizeof(propColl));

		ASSERT(cle_create_property(inst,object1,propname,&id2.id) == 0);

		ASSERT(cle_collection_add_object(inst,object1,id2.id,object1) == 0);
		ASSERT(cle_collection_add_object(inst,object1,id2.id,object2) == 0);

		// are they in
		ASSERT(cle_collection_test_object(inst,object1,id2.id,object1));
		ASSERT(cle_collection_test_object(inst,object1,id2.id,object2));
		// object3 not in
		ASSERT(cle_collection_test_object(inst,object1,id2.id,object3) == 0);

		// delete 2
		ASSERT(cle_collection_remove_object(inst,object1,id2.id,object2) == 0);

		ASSERT(cle_collection_test_object(inst,object1,id2.id,object1));
		ASSERT(cle_collection_test_object(inst,object1,id2.id,object2) == 0);
	}

	// EXPRS
	pt = name;
	st_update(t,&pt,exprpath,sizeof(exprpath));

	ASSERT(cle_create_expr(inst,object1,name,meth,&_test_pipe_stdout,0) == 0);

	tk_drop_task(t);
}
