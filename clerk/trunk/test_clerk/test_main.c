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
#include <malloc.h>
#include <errno.h>
#include "test.h"

void unimplm()
{
	puts("failed in unimpl in test_main.c");
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
	tk_dup_ptr(&tmp,&root);
	ASSERT(st_insert(t,&tmp,test1,sizeof(test1)));

	// collection not empy anymore
	ASSERT(st_is_empty(&root) == 0);

	// value can be found again
	ASSERT(st_exsist(t,&root,test1,sizeof(test1)));

	// and some other random values can not be found
	ASSERT(st_exsist(t,&root,test2,sizeof(test2)) == 0);

	ASSERT(st_exsist(t,&root,test3,sizeof(test3)) == 0);

	// can move pointer there (same as exsist) no prob
	tk_dup_ptr(&tmp,&root);
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
	tk_dup_ptr(&tmp,&root);
	ASSERT(st_move(t,&tmp,test1,sizeof(test1)) == 0);

	ASSERT(st_exsist(t,&tmp,test1,sizeof(test1)));

	// we can move all the way and find and empty node
	tk_dup_ptr(&tmp,&root);
	ASSERT(st_move(t,&tmp,test1x2,sizeof(test1x2)) == 0);

	ASSERT(st_is_empty(&tmp));

	// put 2 more root-values in
	tk_dup_ptr(&tmp,&root);
	ASSERT(st_insert(t,&tmp,test2,sizeof(test2)));

	tk_dup_ptr(&tmp,&root);
	ASSERT(st_insert(t,&tmp,test3,sizeof(test3)));

	// we can find all 3 distinct values
	ASSERT(st_exsist(t,&root,test1,sizeof(test1)));

	ASSERT(st_exsist(t,&root,test2,sizeof(test2)));

	ASSERT(st_exsist(t,&root,test3,sizeof(test3)));

	// and we can still find the combined string
	ASSERT(st_exsist(t,&root,test1x2,sizeof(test1x2)));

	// we can move to one of the new values and start a new collection there
	tk_dup_ptr(&tmp,&root);
	ASSERT(st_move(t,&tmp,test2,sizeof(test2)) == 0);

	tk_dup_ptr(&tmp2,&tmp);
	ASSERT(st_insert(t,&tmp2,test2,sizeof(test2)));

	tk_dup_ptr(&tmp2,&tmp);
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
	ASSERT(st_insert_st(t,&root,&tmp));

	ASSERT(st_move(t,&tmp2,test1x2,sizeof(test1x2)) == 0);

	printf("pagecount %d, overflowsize %d, resize-count %d\n",page_size,overflow_size,resize_count);

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
	while(it_next(t,0,&it))
	{
		i++;
	}
	stop = clock();

	printf("it_next %d items. Time %d\n",i, stop - start);

	ASSERT(i == HIGH_ITERATION_COUNT);

	it_reset(&it);
	i = 0;

	start = clock();
	while(it_prev(t,0,&it))
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
//		st_delete(t,&root,(cdat)&counter,sizeof(counter));
	}
	stop = clock();

	printf("delete %d items. Time %d\n",HIGH_ITERATION_COUNT, stop - start);

	// collection now empty again
	//ASSERT(st_is_empty(&root));

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
	start = clock();
	while(it_next(t,0,&it))
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

	printf("(pre-commit) pagecount %d, overflowsize %d, resize-count %d\n",page_size,overflow_size,resize_count);

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

	i = 0;
	start = clock();
	while(it_next(t,&tmp,&it))
	{
		//int j;
		i++;

		//for(j = 0; j < it.kused; j++)
		//{
		//	printf("%d ",it.kdata[j]);
		//}
		//printf(" %p\n",tmp.pg->pg->id);
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

void test_task_c_filepager()
{
	clock_t start,stop;

	st_ptr root,tmp;
	it_ptr it;
	task* t;
	int i;

	cle_pagesource* psource = &util_file_pager;
	cle_psrc_data pdata;
	
	puts("\nRunning filepager\n");
	// remove old test-database first
	_unlink(testdbfilename);
	
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
	while(it_next(t,0,&it))
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

	// read back collection
	it_create(t,&it,&root);

	i = 0;
	start = clock();
	while(it_next(t,&tmp,&it))
	{
		i++;
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
	
	ASSERT(st_copy_st(t,&tmp,&root) == 0);

	ASSERT(st_exsist(t,&tmp,"aaa",4));
	ASSERT(st_exsist(t,&tmp,"abb",4));
	ASSERT(st_exsist(t,&tmp,"aac",4));
	ASSERT(st_exsist(t,&tmp,"abc",4));
	ASSERT(st_exsist(t,&tmp,"aaa\0a",6));

	ASSERT(st_compare_st(t,&root,&tmp) == 0);

	tk_drop_task(t);
}

void heap_check()
{
///*
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
//*/
}

int main(int argc, char* argv[])
{
	test_compile_c();

	test_struct_c();

	test_st_trace();

	//heap_check();

	time_struct_c();

	//heap_check();

	test_iterate_c();

	//heap_check();

	test_task_c();

	//heap_check();

	test_task_c_filepager();

	//heap_check();

	test_stream_c();

	//heap_check();

	test_compile_c();

	//heap_check();

	test_instance_c();

	//heap_check();

	test_runtime_c();

	heap_check();
	// test
	puts("\nTesting done...");
	getchar();
	return 0;
}
