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
#ifndef __CLE_STRUCT_H__
#define __CLE_STRUCT_H__

#include "cle_pagesource.h"

/* Config */

#define PAGE_SIZE 2048

#define OVERFLOW_GROW (16*64)

#define GET_ALL_BUFFER 256

#define IT_GROW_SIZE 16

/* Defs */
typedef struct overflow
{
	uint used;
	uint size;
} overflow;

/* DEL
typedef struct page
{
	ushort used;
	ushort waste;
} page;
*/

typedef struct page_wrap
{
	struct page_wrap* next;
	overflow* ovf;
	void* ext_pageid;
}page_wrap;

typedef struct key
{
	ushort offset;
	ushort length;
	ushort next;
	ushort sub;
} key;

typedef struct ptr
{
	ushort offset;
	ushort zero;
	ushort next;
	ushort koffset;
	void*  pg;
} ptr;

struct task
{
	page_wrap* stack;
	cle_pagesource* ps;
	cle_psrc_data psrc_data;
};

#define GOKEY(pag,off) ((key*)((char*)(pag) + (off)))
#define GOPTR(pag,off) ((key*)(((char*)((page_wrap*)((char*)(pag) + (pag)->size))->ovf) + (((off) - (pag)->size)<<4)))
#define GOOFF(pag,off) ((off & 0xF800)? GOPTR(pag,off):GOKEY(pag,off))
#define KDATA(k) ((char*)k + sizeof(key))

key* _tk_get_ptr(task* t, page** pg, key* me);
void _tk_stack_new(task* t);
void _tk_remove_tree(task* t, page* pg, ushort key);

#endif
