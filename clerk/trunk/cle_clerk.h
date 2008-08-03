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
#ifndef __CLE_CLERK_H__
#define __CLE_CLERK_H__

#include "cle_pagesource.h"

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

uint st_exsist(task* t, st_ptr* pt, cdat path, uint length);

uint st_move(task* t, st_ptr* pt, cdat path, uint length);

uint st_insert(task* t, st_ptr* pt, cdat path, uint length);

uint st_update(task* t, st_ptr* pt, cdat path, uint length);

uint st_append(task* t, st_ptr* pt, cdat path, uint length);

uint st_delete(task* t, st_ptr* pt, cdat path, uint length);

/*
uint st_offset(st_ptr* pt, uint offset);
uint st_prepend(task* t, st_ptr* pt, cdat path, uint length, uint replace_length);
*/

int st_get(task* t, st_ptr* pt, char* buffer, uint buffer_length);

char* st_get_all(task* t, st_ptr* pt, uint* length);

/* iterator functions */
void it_create(it_ptr* it, st_ptr* pt);

void it_dispose(task* t, it_ptr* it);

void it_load(task* t, it_ptr* it, cdat path, uint length);

uint it_new(task* t, it_ptr* it, st_ptr* pt);

uint it_next(task* t, st_ptr* pt, it_ptr* it);

uint it_next_eq(task* t, st_ptr* pt, it_ptr* it);

uint it_prev(task* t, st_ptr* pt, it_ptr* it);

uint it_prev_eq(task* t, st_ptr* pt, it_ptr* it);

/* Task functions */
task* tk_create_task(cle_pagesource* ps);

cle_output* tk_getoutput(task* t);

void tk_drop_task(task* t);

void* tk_alloc(task* t, uint size);

void* tk_malloc(task* t, uint size);
void* tk_realloc(task* t, void* mem, uint size);
void tk_mfree(task* t, void* mem);

/* test */

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
