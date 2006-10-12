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
#include <stdlib.h>

#include "cle_clerk.h"
#include "cle_struct.h"

/* mem-manager */
void* tk_malloc(uint size)
{
	void* m = malloc(size);
	if(m == 0)
		unimplm();

	return m;
}

void* tk_realloc(void* mem, uint size)
{
	void* m = realloc(mem,size);
	if(m == 0)
		unimplm();

	return m;
}

void tk_mfree(void* mem)
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

key* _tk_get_ptr(page_wrap** pg, key* me)
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

task* tk_create_task(task* parent)
{
	task* t = (task*)tk_malloc(sizeof(task));

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