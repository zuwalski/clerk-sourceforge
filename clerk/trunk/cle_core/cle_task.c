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

static page_wrap* _tk_load_page(task* t, page_wrap* parent, cle_pageid pid)
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
		pw->parent = parent;

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
		*pg = _tk_load_page(t,*pg,pt->pg);
		/* go to root-key */
		me = GOKEY(*pg,sizeof(page));
	}

	return me;
}

void tk_root_ptr(task* t, st_ptr* pt)
{
	pt->pg = _tk_load_page(t,0,ROOT_ID);
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
	pg->refcount--;

	/* internal dead page ? */
	if(pg->refcount == 0 && pg->pg->id == 0)
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
	page_wrap* dpw;

	task* t;

	uint halfsize;
	uint fullsize;
};

// create pointer and write page
static ushort _tk_make_ptr_and_write(struct _tk_create* map, page_wrap* pw, uint offset)
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
	pt->offset = offset;

	// create and write page
	pt->pg = map->t->ps->new_page(map->t->psrc_data,map->dest);
	return koff;
}

/*
	trace forward and copy to new page
*/
static ushort _tk_deep_copy(struct _tk_create* map, page_wrap* pw, key* parent, int adjust, ushort ko)
{
	ushort ref = 0;
	while(ko != 0)
	{
		key* sour = GOOFF(pw,ko);

		// next first
		_tk_deep_copy(map,pw,parent,adjust,sour->next);

		if(sour->length == 0)
		{
			ptr* pt = (ptr*)sour;
			if(pt->koffset == 0)
			{
				// mapped pagelink?
				if(map->id == pt->pg)
				{
					pw = map->pg;
					sour = GOKEY(pw,sizeof(page));
				}
				else
				{
					// copy pointer
					ptr* dest;
					map->dest->used += map->dest->used & 1;
					ref = map->dest->used;
					dest = (ptr*)((char*)map->dest + map->dest->used);
					memcpy(dest,sour,sizeof(ptr));
					map->dest->used += sizeof(ptr);
					return ref;
				}
			}
			else
			{
				// internal ptr (transparant)
				pw = (page_wrap*)pt->pg;
				sour = GOKEY(pw,pt->koffset);
			}
		}

		if(sour->length > 1)
		{
			uint bytelength = (sour->length + 7) >> 3;
			// key-continue?
			if(parent != 0 && parent->length == sour->offset)
			{
				char* dest = (char*)map->dest + map->dest->used;
				memcpy(dest,KDATA(sour),bytelength);
				adjust += parent->length;
				parent->length += sour->length;
				map->dest->used += bytelength;
			}
			else
			{
				key* dest;
				map->dest->used += map->dest->used & 1;
				ref = map->dest->used;
				dest = (key*)((char*)map->dest + map->dest->used);
				memcpy(KDATA(dest),KDATA(sour),bytelength);
				dest->length = sour->length;
				dest->offset = sour->offset + adjust;
				dest->sub = dest->next = 0;
				map->dest->used += bytelength + sizeof(key);
				adjust = 0;
			}

			parent = sour;
		}
		
		ko = sour->sub;
	}
	return ref;
}

static ushort _tk_make_page(struct _tk_create* map, page_wrap* pw, key* sub, key* prev, uint size)
{
	key* dest;
	uint ksize,byteoffset,offset = 0;
	// result in page overflow?
	if(size + sub->length - (prev != 0? prev->offset : 0) > map->fullsize)
	{}

	// copy first (part-of) key and setup dest-page
	byteoffset = offset >> 3;
	dest = (key*)((char*)map->dest + sizeof(page));
	ksize = ((sub->length + 7) >> 3) - byteoffset;
	map->dest->used = ksize + sizeof(page);

	memcpy(KDATA(dest),KDATA(sub) + byteoffset,ksize);

	dest->length = sub->length - offset;
	dest->offset = dest->sub = dest->next = 0;

	// deep-copy rest of page-content
	if(prev != 0)
		dest->sub = _tk_deep_copy(map,pw,dest,-offset,prev->next);
	else
		dest->sub = _tk_deep_copy(map,pw,dest,-offset,sub->sub);

	// cut off sub and...
	sub->length = offset;
	// make a pointer instead
	return _tk_make_ptr_and_write(map,pw,offset);
}

/*
	trace tree depth first.
	find cut-point for half-page-sized subtrees and copy
	of if root-relation is "continue" then proceed towards fullsize-page-copy
	final return gets copied to root page
*/
static uint _tk_measure(struct _tk_create* map, page_wrap* pw, key* parent, key* k)
{
	uint size = 0;	// in bits

tailcall:
	if(k->next != 0)
	{
		size = _tk_measure(map,pw,parent,GOOFF(pw,k->next));

		if(size + parent->length - k->offset >= map->halfsize)
		{
			k->next = _tk_make_page(map,pw,parent,k,size);
			size = sizeof(ptr) << 3;
		}
	}

	if(k->length != 0)
	{
		if(k->sub != 0)
		{
			key* ksub = GOOFF(pw,k->sub);
			uint sub_size = _tk_measure(map,pw,k,ksub);
			
			if(sub_size + k->length >= map->halfsize)
			{
				// build more on key-continue...?
				if(ksub->offset == k->length && sub_size + k->length < map->fullsize)
					size += sub_size + k->length;
				else
				{
					k->sub = _tk_make_page(map,pw,k,0,sub_size);
					size += sizeof(ptr) << 3;
				}
			}
			else
				size += sub_size + k->length + (sizeof(key) << 3);
		}
		else if(k->length >= map->halfsize)
		{
			k->sub = _tk_make_page(map,pw,k,0,0);
			size += sizeof(ptr) << 3;
		}
		else
			size += k->length + (sizeof(key) << 3);
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
			map.dest = tk_malloc(t,pg->size);
			map.dest->size = pg->size;
			map.dest->waste = 0;

			map.halfsize = pg->size << 2;
			map.fullsize = pg->size << 3;

			// pick up parent and rebuild from there (if not root)
			if(pgw->parent != 0)
			{
				map.id = pg->id;
				map.pg = pgw;
				pgw = pgw->parent;
			}
			else
			{
				map.id = 0;
				map.pg = 0;
			}

			_tk_measure(&map,pgw,0,GOKEY(pgw,sizeof(page)));

			_tk_deep_copy(&map,pgw,0,0,sizeof(page));

			t->ps->write_page(t->psrc_data,pgw->pg->id,map.dest);

			// release old (now rebuild) page (if we found parent)
			if(map.id != 0)
				t->ps->remove_page(t->psrc_data,map.id);

			tk_mfree(t,map.dest);
		}
		else
		/* just write it */
			t->ps->write_page(t->psrc_data,pgw->ext_pageid,pg);

		ret = t->ps->page_error(t->psrc_data);
		pgw = tmp;
	}

	tk_drop_task(t);

	return ret;
}
