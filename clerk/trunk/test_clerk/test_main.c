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

void unimplm()
{
	puts("failed in unimpl in test_main.c");
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
	ASSERT(st_insert_st(t,&root,&tmp) == 0);

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
	while(it_next(t,0,&it))
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
	keystore[0] = 0;
	start = clock();
	while(it_next(t,&tmp,&it))
	{
		uint klen = sim_new(keystore,sizeof(keystore));
		i++;
		if(i > HIGH_ITERATION_COUNT || memcmp(keystore,it.kdata,klen) != 0)
		{
			st_exsist(t,&root,keystore,klen);
			break;
		}
		if(i == 892)
			i = i;
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

/////////////////////////////// v2 /////////////////////////////////
#ifdef _DEBUG
#define CHECK(e) if((e) == 0) {fprintf(stderr,"failed: %s : %d\n",__FILE__,__LINE__);}
#else
#define CHECK(e)
#endif
static void* _ptr_validate = 0;
#include <string.h>
struct _tk_setup
{
	void* id;
	page_wrap* pg;

	page* dest;
	task* t;

	ptr* pt;
	ushort pt_off;

	uint halfsize;
	uint fullsize;
};

static void _tk_compact_copy(struct _tk_setup* setup, page_wrap* pw, key* parent, ushort* rsub, ushort next, int adjoffset)
{
	while(next != 0)
	{
		key* k = GOOFF(pw,next);
		// trace to end-of-next's
		if(k->next != 0) _tk_compact_copy(setup,pw,parent,rsub,k->next,adjoffset);

		if(ISPTR(k))	// pointer
		{
			ptr* pt = (ptr*)k;
			if(setup->pt_off == 0)
			{
				setup->pt_off = next;
				setup->pt = pt;
			}
			if(pt->koffset != 0)
			{
				pw = (page_wrap*)pt->pg;
				k = GOKEY(pw,pt->koffset);
			}
			else if(setup->id == pt->pg)
			{
				pw = setup->pg;
				k = GOKEY(pw,sizeof(page));
			}
			else
			{
				ptr* newptr;
				setup->dest->used += setup->dest->used & 1;
				newptr = (ptr*)((char*)setup->dest + setup->dest->used);
				newptr->koffset = 0;
				newptr->ptr_id = PTR_ID;
				newptr->offset = pt->offset + adjoffset;
				newptr->next = *rsub;
				newptr->pg = pt->pg;
				*rsub = setup->dest->used;
				setup->dest->used += sizeof(ptr);
				if(setup->dest->used >= setup->dest->size)
					k = k;
				break;
			}
		}

		if((parent != 0) && (k->offset + adjoffset == parent->length))	// append to parent key?
		{
			adjoffset = parent->length & 0xFFF8; 
			if(k->length != 0)	// skip empty keys
			{
				memcpy(KDATA(parent) + (adjoffset >> 3),KDATA(k),CEILBYTE(k->length));
				parent->length = k->length + adjoffset;
				setup->dest->used += CEILBYTE(k->length);
				if(setup->pt_off == 0 && (((k->length + 15) >> 3) + sizeof(key)) >= sizeof(ptr))
				{
					setup->pt_off = next;
					setup->pt = (ptr*)k;
				}
				if(setup->dest->used >= setup->dest->size)
					k = k;
			}
		}
		else if(k->length == 0)	// empty key?
		{
			if(parent != 0)
				adjoffset += k->offset;
			parent = 0;
		}
		else					// key w/data
		{
			setup->dest->used += setup->dest->used & 1;
			parent = (key*)((char*)setup->dest + setup->dest->used);
			parent->offset = k->offset + adjoffset;
			parent->length = k->length;
			parent->next = *rsub;
			parent->sub = 0;
			memcpy(KDATA(parent),KDATA(k),CEILBYTE(k->length));
			*rsub = setup->dest->used;
			rsub = &parent->sub;
			setup->dest->used += sizeof(key) + CEILBYTE(k->length);
			if(setup->pt_off == 0 && (((k->length + 15) >> 3) + sizeof(key)) >= sizeof(ptr))
			{
				setup->pt_off = next;
				setup->pt = (ptr*)k;
			}
			if(setup->dest->used >= setup->dest->size)
				k = k;

			adjoffset = 0;
		}
		next = k->sub;
	}
}

static uint _tk_cut2(struct _tk_setup* setup, page_wrap* pw, key* copy, key* prev, int offset)
{
	// copy first/root key
	key* root; int limit = copy->length;
	//CHECK(offset <= copy->length)

	if(prev != 0)
	{
		if(prev->next != 0)
			limit = GOOFF(pw,prev->next)->offset;// - 1;
	}
	else if(copy->sub != 0)
		limit = (GOOFF(pw,copy->sub)->offset) & 0xFFF8;// - 1;

	if(offset > limit)
		offset = limit;

	setup->dest->used = sizeof(page);
	root = (key*)((char*)setup->dest + sizeof(page));

	root->offset = root->next = root->sub = 0;
	root->length = copy->length - (offset & 0xFFF8);

	memcpy(KDATA(root),KDATA(copy) + (offset >> 3),CEILBYTE(root->length));
	setup->dest->used += sizeof(key) + CEILBYTE(root->length);

	// cut 'copy'
	copy->length = offset;

//	_tk_new_pointer(setup,pw,offset);
	setup->pt_off = 0;
	// start compact-copy
	if(prev != 0)
	{
		_tk_compact_copy(setup,pw,root,&root->sub,prev->next,-(offset & 0xFFF8));
		prev->next = setup->pt_off;
	}
	else
	{
		_tk_compact_copy(setup,pw,root,&root->sub,copy->sub,-(offset & 0xFFF8));
		copy->sub = setup->pt_off;
	}

	if(setup->pt_off == 0)
	{
		unimplm();
	}

	setup->pt->ptr_id = PTR_ID;
	setup->pt->koffset = setup->pt->next = 0;
	setup->pt->offset = offset;
	// pager: create new page
	setup->pt->pg = setup->t->ps->new_page(setup->t->psrc_data,setup->dest);

	return (sizeof(ptr)*8);
}

static uint _tk_measure2(struct _tk_setup* setup, page_wrap* pw, key* parent, key* k)
{
	uint size = (k->next == 0)? 0 : _tk_measure2(setup,pw,parent,GOOFF(pw,k->next));

	// parent over k->offset
	if(parent != 0)
		while(1)
		{
			int offset = size + parent->length - k->offset + ((sizeof(key)+1)*8) - setup->halfsize;
			if(offset <= 0)	// upper-cut
				break;
			size = _tk_cut2(setup,pw,parent,k,offset + k->offset);
		}

	if(ISPTR(k))
	{
		ptr* pt = (ptr*)k;
		if(pt->koffset != 0)
			size += _tk_measure2(setup,(page_wrap*)pt->pg,0,GOOFF((page_wrap*)pt->pg,pt->koffset));
		else if(setup->id == pt->pg)
			size += _tk_measure2(setup,setup->pg,0,GOOFF(setup->pg,sizeof(page)));
		else
			size += ((sizeof(ptr)+1)*8);

		return size;
	}
	else	// cut k below limit (length | sub->offset)
	{
		uint subsize = (k->sub == 0)? 0 : _tk_measure2(setup,pw,k,GOOFF(pw,k->sub));

		do
		{
			int offset = size + subsize + k->length + ((sizeof(key)+1)*8) - setup->halfsize;
			if(offset < 0)
				break;
			subsize = _tk_cut2(setup,pw,k,0,offset);
		}
		while(subsize + k->length + ((sizeof(key)+1)*8) > setup->halfsize);
		size += subsize;
	}

	return size + k->length + ((sizeof(key)+1)*8);
}

static char* _ms_tests[] = {
	"aac",
	"bb",
	"aabb",
	"bbxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
	"bbxxxxxxxxxxxxxxxxxyxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
	"bbxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxy",
	0};

static key* _trace_nxt(st_ptr* pt)
{
	key* nxt;
	key* me = GOOFF(pt->pg,pt->key);
	// deal with offset
	if(me->sub == 0)
		return 0;

	nxt = GOOFF(pt->pg,me->sub);
	while(nxt->offset < pt->offset)
	{
		if(nxt->next == 0)
			return 0;

		nxt = GOOFF(pt->pg,nxt->next);
	}
	return nxt;
}

#define COMMIT_ITERATION_COUNT 1000000
void test_measure2()
{
		it_ptr it;
	st_ptr pt,pt2;
	struct _tk_setup setup;
	cle_psrc_data psrc_data = util_create_mempager();
	task* t = tk_create_task(&util_memory_pager,psrc_data);
	char** chr;
	key* k;
	int i;
	ushort sub;
	uint a = 0x80;
	struct
	{
		page head;
		char data[10000];
	}dest;

				a |= (a >> 1);
				a |= (a >> 2);
				a |= (a >> 4);
				// lzc(a)
				a -= ((a >> 1) & 0x55);
				a = (((a >> 2) & 0x33) + (a & 0x33));
				a = (((a >> 4) + a) & 0x0f);

	setup.t = t;
	setup.fullsize = 80*8;
	setup.halfsize = setup.fullsize/2;
	setup.dest = &dest.head;
	setup.id = 0;
	setup.pg = 0;

	dest.head.id = 0;
	dest.head.size = sizeof(page) + setup.fullsize/8;
	dest.head.used = 0;
	dest.head.waste = 0;

	_ptr_validate = setup.dest;

	//st_empty(t,&pt);

	////_tk_measure2(&setup,pt.pg,0,GOKEY(pt.pg,pt.key));

	//chr = _ms_tests;
	//while(*chr != 0)
	//{
	//	size_t len = strlen(*chr);
	//	st_ptr tmp = pt;
	//	st_insert(t,&tmp,*chr,(uint)len);
	//	chr++;
	//}

	//pt2 = pt;
	//st_move(t,&pt2,"bbxxxxxxxxxxxxx",15);

	//k = _trace_nxt(&pt2);
	//_tk_cut2(&setup,pt2.pg,(key*)((char*)pt2.pg->pg + pt2.key),k,150);

	//heap_check();
	//sub = 0;
	//dest.head.used = sizeof(page);
	//dest.head.size = sizeof(page) + 1000; 
	//_tk_compact_copy(&setup,pt.pg,0,&sub,pt.key,0);
	//heap_check();
	//t->ps->write_page(t->psrc_data,ROOT_ID,setup.dest);
	//heap_check();

	//dest.head.size = sizeof(page) + setup.fullsize/8;
	//tk_root_ptr(t,&pt2);

	//chr = _ms_tests;
	//while(*chr != 0)
	//{
	//	size_t len = strlen(*chr);
	//	ASSERT(st_exsist(t,&pt2,*chr,(uint)len));
	//	chr++;
	//}
/*
	st_empty(t,&pt);

	chr = _ms_tests;
	while(*chr != 0)
	{
		size_t len = strlen(*chr);
		st_ptr tmp = pt;
		st_insert(t,&tmp,*chr,(uint)len);
		chr++;
	}

	st_prt_page(&pt);

	setup.fullsize = 80*8;
	setup.halfsize = setup.fullsize/2;
	dest.head.size = sizeof(page) + setup.fullsize/8;

	_tk_measure2(&setup,pt.pg,0,GOKEY(pt.pg,pt.key));

	sub = 0;
	setup.dest->used = sizeof(page);
	_tk_compact_copy(&setup,pt.pg,0,&sub,pt.key,0);
	setup.dest->size = 1024;
	t->ps->write_page(t->psrc_data,ROOT_ID,setup.dest);

	tk_root_ptr(t,&pt);

	puts("----");
	st_prt_page(&pt);

	chr = _ms_tests;
	while(*chr != 0)
	{
		size_t len = strlen(*chr);
		ASSERT(st_exsist(t,&pt,*chr,(uint)len));
		chr++;
	}
*/
	//////////////////////////////////
	st_empty(t,&pt);
	// create
//	it_create(t,&it,&pt);

	// insert data
	for(i = 0; i < COMMIT_ITERATION_COUNT; i++)
	{
		pt2 = pt;
		st_insert(t,&pt2,(char*)&i,sizeof(uint));
//		if(it_new(t,&it,&pt2))
//			break;
	}

	for(i = 0; i < COMMIT_ITERATION_COUNT; i++)
	{
		ASSERT(st_exsist(t,&pt,(char*)&i,sizeof(uint)));
	}

	st_prt_distribution(&pt,t);

	// destroy
	//it_dispose(t,&it);

	// commit!
	setup.fullsize = 1000*8;
	setup.halfsize = setup.fullsize/2;
	dest.head.size = sizeof(page) + setup.fullsize/8;

	heap_check();
	_tk_measure2(&setup,pt.pg,0,GOOFF(pt.pg,pt.key));

	heap_check();
	sub = 0;
	setup.dest->used = sizeof(page);
	_tk_compact_copy(&setup,pt.pg,0,&sub,pt.key,0);
	heap_check();
	t->ps->write_page(t->psrc_data,ROOT_ID,setup.dest);

	tk_drop_task(t);
	t = tk_create_task(&util_memory_pager,psrc_data);

	heap_check();
	puts("----");
	// set pagesource-root
	tk_root_ptr(t,&pt);

	st_prt_distribution(&pt,t);

	// read back collection
//	it_create(t,&it,&pt);
	for(i = 0; i < COMMIT_ITERATION_COUNT; i++)
	{
		if(st_exsist(t,&pt,(char*)&i,sizeof(uint)) == 0)
			break;
	}

//	i = 0;
//	while(it_next(t,0,&it))
//	{
		//int j;
//		i++;

		//for(j = 0; j < it.kused; j++)
		//{
		//	printf("%d ",it.kdata[j]);
		//}
		//printf(" %p\n",tmp.pg->pg->id);
//		ASSERT(i <= HIGH_ITERATION_COUNT);
//	}

	// should have same count
	ASSERT(i == COMMIT_ITERATION_COUNT);

	// destroy
//	it_dispose(t,&it);

	tk_drop_task(t);
}

int main(int argc, char* argv[])
{
	test_measure2();

	heap_check();

	test_struct_c();

	time_struct_c();

	heap_check();

	test_st_trace();

	heap_check();

	test_iterate_c();

	heap_check();

	test_task_c();

	heap_check();

	test_task_c_filepager();

	heap_check();

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
