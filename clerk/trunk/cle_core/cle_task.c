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

static void _tk_release_page(task* t, page_wrap* wp)
{
	/* unref linked pages */
	if(wp->ovf != 0)
	{
		int off;
		for(off = 16; off < wp->ovf->used; off += 16)
		{
			ptr* pt = (ptr*)((char*)wp->ovf + off);
			page_wrap* wpt = (page_wrap*)pt->pg;

			if(--(wpt->refcount) == 0)
				_tk_release_page(t,wpt);
		}

		/* release ovf */
		tk_mfree(t,wp->ovf);
	}

	/* release page */
	tk_mfree(t,wp->pg);
}

static page_wrap* _tk_load_page(task* t, cle_pageid pid)
{
	/* have a wrapper? */
	page_wrap* pw = t->wpages;

	while(pw && pw->ext_pageid != pid)
		pw = pw->next;

	if(pw != 0)
		pw->refcount++;
	else
	{
		/* call pager to get page */
		page* npage = t->ps->read_page(t->psrc_data,pid);

		/* create wrapper and add to w-list */
		pw = (page_wrap*)tk_alloc(t,sizeof(page_wrap));
		pw->ext_pageid = pid;
		pw->ovf = 0;
		pw->pg = npage;
		pw->refcount = 1;

		pw->next = t->wpages;
		t->wpages = pw;
	}

	return pw;
}

key* _tk_get_ptr(task* t, page_wrap** pg, key* me)
{
	ptr* pt = (ptr*)me;
	if(pt->koffset)
	{
		page_wrap* oldpage = *pg;

		*pg = (page_wrap*)pt->pg;
		me = GOKEY(*pg,pt->koffset);	/* points to a key - not an ovf-ptr */

		(*pg)->refcount++;

		if(--(oldpage->refcount) == 0)
			/* dead page */
			//_tk_release_page(t,oldpage);
			;
	}
	else
	{
		*pg = _tk_load_page(t,pt->pg);
		/* go to root-key */
		me = GOKEY(*pg,sizeof(page));
	}

	return me;
}

void tk_root_ptr(task* t, st_ptr* pt)
{
	pt->pg = _tk_load_page(t,ROOT_ID);
	pt->key = sizeof(page);
	pt->offset = 0;
}

void tk_dup_ptr(st_ptr* to, st_ptr* from)
{
	*to = *from;
	to->pg->refcount++;
}

void tk_unref(task* t, page_wrap* pg)
{
	if(pg->pg->id)
//		t->ps->unref_page(t->psrc_data,pg->pg->id);
		;
	/* internal dead page ? */
	else if(--(pg->refcount) == 0)
		_tk_release_page(t,pg);
}

void _tk_write_copy(task* t, page_wrap* pg)
{
	/* copy to new (internal) page */
	page* npg = (page*)tk_malloc(t,pg->pg->size);
	memcpy(npg,pg->pg,pg->pg->used);
	npg->id = 0;

	/* swap pages */
	t->ps->unref_page(t->psrc_data,pg->pg->id);
	pg->pg = npg;
}

void _tk_stack_new(task* t)
{
	/* put a new page on the task stack */
	page* pg;
	page_wrap* tmp = (page_wrap*)tk_malloc(t,PAGE_SIZE + sizeof(page_wrap));

	pg = (page*)(tmp + 1);
	pg->id = 0;
	pg->size = PAGE_SIZE;
	pg->used = sizeof(page);
	pg->waste = 0;

	tmp->next = t->stack;
	tmp->ext_pageid = 0;
	tmp->refcount = 1;
	tmp->ovf = 0;
	tmp->pg = pg;
	t->stack = tmp;

	page_size++;	// TEST
}

void* tk_alloc(task* t, uint size)
{
	uint offset;
	page* pg = t->stack->pg;
	if(pg->used + size + 3 > PAGE_SIZE)
	{
		if(size > PAGE_SIZE - sizeof(page))
			return 0;

		_tk_stack_new(t);
	}

	pg = t->stack->pg;
	if(pg->used & 3)
		pg->used += 4 - (pg->used & 3);

	offset = pg->used;
	pg->used += size;

	t->stack->refcount++;
	return (void*)((char*)pg + offset);
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
			pg->pg->waste += sizeof(ptr);
		}
		else
		{
			pg->pg->waste += ((k->length + 7) >> 3) + sizeof(key);
			_tk_clear_tree(t,pg,k->sub);
		}

		off = k->next;
	}
}

void _tk_remove_tree(task* t, page_wrap* pg, ushort off)
{
	// clear tree before clean_page
	_tk_clear_tree(t,pg,off);

	// clean_page
	if(pg->pg->waste > pg->pg->size/2)
	{
		// que this + parent
	}
}

task* tk_create_task(cle_pagesource* ps, cle_psrc_data psrc_data)
{
	task* t;
	page* p;
	task xt;
	xt.stack = 0;
	_tk_stack_new(&xt);
	/* always delete with task itself */
	xt.stack->refcount++;

	p = xt.stack->pg;
	t = (task*)((char*)p + p->used);
	p->used += sizeof(task);

	t->psrc_data = psrc_data;
	t->ps = ps;
	t->stack = xt.stack;
	t->wpages = 0;
	return t;
}

void tk_drop_task(task* t)
{
	page_wrap* pg = t->wpages;
	while(pg)
	{
		page_wrap* tmp = pg->next;
		tk_mfree(t,pg->ovf);
		// unref external pages
		if(pg->pg->id != 0)
			t->ps->unref_page(t->psrc_data,pg->pg->id);
		else
			// free internal (written) pages
			tk_mfree(t,pg->pg);
		pg = tmp;
	}

	pg = t->stack;
	while(pg)
	{
		page_wrap* tmp = pg->next;
		tk_mfree(t,pg->ovf);
		tk_mfree(t,pg);
		pg = tmp;
	}
}

// ---- commit ----------------------------------
struct _tk_create
{
	void* id;
	page_wrap* pg;

	page* dest;

	uint halfsize;
	uint fullsize;
};

// create pointer 
static ushort _tk_make_ptr(struct _tk_create* map, page_wrap* pw, uint offset)
{
	ushort koff;
	ptr* pt;

	if(pw->pg->used + sizeof(ptr) > pw->pg->size)
	{
		if(pw->ovf == 0)
		{}
		else if(pw->ovf->used + 16 > pw->ovf->size)
		{}

		koff = pw->ovf->used >> 4;
		pw->ovf->used += 16;
		koff |= 0x8000;
	}
	else
	{
		koff = pw->pg->used;
		pw->pg->used += sizeof(ptr);
		pt = (ptr*)((char*)pw->pg + koff);
	}

	pt->koffset = pt->next = pt->zero = 0;
	pt->pg = map->dest->id;
	pt->offset = offset;
	return koff;
}

/*
	trace forward and copy one half-page
*/
static void _tk_copy_page(struct _tk_create* map, key* sub, key* prev, uint offset)
{
	key* dest = (key*)((char*)map->dest + map->dest->used);
	uint byteoffset = offset >> 3;
	memcpy(dest + sizeof(key),KDATA(sub) + byteoffset,((sub->length + 7) >> 3) - byteoffset);

	dest->length = sub->length - offset;
	dest->offset = sub->offset - offset;
	dest->next = 0;
	dest->sub = 0;
}

/*
	trace tree depth first.
	find cut-point for half-page-sized subtrees and copy
	final return gets copied to root page
*/
static uint _tk_measure(struct _tk_create* map, page_wrap* pw, key* parent, key* k)
{
	uint size = 0;	// in bits

tailcall:
	if(k->next != 0)
	{
		key* knext = GOOFF(pw,k->next);
		uint offset = 0;
		size = _tk_measure(map,pw,parent,knext);

		if(size + parent->length - knext->offset >= map->halfsize)
			offset = knext->offset;
		else if(size + parent->length - k->offset >= map->halfsize)
		{
			offset = (knext->offset - k->offset)/2 + k->offset;
			if(offset == k->offset)
				offset++;
		}

		if(offset != 0)
		{
			_tk_copy_page(map,parent,k,offset);
			parent->length = offset;
			k->next = _tk_make_ptr(map,pw,offset);
			size = sizeof(ptr) << 3;
		}
	}

	if(k->length != 0)
	{
		size += k->length + (sizeof(key) << 3);

		if(k->sub != 0)
		{
			key* ksub = GOOFF(pw,k->sub);
			uint sub_size = _tk_measure(map,pw,k,ksub);
			
			if(sub_size + k->length > map->halfsize)
			{
				uint offset = 0;
				// continue-key?
				if(ksub->offset == k->length)
					offset = 0;
				else
					offset = 0;

				_tk_copy_page(map,ksub,0,offset);
				k->length = offset;
				k->sub = _tk_make_ptr(map,pw,offset);
				size += sizeof(ptr) << 3;
			}
			else
				size += sub_size;
		}
	}
	else
	{
		ptr* pt = (ptr*)k;
		if(pt->koffset == 0)	// real page
		{
			// go throu pointer?
			if(map->id == pt->pg)
			{
				parent = 0;
				pw = map->pg;
				k = GOKEY(pw,sizeof(page));
				goto tailcall;
			}
			else
				size += sizeof(ptr) << 3;
		}
		else
		{
			// internal page-ptr
			pw = (page_wrap*)pt->pg;
			k = GOKEY(pw,pt->koffset);
			goto tailcall;
		}
	}

	return size;
}

int tk_commit_task(task* t)
{
	page_wrap* pgw = t->wpages;
	int ret = 0;
	while(pgw != 0 && ret == 0)
	{
		page_wrap* tmp = pgw->next;
		page* pg = pgw->pg;

		/* overflowed or underflow? */
		if(pgw->ovf || pg->waste > pg->size/2)
		{
			struct _tk_create map;
			// TODO: pick up parent and rebuild from there
			key* rootkey = GOKEY(pgw,sizeof(page));
			map.id = pg->id;
			map.pg = 0;
			// TODO: create initial page
			map.dest = 0;
			map.halfsize = pg->size << 2;
			map.fullsize = pg->size << 3;

			_tk_measure(&map,pgw,0,rootkey);

			_tk_copy_page(&map,rootkey,0,0);
		}
		else
		/* just write it */
			ret = t->ps->write_page(t->psrc_data,pgw->ext_pageid,pg);

		pgw = tmp;
	}

	tk_drop_task(t);

	return ret;
}
