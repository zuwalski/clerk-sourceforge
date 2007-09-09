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
#ifndef __CLE_STRUCT_H__
#define __CLE_STRUCT_H__

/* Config */

#define OVERFLOW_GROW (16*64)

#define GET_ALL_BUFFER 256

#define IT_GROW_SIZE 16

/* Defs */
typedef struct overflow
{
	uint used;
	uint size;
} overflow;

typedef struct page
{
	ushort used;
	ushort waste;
} page;

typedef struct page_wrap
{
	struct page_wrap* next;
	overflow* ovf;
	void*     page_adr;
	/* from page-header */
	page pg;
} page_wrap;

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

typedef struct task
{
	page_wrap* stack;
} task;

#define GOKEY(pag,off) ((key*)((char*)&((pag)->pg) + (off)))
#define GOPTR(pg,off) ((key*)((char*)(pg)->ovf + (((off) - PAGE_SIZE)<<4)))
#define GOOFF(pg,off) ((off & 0xF800)? GOPTR(pg,off):GOKEY(pg,off))
#define KDATA(k) ((char*)k + sizeof(key))

key* _tk_get_ptr(page_wrap** pg, key* me);
void _tk_stack_new(task* t);
void _tk_remove_tree(task* t, page_wrap* pg, ushort key);

#endif
