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
	TEST-SUITE RUNNER
*/

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
//#include <malloc.h>
#include <memory.h>
#include <errno.h>
#include <string.h>
#include "test.h"

void heap_check()
{
/*
int heapstatus = _heapchk();
   switch( heapstatus )
   {
   case _HEAPOK:
      printf(" OK - heap is fine\n" );
      break;
   case _HEAPEMPTY:
      printf(" OK - heap is empty\n" );
      break;
   case _HEAPBADBEGIN:
      printf( "ERROR - bad start of heap\n" );
      break;
   case _HEAPBADNODE:
      printf( "ERROR - bad node in heap\n" );
      break;
   default:
      printf( "ERROR - other\n" );
   }
*/
}

void cle_panic(task* t)
{
	puts("failed in cle_panic in test_main.c");
	heap_check();
	getchar();
	exit(-1);
}

void cle_notify_start(event_handler* handler)
{
	while(handler != 0)
	{
		handler->thehandler->input.start(handler);
		handler = handler->next;
	}
}

void cle_notify_next(event_handler* handler)
{
	while(handler != 0)
	{
		handler->thehandler->input.next(handler);
		handler = handler->next;
	}
}

void cle_notify_end(event_handler* handler, cdat msg, uint msglength)
{
	while(handler != 0)
	{
		handler->thehandler->input.end(handler,msg,msglength);
		handler = handler->next;
	}
}

uint page_size = 0;
uint resize_count = 0;
uint overflow_size = 0;

char test1[] = "test1";
char test1x2[] = "test1\0test1";
char test2[] = "t1set";
char test3[] = "t2set";
char test2x2[] = "t1set\0t1set";
char test2_3[] = "t1set\0t2set";

const char testdbfilename[] = "testdb.dat";

void test_struct_c()
{
	st_ptr root,tmp,tmp2;
	task* t;

	page_size = 0;
	resize_count = 0;
	overflow_size = 0;

	//  new task
	t = tk_create_task(0,0);

	// should not happen.. but
	ASSERT(t);

	// create empty node no prob
	ASSERT(st_empty(t,&root) == 0);

	// we now have an empty node
	ASSERT(st_is_empty(&root));

	// insert single value
	tmp = root;
	ASSERT(st_insert(t,&tmp,test1,sizeof(test1)));

	// collection not empy anymore
	ASSERT(st_is_empty(&root) == 0);

	// value can be found again
	ASSERT(st_exsist(t,&root,test1,sizeof(test1)));

	// and some other random values can not be found
	ASSERT(st_exsist(t,&root,test2,sizeof(test2)) == 0);

	ASSERT(st_exsist(t,&root,test3,sizeof(test3)) == 0);

	// can move pointer there (same as exsist) no prob
	tmp = root;
	ASSERT(st_move(t,&tmp,test1,sizeof(test1)) == 0);

	// now tmp points to an empty node
	ASSERT(st_is_empty(&tmp));

	// insert same string again
	ASSERT(st_insert(t,&tmp,test1,sizeof(test1)));

	// we can still find first part alone
	ASSERT(st_exsist(t,&root,test1,sizeof(test1)));

	// we can find the combined string
	ASSERT(st_exsist(t,&root,test1x2,sizeof(test1x2)));

	// we can move half way and find rest
	tmp = root;
	ASSERT(st_move(t,&tmp,test1,sizeof(test1)) == 0);

	ASSERT(st_exsist(t,&tmp,test1,sizeof(test1)));

	// we can move all the way and find and empty node
	tmp = root;
	ASSERT(st_move(t,&tmp,test1x2,sizeof(test1x2)) == 0);

	ASSERT(st_is_empty(&tmp));

	// put 2 more root-values in
	tmp = root;
	ASSERT(st_insert(t,&tmp,test2,sizeof(test2)));

	tmp = root;
	ASSERT(st_insert(t,&tmp,test3,sizeof(test3)));

	// we can find all 3 distinct values
	ASSERT(st_exsist(t,&root,test1,sizeof(test1)));

	ASSERT(st_exsist(t,&root,test2,sizeof(test2)));

	ASSERT(st_exsist(t,&root,test3,sizeof(test3)));

	// and we can still find the combined string
	ASSERT(st_exsist(t,&root,test1x2,sizeof(test1x2)));

	// we can move to one of the new values and start a new collection there
	tmp = root;
	ASSERT(st_move(t,&tmp,test2,sizeof(test2)) == 0);

	tmp2 = tmp;
	ASSERT(st_insert(t,&tmp2,test2,sizeof(test2)));

	tmp2 = tmp;
	ASSERT(st_insert(t,&tmp2,test3,sizeof(test3)));

	// we can find them relative to tmp ..
	ASSERT(st_exsist(t,&tmp,test2,sizeof(test2)));

	ASSERT(st_exsist(t,&tmp,test3,sizeof(test3)));

	// .. and from root
	ASSERT(st_exsist(t,&root,test2_3,sizeof(test2_3)));

	ASSERT(st_exsist(t,&root,test2x2,sizeof(test2x2)));

	// .. and the old one
	ASSERT(st_exsist(t,&root,test1x2,sizeof(test1x2)));

	// Test copy-move, insert compare
	ASSERT(st_empty(t,&root) == 0);

	tmp = root;
	ASSERT(st_insert(t,&tmp,test1,sizeof(test1)));

	ASSERT(st_empty(t,&tmp) == 0);

	tmp2 = tmp;
	ASSERT(st_insert(t,&tmp2,test1,sizeof(test1)));

	tmp2 = root;
	ASSERT(st_move_st(t,&tmp2,&tmp) == 0);

	ASSERT(st_insert(t,&tmp2,test1,sizeof(test1)));

	tmp2 = tmp;
	st_update(t,&tmp2,test1x2,sizeof(test1x2));

	tmp2 = root;
	ASSERT(st_move_st(t,&tmp2,&tmp) == 0);

	root = tmp2;
	ASSERT(st_insert_st(t,&root,&tmp) == 0);

	ASSERT(st_move(t,&tmp2,test1x2,sizeof(test1x2)) == 0);

	// Testing st_delete
	st_empty(t,&root);

	tmp = root;
	st_insert(t,&tmp,"1234",4);

	ASSERT(st_exsist(t,&root,"1245",4) == 0);

	tmp = root;
	st_insert(t,&tmp,"1245",4);

	st_delete(t,&root,"1234",4);

	ASSERT(st_exsist(t,&root,"12",2));
	ASSERT(st_exsist(t,&root,"1245",4));
	ASSERT(st_exsist(t,&root,"1234",4) == 0);

	st_delete(t,&root,"12",2);

	ASSERT(st_exsist(t,&root,"12",2) == 0);
	ASSERT(st_exsist(t,&root,"1245",4) == 0);

	ASSERT(st_is_empty(&root));

	tmp = root;
	st_insert(t,&tmp,"1234567",7);

	ASSERT(st_exsist(t,&root,"1234567",7));

	st_delete(t,&root,"1",1);

	ASSERT(st_is_empty(&root));

	tmp = root;
	st_insert(t,&tmp,"1234567",7);

	tmp = root;
	st_insert(t,&tmp,"1233333",7);

	tmp = root;
	st_insert(t,&tmp,"1235555",7);

	tmp = root;
	st_insert(t,&tmp,"123456789",9);

	st_delete(t,&root,"123456789",9);

	ASSERT(st_exsist(t,&root,"1233333",7));
	ASSERT(st_exsist(t,&root,"1235555",7));
	ASSERT(st_exsist(t,&root,"1234567",7) == 0);
	ASSERT(st_exsist(t,&root,"123456789",9) == 0);

	st_delete(t,&root,"1233333",7);

	ASSERT(st_exsist(t,&root,"1233333",7) == 0);
	ASSERT(st_exsist(t,&root,"1235555",7));

	st_delete(t,&root,"1235555",7);

	ASSERT(st_exsist(t,&root,"1235555",7) == 0);
	ASSERT(st_is_empty(&root));

	tmp = root;
	st_insert(t,&tmp,"1234567",7);

	tmp = root;
	st_insert(t,&tmp,"1233333",7);

	tmp = root;
	st_insert(t,&tmp,"1235555",7);

	st_delete(t,&root,"1235555",7);

	ASSERT(st_exsist(t,&root,"1233333",7));
	ASSERT(st_exsist(t,&root,"1234567",7));
	ASSERT(st_exsist(t,&root,"1235555",7) == 0);

	tk_drop_task(t);
}

void test_iterate_c()
{
	clock_t start,stop;

	task* t;
	st_ptr root,tmp;
	it_ptr it;
	int i;

	page_size = 0;
	resize_count = 0;
	overflow_size = 0;

	//  new task
	t = tk_create_task(0,0);

	// should not happen.. but
	ASSERT(t);

	// create empty node no prob
	ASSERT(st_empty(t,&root) == 0);

	// create
	it_create(t,&it,&root);

	// insert data
	start = clock();
	for(i = 0; i < HIGH_ITERATION_COUNT; i++)
	{
		if(it_new(t,&it,&tmp))
			break;
	}
	stop = clock();

	printf("it_new %d items. Time %d\n",i, stop - start);

	ASSERT(i == HIGH_ITERATION_COUNT);

	// run up and down
	it_reset(&it);
	i = 0;

	start = clock();
	while(it_next(t,0,&it,0))
	{
		i++;
	}
	stop = clock();

	printf("it_next %d items. Time %d\n",i, stop - start);

	ASSERT(i == HIGH_ITERATION_COUNT);

	it_reset(&it);
	i = 0;

	start = clock();
	while(it_prev(t,0,&it,0))
	{
		i++;
	}
	stop = clock();

	printf("it_prev %d items. Time %d\n",i, stop - start);

	ASSERT(i == HIGH_ITERATION_COUNT);

	// destroy
	it_dispose(t,&it);

	printf("pagecount %d, overflowsize %d, resize-count %d\n",page_size,overflow_size,resize_count);

	tk_drop_task(t);
}

void test_iterate_fixedlength()
{
	clock_t start,stop;

	task* t;
	st_ptr root,tmp;
	it_ptr it;
	int i;

	//  new task
	t = tk_create_task(0,0);

	// should not happen.. but
	ASSERT(t);

	// create empty node no prob
	ASSERT(st_empty(t,&root) == 0);

	// insert data
	start = clock();
	for(i = 0; i < HIGH_ITERATION_COUNT; i++)
	{
		tmp = root;
		st_insert(t,&tmp,(cdat)&i,sizeof(int));
	}
	stop = clock();

	printf("insert %d items. Time %d\n",i, stop - start);

	// run up and down
	// create
	it_create(t,&it,&root);
	i = 0;

	start = clock();
	while(it_next(t,&tmp,&it,sizeof(int)))
	{
		i++;
	}
	stop = clock();

	printf("it_next[FIXED] %d items. Time %d\n",i, stop - start);

	ASSERT(i == HIGH_ITERATION_COUNT);

	it_reset(&it);
	i = 0;

	start = clock();
	while(it_prev(t,&tmp,&it,sizeof(int)))
	{
		i++;
	}
	stop = clock();

	printf("it_prev[FIXED] %d items. Time %d\n",i, stop - start);

	ASSERT(i == HIGH_ITERATION_COUNT);

	// destroy
	it_dispose(t,&it);

	tk_drop_task(t);
}

void time_struct_c()
{
	clock_t start,stop;

	st_ptr root,tmp;
	task* t;

	int counter,notfound;

	page_size = 0;
	resize_count = 0;
	overflow_size = 0;

	//  new task
	t = tk_create_task(0,0);

	// should not happen.. but
	ASSERT(t);

	// create empty node no prob
	ASSERT(st_empty(t,&root) == 0);

	ASSERT(st_is_empty(&root));

	// insert alot
	start = clock();
	for(counter = 1; counter <= HIGH_ITERATION_COUNT; counter++)
	{
		tmp = root;
		st_insert(t,&tmp,(cdat)&counter,sizeof(counter));
	}
	stop = clock();

	printf("insert %d items. Time %d\n",HIGH_ITERATION_COUNT, stop - start);

	// find them all
	notfound = 0;
	start = clock();
	for(counter = 1; counter <= HIGH_ITERATION_COUNT; counter++)
	{
		if(st_exsist(t,&root,(cdat)&counter,sizeof(counter)) == 0)
			notfound++;
	}
	stop = clock();

	ASSERT(notfound == 0);

	printf("exsist %d items. Time %d\n",HIGH_ITERATION_COUNT, stop - start);

	// delete all items
	start = clock();
	for(counter = 1; counter <= HIGH_ITERATION_COUNT; counter++)
	{
		st_delete(t,&root,(cdat)&counter,sizeof(counter));
	}
	stop = clock();

	printf("delete %d items. Time %d\n",HIGH_ITERATION_COUNT, stop - start);

	// collection now empty again
	ASSERT(st_is_empty(&root));

	notfound = 0;
	start = clock();
	for(counter = 1; counter <= HIGH_ITERATION_COUNT; counter++)
	{
		if(st_exsist(t,&root,(cdat)&counter,sizeof(counter)) == 0)
			notfound++;
	}
	stop = clock();

	ASSERT(notfound == HIGH_ITERATION_COUNT);

	printf("exsist (empty) %d items. Time %d\n",HIGH_ITERATION_COUNT, stop - start);

	printf("pagecount %d, overflowsize %d, resize-count %d\n",page_size,overflow_size,resize_count);
	tk_drop_task(t);
}

void test_task_c()
{
	clock_t start,stop;

	st_ptr root,tmp;
	it_ptr it;
	task* t;
	int i;
	uchar keystore[100];

	cle_pagesource* psource = &util_memory_pager;
	cle_psrc_data pdata = util_create_mempager();

	puts("\nRunning mempager\n");
	page_size = 0;
	resize_count = 0;
	overflow_size = 0;

	//  new task
	t = tk_create_task(psource,pdata);

	// should not happen.. but
	ASSERT(t);

	// set pagesource-root
	tk_root_ptr(t,&root);

	// create
	it_create(t,&it,&root);

	// insert data
	start = clock();
	for(i = 0; i < HIGH_ITERATION_COUNT; i++)
	{
		if(it_new(t,&it,&tmp))
			break;
	}
	stop = clock();

	printf("(pre-commit)it_new. Time %d\n",stop - start);

	it_reset(&it);

	i = 0;
	keystore[0] = 0;
	start = clock();
	while(it_next(t,0,&it,0))
	{
		uint klen = sim_new(keystore,sizeof(keystore));
		i++;
		if(i > HIGH_ITERATION_COUNT || memcmp(keystore,it.kdata,klen) != 0)
			break;
	}
	stop = clock();

	// should have same count
	ASSERT(i == HIGH_ITERATION_COUNT);

	printf("(pre-commit)it_next. Time %d\n",stop - start);

	// destroy
	it_dispose(t,&it);

	st_prt_distribution(&root,t);

	// commit!
	start = clock();
	tk_commit_task(t);
	stop = clock();

	printf("mempager: tk_commit_task. Time %d - pages %d\n",stop - start,mempager_get_pagecount(pdata));

	// new task, same source
	t = tk_create_task(psource,pdata);

	// set pagesource-root
	tk_root_ptr(t,&root);

	st_prt_distribution(&root,t);

	// read back collection
	it_create(t,&it,&root);

	keystore[0] = 0;
	start = clock();
	for(i = 0; i < HIGH_ITERATION_COUNT; i++)
	{
		uint klen = sim_new(keystore,sizeof(keystore));
		if(st_exsist(t,&root,keystore,klen) == 0)
			break;
	}
	stop = clock();

	// should have same count
	ASSERT(i == HIGH_ITERATION_COUNT);

	printf("(commit)st_exsist. Time %d\n",stop - start);

	i = 0;
	keystore[0] = 0;
	start = clock();
	while(it_next(t,&tmp,&it,0))
	{
		uint klen = sim_new(keystore,sizeof(keystore));
		i++;
		if(i > HIGH_ITERATION_COUNT || memcmp(keystore,it.kdata,klen) != 0)
			break;
	}
	stop = clock();

	// should have same count
	ASSERT(i == HIGH_ITERATION_COUNT);

	printf("(commit)it_next. Time %d\n",stop - start);

	// destroy
	it_dispose(t,&it);

	tk_drop_task(t);
}

void test_task_c_2()
{
	clock_t start,stop;

	st_ptr root,tmp;
	task* t;
	int i;
	uchar keystore[100];

	cle_pagesource* psource = &util_memory_pager;
	cle_psrc_data pdata = util_create_mempager();

	puts("\nRunning mempager: Multi-commit-test\n");
	page_size = 0;
	resize_count = 0;
	overflow_size = 0;

	start = clock();
	//  new task
	t = tk_create_task(psource,pdata);

	// set pagesource-root
	tk_root_ptr(t,&root);

	for(i = 0; i < HIGH_ITERATION_COUNT; i++)
	{
		tmp = root;
		memcpy(keystore,(char*)&i,sizeof(int));
		st_insert(t,&tmp,(cdat)keystore,sizeof(keystore));
	}

	tk_commit_task(t);

	stop = clock();

	printf("1-commit Time %d\n",stop - start);

	t = tk_create_task(psource,pdata);

	// set pagesource-root
	tk_root_ptr(t,&root);
	
	tk_ref_ptr(&root);

	start = clock();
	for(i = 0; i < HIGH_ITERATION_COUNT; i++)
	{
		ASSERT(st_exsist(t,&root,(cdat)&i,sizeof(int)));
	}
	stop = clock();

	st_prt_distribution(&root,t);
	
	tk_drop_task(t);
	//tk_commit_task(t);

	printf("1-commit Validate time %d\n",stop - start);

	start = clock();
	for(i = 0; i < HIGH_ITERATION_COUNT; i++)
	{
		//  new task
		t = tk_create_task(psource,pdata);

		// set pagesource-root
		tk_root_ptr(t,&root);

		tmp = root;
		memcpy(keystore,(char*)&i,sizeof(int));
		st_insert(t,&tmp,(cdat)keystore,sizeof(keystore));
//		st_insert(t,&root,(cdat)keystore,10);	// mem-bug!!!

		tk_commit_task(t);
	}
	stop = clock();

	printf("Multi-commit Time %d\n",stop - start);

	//  new task
	t = tk_create_task(psource,pdata);

	// should not happen.. but
	ASSERT(t);

	// set pagesource-root
	tk_root_ptr(t,&root);
	
	tk_ref_ptr(&root);

	start = clock();
	for(i = 0; i < HIGH_ITERATION_COUNT; i++)
	{
		ASSERT(st_exsist(t,&root,(cdat)&i,sizeof(int)));
	}
	stop = clock();

	printf("Multi-commit Time validate %d\n",stop - start);

	st_prt_distribution(&root,t);
	
	tk_drop_task(t);
}

void test_task_c_3() {
	st_ptr root,tmp;
	task* t;
	uchar keystore[2000];
	
	memset(keystore,0,sizeof(keystore));
	
	cle_pagesource* psource = &util_memory_pager;
	cle_psrc_data pdata = util_create_mempager();
	
	//  new task
	t = tk_create_task(psource,pdata);
	
	// set pagesource-root
	tk_root_ptr(t,&root);

	tmp = root;
	st_insert(t,&root,(cdat)keystore,sizeof(keystore));
	
	tk_commit_task(t);

	t = tk_create_task(psource,pdata);
	
	// set pagesource-root
	tk_root_ptr(t,&root);
	
	ASSERT(st_exsist(t,&root,(cdat)keystore,sizeof(keystore)));
	
	st_prt_distribution(&root,t);
	
	keystore[1000] = 1;
	tmp = root;
	st_insert(t,&tmp,(cdat)keystore,sizeof(keystore));
	
	keystore[1000] = 0;
	keystore[1500] = 1;
	tmp = root;
	st_insert(t,&tmp,(cdat)keystore,sizeof(keystore));
	
	tk_commit_task(t);

	t = tk_create_task(psource,pdata);
	
	// set pagesource-root
	tk_root_ptr(t,&root);

	keystore[1000] = 0;
	keystore[1500] = 0;
	ASSERT(st_exsist(t,&root,(cdat)keystore,sizeof(keystore)));
	
	keystore[1000] = 1;
	keystore[1500] = 0;
	ASSERT(st_exsist(t,&root,(cdat)keystore,sizeof(keystore)));

	keystore[1000] = 0;
	keystore[1500] = 1;
	ASSERT(st_exsist(t,&root,(cdat)keystore,sizeof(keystore)));

	tk_drop_task(t);
}

void test_task_c_filepager()
{
	clock_t start,stop;

	st_ptr root,tmp;
	it_ptr it;
	task* t;
	int i;
	uchar keystore[100];

	cle_pagesource* psource = &util_file_pager;
	cle_psrc_data pdata;
	
	puts("\nRunning filepager\n");
	// remove old test-database first
	unlink(testdbfilename);
	
	pdata = util_create_filepager(testdbfilename);

	//  new task
	t = tk_create_task(psource,pdata);

	// should not happen.. but
	ASSERT(t);

	// set pagesource-root
	tk_root_ptr(t,&root);

	// create
	it_create(t,&it,&root);

	// insert data
	start = clock();
	for(i = 0; i < HIGH_ITERATION_COUNT; i++)
	{
		if(it_new(t,&it,&tmp))
			break;
	}
	stop = clock();

	printf("(pre-commit)it_new. Time %d\n",stop - start);

	it_reset(&it);

	i = 0;
	start = clock();
	while(it_next(t,0,&it,0))
	{
		i++;
		ASSERT(i <= HIGH_ITERATION_COUNT);
	}
	stop = clock();

	// should have same count
	ASSERT(i == HIGH_ITERATION_COUNT);

	printf("(pre-commit)it_next. Time %d\n",stop - start);

	// destroy
	it_dispose(t,&it);

//	printf("(pre-commit) pagecount %d, overflowsize %d, resize-count %d\n",page_size,overflow_size,resize_count);

	// commit!
	start = clock();
	tk_commit_task(t);
	stop = clock();

	printf("filepager: tk_commit_task. Time %d\n",stop - start);

	// reopen
	pdata = util_create_filepager(testdbfilename);

	// new task, same source
	t = tk_create_task(psource,pdata);

	// set pagesource-root
	tk_root_ptr(t,&root);

	keystore[0] = 0;
	start = clock();
	for(i = 0; i < HIGH_ITERATION_COUNT; i++)
	{
		uint klen = sim_new(keystore,sizeof(keystore));
		if(st_exsist(t,&root,keystore,klen) == 0)
			break;
	}
	stop = clock();

	// should have same count
	ASSERT(i == HIGH_ITERATION_COUNT);

	printf("(commit)st_exsist. Time %d\n",stop - start);

	// read back collection
	it_create(t,&it,&root);

	i = 0;
	start = clock();
	while(it_next(t,&tmp,&it,0))
	{
		i++;
		ASSERT(i <= HIGH_ITERATION_COUNT);
	}
	stop = clock();

	// should have same count
	ASSERT(i == HIGH_ITERATION_COUNT);

	printf("(commit)it_next. Time %d\n",stop - start);

	// destroy
	it_dispose(t,&it);

//	printf("(post-commit) pagecount %d, overflowsize %d, resize-count %d\n",page_size,overflow_size,resize_count);

	tk_drop_task(t);
}

void test_tk_sync() {
	st_ptr root,tmp,ins_root,del_root;
	task* t;
	
	const unsigned char t1[] = "abc";
	const unsigned char t2[] = "abad";
	const unsigned char t3[] = "abcd";
	const unsigned char t4[] = "abcde";
	
	cle_pagesource* psource = &util_memory_pager;
	cle_psrc_data pdata = util_create_mempager();
	
	//  new task
	t = tk_create_task(psource,pdata);
	
	// set pagesource-root
	tk_root_ptr(t,&root);
	
	tmp = root;
	st_insert(t, &tmp, t1, sizeof(t1));
	
	tmp = root;
	st_insert(t, &tmp, t2, sizeof(t2));

	tmp = root;
	st_insert(t, &tmp, t3, sizeof(t3));
	
	st_empty(t, &ins_root);
	st_empty(t, &del_root);
	
	tk_sync_to(t, &del_root, &ins_root);

	st_prt_page(&ins_root);

	ASSERT(st_exsist(t, &ins_root, t1, sizeof(t1)));
	ASSERT(st_exsist(t, &ins_root, t2, sizeof(t2)));
	ASSERT(st_exsist(t, &ins_root, t3, sizeof(t3)));
	
	tk_commit_task(t);
	
	//  new task
	t = tk_create_task(psource,pdata);
	
	// set pagesource-root
	tk_root_ptr(t,&root);

	st_empty(t, &ins_root);
	st_empty(t, &del_root);
	
	tk_sync_to(t, &del_root, &ins_root);
	
	ASSERT(st_is_empty(&ins_root));
	ASSERT(st_is_empty(&del_root));
	
	tmp = root;
	st_insert(t, &tmp, t4, sizeof(t4));

	tk_sync_to(t, &del_root, &ins_root);

	ASSERT(st_exsist(t, &ins_root, t4, sizeof(t4)));

	tk_commit_task(t);
	
	//  new task
	t = tk_create_task(psource,pdata);
	
	// set pagesource-root
	tk_root_ptr(t,&root);
	
	st_delete(t, &root, t2, sizeof(t2));
	
	st_prt_page(&root);

	st_empty(t, &ins_root);
	st_empty(t, &del_root);
	
	tk_sync_to(t, &del_root, &ins_root);	
	
	st_prt_page(&del_root);

	tk_drop_task(t);
}

static uint dat(void* ctx,cdat dat,uint len)
{
	printf("dat %d %.*s\n",len,len,dat);
	return 0;
}

static uint push(void* ctx)
{
	puts("push");
	return 0;
}

static uint pop(void* ctx)
{
	puts("pop");
	return 0;
}

void test_st_trace()
{
	st_ptr root,tmp;
	task* t;

	page_size = 0;
	resize_count = 0;
	overflow_size = 0;

	//  new task
	t = tk_create_task(0,0);

	// should not happen.. but
	ASSERT(t);

	ASSERT(st_empty(t,&root) == 0);

	tmp = root;
	st_insert(t,&tmp,"aaa",4);
	tmp = root;
	st_insert(t,&tmp,"abb",4);
	tmp = root;
	st_insert(t,&tmp,"aac",4);
	tmp = root;
	st_insert(t,&tmp,"abc",4);
	tmp = root;
	st_insert(t,&tmp,"aaa\0a",6);	// continue 1

//	st_map_st(t,&root,dat,push,pop,0);

	ASSERT(st_empty(t,&tmp) == 0);
	
	/*ASSERT(st_copy_st(t,&tmp,&root) == 0);

	ASSERT(st_exsist(t,&tmp,"aaa",4));
	ASSERT(st_exsist(t,&tmp,"abb",4));
	ASSERT(st_exsist(t,&tmp,"aac",4));
	ASSERT(st_exsist(t,&tmp,"abc",4));
	ASSERT(st_exsist(t,&tmp,"aaa\0a",6));

	ASSERT(st_compare_st(t,&root,&tmp) == 0);
*/
	tk_drop_task(t);
}

int main(int argc, char* argv[])
{
	test_tk_sync();
	
	
	test_task_c_3();

	time_struct_c();

	heap_check();

	test_st_trace();

	heap_check();

	test_iterate_c();

	heap_check();

	test_iterate_fixedlength();

	heap_check();

	test_task_c();

	heap_check();

	test_task_c_2();

	puts("done");
	getchar();
	exit(0);

	heap_check();

	test_compile_c();

	test_instance_c();

	heap_check();

	test_task_c_filepager();

	heap_check();

	test_stream_c();

	heap_check();


	// test
	puts("\nTesting done...");
	getchar();
	return 0;
}
