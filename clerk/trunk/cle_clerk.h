/* 
   Copyright 2005-2006 Lars Szuwalski

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/
#ifndef __CLE_CLERK_H__
#define __CLE_CLERK_H__

#include <stdio.h>

#define PAGE_SIZE 2048

typedef unsigned int uint;
typedef unsigned long ulong;
typedef unsigned short ushort;
typedef unsigned char uchar;
typedef const unsigned char* cdat;

typedef struct page_wrap page_wrap;

typedef struct task task;

typedef struct st_ptr
{
	page_wrap* pg;
	ushort	   key;
	ushort	   offset;
} st_ptr;

typedef struct it_ptr
{
	page_wrap* pg;
	uchar* kdata;
	ushort key;
	ushort offset;
	ushort ksize;
	ushort kused;
} it_ptr;

/* output interface begin */
typedef struct cle_output
{
	int (*start)(void*);
	int (*end)(void*,cdat,uint);
	int (*pop)(void*);
	int (*push)(void*);
	int (*data)(void*,cdat,uint);
	int (*next)(void*);
} cle_output;

/* output interface end */

/* generel functions */
void st_empty(task* t, st_ptr* pt);

uint st_is_empty(st_ptr* pt);

uint st_exsist(st_ptr* pt, cdat path, uint length);

uint st_move(st_ptr* pt, cdat path, uint length);

uint st_insert(task* t, st_ptr* pt, cdat path, uint length);

uint st_update(task* t, st_ptr* pt, cdat path, uint length);

uint st_append(task* t, st_ptr* pt, cdat path, uint length);

uint st_prepend(task* t, st_ptr* pt, cdat path, uint length, uint replace_length);

uint st_delete(task* t, st_ptr* pt, cdat path, uint length);

uint st_offset(st_ptr* pt, uint offset);

int st_get(st_ptr* pt, char* buffer, uint buffer_length);

char* st_get_all(st_ptr* pt, uint* length);

/* iterator functions */
void it_create(it_ptr* it, st_ptr* pt);

void it_dispose(it_ptr* it);

void it_load(it_ptr* it, cdat path, uint length);

uint it_new(task* t, it_ptr* it, st_ptr* pt);

uint it_next(st_ptr* pt, it_ptr* it);

uint it_next_eq(st_ptr* pt, it_ptr* it);

uint it_prev(st_ptr* pt, it_ptr* it);

uint it_prev_eq(st_ptr* pt, it_ptr* it);

/* Task functions */
task* tk_create_task(task* parent);

cle_output* tk_getoutput(task* t);

void tk_drop_task(task* t);

void* tk_alloc(task* t, uint size);

void* tk_malloc(uint size);
void* tk_realloc(void* mem, uint size);
void tk_mfree(void* mem);

/* test */

void st_prt_page(st_ptr* pt);

void unimplm();

extern uint page_size;
extern uint resize_count;
extern uint overflow_size;

#define HEAD_SIZE 2
#define HEAD_FUNCTION "\0F"
#define HEAD_EXPR "\0E"
#define HEAD_INT "\0I"
#define HEAD_STR "\0S"
#define HEAD_NEXT "\0N"

#define HEAD_TYPE "\0T"
#define HEAD_APPS  "\0A"

#define HEAD_EVENT "\0e"
#define HEAD_IMPORT "\0i"
#define HEAD_EXTENDS "\0x"

#endif
