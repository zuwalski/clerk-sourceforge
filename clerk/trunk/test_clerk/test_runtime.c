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
	"ab\0defghi",		// sync, async
	0
};

static  const char objone[] = "object\0one";
static  const char prop[] = "prop";
static const char start_state[] = "start";
static const char testevent[] = "ab";

static const char fibn[] = "fib";
static const char takn[] = "tak";
static const char ackn[] = "ack";

static const char testmeth[] = 
"()"
" var :n = 11;"
" 'Ack(3,' str(:n) ') : ' str(ack(ack,3,:n)) ' / ' "
" 'Fib(' str(:n + 27) ') : ' str(fib(fib,:n + 27)) ' / ' "
" 'Tak(' str((:n - 1) * 3) ',' str((:n - 1) * 2) ',' str(:n - 1) ') : ' str(tak(tak,3 * (:n - 1), 2 * (:n - 1), :n - 1)) ' / ' "
" 'Fib(3) : ' str(fib(fib,3)) ' / ' "
" 'Tak(3,2,1) : ' str(tak(tak,3,2,1)) ' / ' ";

//"()"
//" var :n = 11;"
//" 'Ack(3,' :n ') : ' ack(3,:n) ' / ' "
//" 'Fib(' :n + 27 ') : ' fib(:n + 27) ' / ' "
//" 'Tak(' (:n - 1) * 3 ',' (:n - 1) * 2 ',' :n - 1 ') : ' tak(3 * (:n - 1), 2 * (:n - 1), :n - 1) ' / ' "
//" 'Fib(3) : ' fib(3) ' / ' "
//" 'Tak(3,2,1) : ' tak(3,2,1) ' / ' ";

static const char fib[] = 
"(:fib,:n)"
" if :n > 1 do (:fib(:fib,:n - 1)) + (:fib(:fib,:n - 2)) else 1 end";

static const char ack[] = 
"(:ack,:m,:n)"
" if :m = 0 do :n + 1 "
" elseif :n = 0 do :ack(:ack,:m - 1,1) "
" else :ack(:ack,:m - 1, :ack(:ack,:m, :n - 1)) end";

static const char tak[] = 
"(:tak,:x,:y,:z)"
" if :y < :x do "
"  :tak(:tak, :tak(:tak,:x - 1,:y,:z), :tak(:tak,:y - 1,:z,:x), :tak(:tak,:z - 1,:x,:y) )"
" else :z end";

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
	st_update(t,&pt,fib,sizeof(fib) - 1);

	tmp = name;
	st_update(t,&tmp,fibn,sizeof(fibn));

	ASSERT(cle_set_expr(t,root,objone,sizeof(objone),name,meth,&_test_pipe_stdout,0) == 0);

	// ----

	pt = meth;
	st_update(t,&pt,ack,sizeof(ack) - 1);

	tmp = name;
	st_update(t,&tmp,ackn,sizeof(ackn));

	ASSERT(cle_set_expr(t,root,objone,sizeof(objone),name,meth,&_test_pipe_stdout,0) == 0);

	// ----

	{
		st_ptr pt;
		st_empty(t,&meth);
		pt = meth;
		st_insert(t,&pt,tak,sizeof(tak) - 1);

		pt = name;
		st_update(t,&pt,takn,sizeof(takn));

		ASSERT(cle_set_expr(t,root,objone,sizeof(objone),name,meth,&_test_pipe_stdout,0) == 0);
	}

	// ----
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
				cle_end(ipt,0,0);
			}
			//else
			//	printf("event not started\n");

			//tk_drop_task(app_inst);
		}
	}
	stop = clock();

//	printf("\n\npagecount %d, overflowsize %d, resize-count %d\n",page_size,overflow_size,resize_count);
	printf("\nRuntimeTest. Time %d\n\n",stop - start);
}

