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

typedef struct task task;
typedef struct page_wrap page_wrap;

typedef struct st_ptr
{
	page_wrap* pg;
	ushort key;
	ushort offset;
} st_ptr;

typedef struct ptr_list
{
	struct ptr_list* link;
	st_ptr pt;
} ptr_list;

typedef struct it_ptr
{
	page_wrap* pg;
	uchar* kdata;
	ushort key;
	ushort offset;
	ushort ksize;
	ushort kused;
} it_ptr;

/* generel functions */
// create empty node
// = 0 if ok - 1 if t is readonly
uint st_empty(task* t, st_ptr* pt);

// = 1 if pt points to empty node else = 0
uint st_is_empty(st_ptr* pt);

// test if path exsist from pt - else = 0
uint st_exsist(task* t, st_ptr* pt, cdat path, uint length);

// move ptr to path - else = 1
uint st_move(task* t, st_ptr* pt, cdat path, uint length);

// insert path - if already there = 1
uint st_insert(task* t, st_ptr* pt, cdat path, uint length);

uint st_update(task* t, st_ptr* pt, cdat path, uint length);

uint st_append(task* t, st_ptr* pt, cdat path, uint length);

uint st_delete(task* t, st_ptr* pt, cdat path, uint length);

/*
uint st_offset(st_ptr* pt, uint offset);
uint st_prepend(task* t, st_ptr* pt, cdat path, uint length, uint replace_length);
*/

int st_get(task* t, st_ptr* pt, char* buffer, uint buffer_length);

//char* st_get_all(task* t, st_ptr* pt, uint* length);

/* iterator functions */
void it_create(task* t, it_ptr* it, st_ptr* pt);

void it_dispose(task* t, it_ptr* it);

void it_load(task* t, it_ptr* it, cdat path, uint length);

void it_reset(it_ptr* it);

uint it_new(task* t, it_ptr* it, st_ptr* pt);

uint it_next(task* t, st_ptr* pt, it_ptr* it);

uint it_next_eq(task* t, st_ptr* pt, it_ptr* it);

uint it_prev(task* t, st_ptr* pt, it_ptr* it);

uint it_prev_eq(task* t, st_ptr* pt, it_ptr* it);

/* Task functions */
task* tk_create_task(cle_pagesource* ps, cle_psrc_data psrc_data);

task* tk_clone_task(task* parent);

void tk_drop_task(task* t);
int tk_commit_task(task* t);

void* tk_alloc(task* t, uint size);

void* tk_malloc(task* t, uint size);
void* tk_realloc(task* t, void* mem, uint size);
void tk_mfree(task* t, void* mem);

void tk_unref(task* t, page_wrap* pg);
void tk_dup_ptr(st_ptr* to, st_ptr* from);

void tk_root_ptr(task* t, st_ptr* pt);

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



#endif
