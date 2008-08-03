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
static void _tk_clear_tree(task* t, page_wrap* pg, ushort off)
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
			pg->pg.waste += sizeof(ptr);
		}
		else
		{
			pg->pg.waste += ((k->length + 7) >> 3) + sizeof(key);
			_tk_clear_tree(t,pg,k->sub);
		}

		off = k->next;
	}
}

key* _tk_get_ptr(task* t, page_wrap** pg, key* me)
{
	ptr* pt = (ptr*)me;
	if(pt->koffset)
	{
		*pg = (page_wrap*)pt->pg;
		me = GOKEY(*pg,pt->koffset);	/* points to a key - not an ovf-ptr */
	}
	else
		/* call pager to get persistent page */
		unimplm();

	return me;
}

void _tk_stack_new(task* t)
{
	/* put a new page on the task stack */
	page_wrap* tmp = (page_wrap*)tk_malloc(PAGE_SIZE + sizeof(page_wrap));

	tmp->next = t->stack;
	tmp->page_adr = 0;		// in-mem page (no disk adr)
	tmp->ovf = 0;
	tmp->pg.used = sizeof(page);
	tmp->pg.waste = 0;
	t->stack = tmp;

	page_size++;	// TEST
}

void* tk_alloc(task* t, uint size)
{
	uint offset;
	if(t->stack == 0 || t->stack->pg.used + size + 3 > PAGE_SIZE)
	{
		if(size > PAGE_SIZE - sizeof(page))
			return 0;

		_tk_stack_new(t);
	}

	if(t->stack->pg.used & 3)
		t->stack->pg.used += 4 - (t->stack->pg.used & 3);

	offset = t->stack->pg.used;
	t->stack->pg.used += size;

	return (void*)GOKEY(t->stack,offset);
}

void _tk_remove_tree(task* t, page_wrap* pg, ushort off)
{
	// clear tree before clean_page
	_tk_clear_tree(t,pg,off);

	// clean_page
	if(pg->pg.waste > PAGE_SIZE/2)
	{
		// que this + parent
	}
}

task* tk_create_task(cle_pagesource* ps)
{
	task* t = (task*)tk_malloc(0,sizeof(task));
	t->ps = ps;
	t->stack = 0;
	return t;
}

void tk_drop_task(task* t)
{
	page_wrap* pg = t->stack;
	while(pg)
	{
		t->stack = pg->next;
		tk_mfree(pg);
		pg = t->stack;
	}

	tk_mfree(t);
}
