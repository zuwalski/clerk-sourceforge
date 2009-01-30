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

// the user
static char userid[] = "test";

// roles on the user
// !! ROLES are prefixed with length
static char* user_roles[] = {
	"\6role1",
	"\6role2",
	0
};

// events to test
// must all be 10 chars (or 0)
static char* test_events[] = {
	"abcdefghi",		// no handler
	"ab\0defghi",		// sync, async
	"ab\0de\0ghi",		// sync, request pipe
	"ab\0de\0g\0i",		// sync, request, response
	0
};

static  const char objone[] = "object\0one";
static  const char prop[] = "prop";
static const char start_state[] = "start";
static const char testevent[] = "ab";
static const char testmeth[] = 
"()"
" var $h = 'hello';"
" var $w = 'world';"
" $h ' ' $w str(start(5))";

static const char testmeth2[] = 
"($1)"
" if $1 > 1 do start($1 - 1) + start($1 - 2) else $1 end";

void test_runtime_c()
{
	task* t;
	st_ptr config_root,root,pt,name,eventname,meth,oid,tmp;
	// create an app_instance (mempager)
	cle_pagesource* psource = &util_memory_pager;
	cle_psrc_data pdata = util_create_mempager();

	clock_t start,stop;

	int i,total;

	// setup
	puts("\nRunning test_runtime_c\n");
	page_size = 0;
	resize_count = 0;
	overflow_size = 0;

	//  new task
	t = tk_create_task(psource,pdata);

	// format
	cle_format_instance(t);

	// build config 
	st_empty(t,&config_root);

	tk_root_ptr(t,&root);

	// allow role "role1" on "ab"
	cle_allow_role(t,root,"ab",3,"role1",6);

	st_empty(t,&name);
	pt = name;
	st_insert(t,&pt,objone,sizeof(objone));

	ASSERT(cle_new_object(t,root,name,0,0) == 0);

	pt = name;
	st_update(t,&pt,prop,sizeof(prop));

	ASSERT(cle_set_property(t,root,objone,sizeof(objone),name,name) == 0);

	pt = name;
	st_update(t,&pt,start_state,sizeof(start_state));

	// the event
	st_empty(t,&eventname);
	pt = eventname;
	st_insert(t,&pt,testevent,sizeof(testevent));

	// method body
	st_empty(t,&meth);
	pt = meth;
	st_insert(t,&pt,testmeth,sizeof(testmeth) - 1);

	// sync-handler for start-state
	ASSERT(cle_set_handler(t,root,objone,sizeof(objone),name,eventname,meth,&_test_pipe_stdout,0,SYNC_REQUEST_HANDLER) == 0);

	pt = meth;
	st_update(t,&pt,testmeth2,sizeof(testmeth2) - 1);

	ASSERT(cle_set_expr(t,root,objone,sizeof(objone),name,meth,&_test_pipe_stdout,0) == 0);

	st_empty(t,&oid);
	pt = oid;
	st_insert(t,&pt,"\1\1\0",3);

	tmp.pg = 0;
	ASSERT(cle_get_handler(t,root,oid,&pt,&tmp,testevent,sizeof(testevent),SYNC_REQUEST_HANDLER) == 0);

	_rt_dump_function(t,&pt);

	//tk_commit_task(t);

	start = clock();
	for(total = 0; total < 1; total++)
	{
		for(i = 0; test_events[i] != 0; i++)
		{
			_ipt* ipt;
			//task* app_inst = tk_create_task(psource,pdata);

			//printf("testing event: %.*s\n",10,test_events[i]);
		
			ipt = cle_start(config_root,test_events[i],10, userid, sizeof(userid), user_roles,&_test_pipe_stdout,0,t);

			if(ipt != 0)
			{
				// test w structure a {b,c}
				cle_data(ipt,"a",2);
				cle_push(ipt);
				cle_data(ipt,"b",2);
				cle_pop(ipt);
				cle_push(ipt);
				cle_data(ipt,"c",2);
				cle_pop(ipt);
				cle_next(ipt);
				cle_end(ipt,0,0);

				//printf("\n\nevent done\n");
			}
			//else
			//	printf("event not started\n");

			//tk_drop_task(app_inst);
		}
	}
	stop = clock();

	printf("\nsimple event-stream. Time %d\n\n",stop - start);
}

