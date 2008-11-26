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
#include "../cle_core/cle_instance.h"
#include <stdio.h>
#include <time.h>

const char objone[] = "object\0one";
const char objtwo[] = "object\0two";
const char objthree[] = "object\0three";

void test_instance_c()
{
	st_ptr root,name,pt;
	task* t = tk_create_task(0,0);
	char buffer[100];

	// setup
	puts("\nRunning test_instance_c\n");
	page_size = 0;
	resize_count = 0;
	overflow_size = 0;

	st_empty(t,&root);
	st_empty(t,&name);

	pt = name;
	st_insert(t,&pt,objone,sizeof(objone));

	ASSERT(cle_new_object(t,root,name,0,0) == 0);

	pt = name;
	st_update(t,&pt,objtwo,sizeof(objtwo));

	ASSERT(cle_new(t,root,objone,sizeof(objone),name,0) == 0);

	pt = root;
	ASSERT(cle_goto_object(t,&pt,objone,sizeof(objone)) == 0);

	ASSERT(cle_get_oid(t,pt,buffer,sizeof(buffer)) == 0);
	puts(buffer);

	pt = root;
	ASSERT(cle_goto_object(t,&pt,objtwo,sizeof(objtwo)) == 0);

	ASSERT(cle_get_oid(t,pt,buffer,sizeof(buffer)) == 0);
	puts(buffer);

	tk_drop_task(t);
}

