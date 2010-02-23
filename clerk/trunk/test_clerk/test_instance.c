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

const char testmeth[] = "() 'hallo world'";

const char prop1[] = "prop1";
const char prop2[] = "prop2";

const char exprpath[] = "expr\0expr";

void test_instance_c()
{
	cle_instance inst;
	st_ptr root,name,pt,eventname,meth,oid,handler,object,empty,object1,object2,object3;
	task* t = tk_create_task(0,0);
	char buffer[100];
	ptr_list list;
	cle_handler href;

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

	ASSERT(cle_get_oid(inst,object,buffer,sizeof(buffer)) == 0);
	ASSERT(memcmp(buffer,"@abaaaaaaaaab",13) == 0);

	pt = name;
	st_update(t,&pt,objtwo,sizeof(objtwo));
	ASSERT(cle_goto_object(inst,name,&object) == 0);

	ASSERT(cle_get_oid(inst,object,buffer,sizeof(buffer)) == 0);
	ASSERT(memcmp(buffer,"@abaaaaaaaaac",13) == 0);

	pt = name;
	st_update(t,&pt,objthree,sizeof(objthree));
	ASSERT(cle_goto_object(inst,name,&object) == 0);

	ASSERT(cle_get_oid(inst,object,buffer,sizeof(buffer)) == 0);
	ASSERT(memcmp(buffer,"@abaaaaaaaaad",13) == 0);

	// states
	pt = name;
	st_update(t,&pt,state1,sizeof(state1));

	ASSERT(cle_create_state(inst,object1,name) == 0);

	ASSERT(cle_create_state(inst,object2,name) == 0);

	ASSERT(cle_create_state(inst,object3,name) == 0);

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
	ASSERT(cle_create_handler(inst,object1,eventname,meth,&list,&_test_pipe_stdout,0,SYNC_REQUEST_HANDLER) == 0);

	// sync-handler for state2
	// set state 
	pt = name;
	st_update(t,&pt,state2,sizeof(state2));

	list.pt = name;

	ASSERT(cle_create_handler(inst,object1,eventname,meth,&list,&_test_pipe_stdout,0,SYNC_REQUEST_HANDLER) == 0);
/*
	pt = oid;
	st_update(t,&pt,"\1\2\0",3);

	object.pg = 0;
	// objtwo does not handle testevent (not in correct state)
	ASSERT(cle_get_handler(t,root,oid,&handler,&object,testevent,sizeof(testevent),SYNC_REQUEST_HANDLER) < 0);

	// set state
	ASSERT(cle_set_state(t,root,objtwo,sizeof(objtwo),name) == 0);

	// objtwo now handles the event...
	ASSERT(cle_get_handler(t,root,oid,&handler,&object,testevent,sizeof(testevent),SYNC_REQUEST_HANDLER) == 0);

	// property-testing
	pt = name;
	st_update(t,&pt,prop1,sizeof(prop1));

	// property-1 to objone
	ASSERT(cle_set_property(t,root,objone,sizeof(objone),name) == 0);

	pt = name;
	st_update(t,&pt,prop2,sizeof(prop2));

	// property-2 to objtwo
	ASSERT(cle_set_property(t,root,objtwo,sizeof(objtwo),name) == 0);

	// property-2 not found in objone
	ASSERT(cle_get_property(t,root,objone,sizeof(objone),name,&object) != 0);

	// property-2 found in objtwo
	ASSERT(cle_get_property(t,root,objtwo,sizeof(objtwo),name,&object) == 0);

	// value "prop2"
	//ASSERT(st_get(t,&object,buffer,sizeof(buffer)) == sizeof(prop2));
	//ASSERT(memcmp(prop2,buffer,sizeof(prop2)) == 0);

	// property-2 found in objthree
	ASSERT(cle_get_property(t,root,objthree,sizeof(objthree),name,&object) == 0);

	// value "prop2"
	//ASSERT(st_get(t,&object,buffer,sizeof(buffer)) == sizeof(prop2));
	//ASSERT(memcmp(prop2,buffer,sizeof(prop2)) == 0);

	pt = name;
	st_update(t,&pt,prop1,sizeof(prop1));

	// property-1 also found in objthree
	ASSERT(cle_get_property(t,root,objthree,sizeof(objthree),name,&object) == 0);

	// value "prop1"
	//ASSERT(st_get(t,&object,buffer,sizeof(buffer)) == sizeof(prop1));
	//ASSERT(memcmp(prop1,buffer,sizeof(prop1)) == 0);

	// property-1 found in objone
	ASSERT(cle_get_property(t,root,objone,sizeof(objone),name,&object) == 0);

	// value "prop1"
	//ASSERT(st_get(t,&object,buffer,sizeof(buffer)) == sizeof(prop1));
	//ASSERT(memcmp(prop1,buffer,sizeof(prop1)) == 0);
*/
	// testing Expr's
	pt = name;
	st_update(t,&pt,exprpath,sizeof(exprpath));

	ASSERT(cle_create_expr(inst,object1,name,meth,&_test_pipe_stdout,0) == 0);

	tk_drop_task(t);
}

