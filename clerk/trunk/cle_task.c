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
#include <stdlib.h>

#include "cle_clerk.h"
#include "cle_struct.h"

/* mem-manager */
void* tk_malloc(task* t, uint size)
{
	void* m = malloc(size);
	if(m == 0)
		unimplm();

	return m;
}

void* tk_realloc(task* t, void* mem, uint size)
{
	void* m = realloc(mem,size);
	if(m == 0)
		unimplm();

	return m;
}

void tk_mfree(task* t, void* mem)
{
	free(mem);
}

/* internal */
static void _tk_clear_tree(task* t, page* pg, ushort off)
{
	while(off)
	{
		key* k = GOKEY(pg,off);
		if(k->length == 0)
		{
			ptr* pt = (ptr*)k;
			if(pt->koffset == 0)	// real page
			{
				// que 
				pt->pg;
			}
			pg->waste += sizeof(ptr);
		}
		else
		{
			pg->waste += ((k->length + 7) >> 3) + sizeof(key);
			_tk_clear_tree(t,pg,k->sub);
		}

		off = k->next;
	}
}

key* _tk_get_ptr(task* t, page** pg, key* me)
{
	ptr* pt = (ptr*)me;
	if(pt->koffset)
	{
		*pg = (page*)pt->pg;
		me = GOKEY(*pg,pt->koffset);	/* points to a key - not an ovf-ptr */
	}
	else
	{
		/* have a writable copy? */
		page_wrap* pw = t->wpages;
		while(pw && pw->ext_pageid != pt->pg)
			pw = pw->next;

		if(pw)
			*pg = pw->pg;	/* use internal(writable) copy of page */
		else
			/* call pager to get page */
			*pg = t->ps->read_page(t->psrc_data,pt->pg);
		/* go to root-key */
		me = GOKEY(*pg,sizeof(page));
	}

	return me;
}

page* _tk_write_copy(task* t, page* pg)
{
	page_wrap* wp;
	page* npg;
	/* copy to new (internal) page */
	npg = (page*)tk_malloc(t,pg->size + sizeof(page_wrap));
	memcpy(npg,pg,pg->used);
	npg->id = 0;

	/* setup page-wrap */
	wp = GOPAGEWRAP(pg);
	wp->ext_pageid = pg->id;
	wp->pg = npg;
	wp->ovf = 0;

	wp->next = t->wpages;
	t->wpages = wp;

	//t->ps->unref_page(t->psrc_data,pg->id);
	return npg;
}

void _tk_stack_new(task* t)
{
	/* put a new page on the task stack */
	page_wrap* tmp;
	page* pg = (page*)tk_malloc(t,PAGE_SIZE + sizeof(page_wrap));

	pg->id = 0;
	pg->size = PAGE_SIZE;
	pg->used = sizeof(page);
	pg->waste = 0;

	tmp = (page_wrap*)((char*)pg + PAGE_SIZE);
	tmp->next = t->stack;
	tmp->ext_pageid = 0;
	tmp->ovf = 0;
	tmp->pg = pg;
	t->stack = tmp;

	page_size++;	// TEST
}

void* tk_alloc(task* t, uint size)
{
	uint offset;
	page* pg = (page*)((char*)t->stack - PAGE_SIZE);
	if(t->stack == 0 || pg->used + size + 3 > PAGE_SIZE)
	{
		if(size > PAGE_SIZE - sizeof(page))
			return 0;

		_tk_stack_new(t);
	}

	pg = (page*)((char*)t->stack - PAGE_SIZE);
	if(pg->used & 3)
		pg->used += 4 - (pg->used & 3);

	offset = pg->used;
	pg->used += size;

	return (void*)GOKEY(pg,offset);
}

void _tk_remove_tree(task* t, page* pg, ushort off)
{
	// clear tree before clean_page
	_tk_clear_tree(t,pg,off);

	// clean_page
	if(pg->waste > pg->size/2)
	{
		// que this + parent
	}
}

task* tk_create_task(cle_pagesource* ps, cle_psrc_data psrc_data)
{
	task* t = (task*)tk_malloc(0,sizeof(task));
	t->psrc_data = psrc_data;
	t->ps = ps;
	t->stack = 0;
	t->wpages = 0;
	return t;
}

void tk_drop_task(task* t)
{
	page_wrap* pg = t->stack;
	while(pg)
	{
		t->stack = pg->next;
		tk_mfree(t,(char*)pg - PAGE_SIZE);
		pg = t->stack;
	}

	tk_mfree(0,t);
}
