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
#include <time.h>


//////////////////////////////////

static const cle_pipe_inst pipe = {&_test_pipe_stdout, 0};
static const st_ptr empty = {0,0,0};

void test_stream_c2() {
	cle_psrc_data store = util_create_mempager();
	task* t = tk_create_task(&util_memory_pager, store);
	cle_instance inst = {t, root(t)};
	
	st_ptr eventid = str(t, "event");
	st_ptr config = str(t, "");
	st_ptr userid = str(t, "");
	st_ptr user_roles = str(t, "");
	
	_ipt* ipt = cle_start2(t, config, eventid, userid, user_roles, pipe);
	
	ASSERT(ipt == 0);
	
	cle_new(inst, eventid, empty, 0);
	
	tk_commit_task(t);
	
	t = tk_create_task(&util_memory_pager, store);
	
	ipt = cle_start2(t, config, eventid, userid, user_roles, pipe);
	
	ASSERT(ipt);
	
	tk_drop_task(t);
}


/////////////////


// testhandler w event_handler* argument
static void _start(event_handler* v)
{
	//char buffer[50];
	//int size = sprintf_s(buffer,sizeof(buffer)," - start: %p - ",v->thehandler);
	v->response->start(v->respdata);

//	v->response->push(v->respdata);
	//v->response->data(v->respdata,buffer,size);
//	v->response->pop(v->respdata);
}
static void _next(event_handler* v)
{
	//char buffer[50];
	//int size = sprintf_s(buffer,sizeof(buffer)," - next: %p - ",v->thehandler);
//	v->response->push(v->respdata);
	//v->response->data(v->respdata,buffer,size);
//	v->response->pop(v->respdata);
	v->response->next(v->respdata);
}
static void _end(event_handler* v,cdat c,uint u)
{
	//char buffer[50];
	//int size = sprintf_s(buffer,sizeof(buffer)," - end: %p - ",v->thehandler);
//	v->response->push(v->respdata);
	//v->response->data(v->respdata,buffer,size);
//	v->response->pop(v->respdata);
	v->response->end(v->respdata,c,u);
}
static void _pop(event_handler* v)
{
	//char buffer[50];
	//int size = sprintf_s(buffer,sizeof(buffer)," - pop: %p - ",v->thehandler);
//	v->response->push(v->respdata);
	//v->response->data(v->respdata,buffer,size);
//	v->response->pop(v->respdata);
	v->response->pop(v->respdata);
}
static void _push(event_handler* v)
{
	//char buffer[50];
	//int size = sprintf_s(buffer,sizeof(buffer)," - push: %p - ",v->thehandler);
//	v->response->push(v->respdata);
	//v->response->data(v->respdata,buffer,size);
//	v->response->pop(v->respdata);
	v->response->push(v->respdata);
}
static void _data(event_handler* v,cdat c,uint u)
{
	v->response->data(v->respdata,c,u);
}
static void _submit(event_handler* v,task* t,st_ptr* st)
{
	//char buffer[50];
	//int size = sprintf_s(buffer,sizeof(buffer)," - submit: %p - ",v->thehandler);
//	v->response->push(v->respdata);
	//v->response->data(v->respdata,buffer,size);
//	v->response->pop(v->respdata);
	v->response->submit(v->respdata,t,st);
}

// testhandler w any argument
static void _start2(event_handler* v)
{
//	printf(" + start2: ");
}
static void _next2(event_handler* v)
{
//	printf(" + next2: ");
}
static void _end2(event_handler* v,cdat c,uint u)
{
//	printf(" + end2: %s %d \n",c,u);
}
static void _pop2(event_handler* v)
{
//	printf(" + pop2: ");
}
static void _push2(event_handler* v)
{
//	printf(" + push2: ");
}
static void _data2(event_handler* v,cdat c,uint u)
{
//	printf("%.*s",u,c);
}
static void _submit2(event_handler* v,task* t,st_ptr* st)
{
//	printf(" + submit2: ");
}

// defs
static cle_pipe _test_pipe = {_start,_next,_end,_pop,_push,_data,_submit};
static cle_pipe _test_pipe2 = {_start2,_next2,_end2,_pop2,_push2,_data2,_submit2};

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


void test_stream_c()
{
	task* t;
	st_ptr config_root,root;
	// create an app_instance (mempager)
	cle_pagesource* psource = &util_memory_pager;
	cle_psrc_data pdata = util_create_mempager();

	cle_syshandler handler1,handler3,handler4;

	clock_t start,stop;

	int i,total;

	// setup
	puts("\nRunning test_stream_c\n");

	handler1.input = _test_pipe;
	handler1.systype = SYNC_REQUEST_HANDLER;

	handler3.input = _test_pipe;
	handler3.systype = PIPELINE_REQUEST;

	handler4.input = _test_pipe;
	handler4.systype = PIPELINE_RESPONSE;

	//  new task
	t = tk_create_task(psource,pdata);

	// format
	//cle_format_instance(t);

	// build config 
	st_empty(t,&config_root);

	// dummy-sync handler for "abcdefghi"
	//cle_add_sys_handler(t,config_root,"abcdefghi",10,&handler1);

	// sync handler for "ab*"
	cle_add_sys_handler(t,config_root,"ab",3,&handler1);

	// request-pipe-handler for "ab\0de*"
	cle_add_sys_handler(t,config_root,"ab\0de",6,&handler3);

	// response-pipe-handler for "ab\0de\0g\0i*"
	cle_add_sys_handler(t,config_root,"ab\0de\0g\0i",10,&handler4);

	printf("handler1 sync: %p\n",&handler1);
	printf("handler3 req: %p\n",&handler3);
	printf("handler4 resp: %p\n",&handler4);

	tk_root_ptr(t,&root);

	// allow role "role1" on "ab"
	cle_allow_role(t,root,"ab",3,"role2",6);

	// allow role "roleX" on "abcdefghi"
	cle_allow_role(t,root,"abcdefghi",10,"roleX",6);

	start = clock();
	for(total = 0; total < HIGH_ITERATION_COUNT; total++)
	{
		for(i = 0; test_events[i] != 0; i++)
		{
			_ipt* ipt;
			task* app_inst = tk_clone_task(t);
			st_ptr dummy_user, dummy_userroles,eventname, pt;
			dummy_user.pg = 0;
			dummy_userroles.pg = 0;

			//printf("testing event: %.*s\n",10,test_events[i]);
	
			st_empty(app_inst,&eventname);
			pt = eventname;
			st_insert(app_inst,&pt,test_events[i],10);

			//FIXME
			//ipt = cle_start(app_inst,config_root,eventname,dummy_user,dummy_userroles,&_test_pipe2,0);

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

			tk_drop_task(app_inst);
		}
	}
	stop = clock();

	printf("\nsimple event-stream. Time %d\n\n",stop - start);

	tk_drop_task(t);
}



