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

static void _cache_pushdown(task* t, page_wrap* wrapper, cle_pageid pid)
{
	cle_pageid firstpid = pid;
	int i;
	for(i = 0; i < PID_CACHE_SIZE; i++)
	{
		page_wrap* twrap = t->cache[i].wrapper;
		cle_pageid tid = t->cache[i].pid;

		t->cache[i].wrapper = wrapper;
		t->cache[i].pid = pid;

		if(tid == 0 || tid == firstpid)
			break;

		pid = tid;
		wrapper = twrap;
	}
}

static page_wrap* _tk_load_page(task* t, page_wrap* parent, cle_pageid pid)
{
	/* have a wrapper? */
	st_ptr root_ptr;
	page_wrap* pw;
	int i;

	// try cache..
	for(i = 0; i < PID_CACHE_SIZE && t->cache[i].pid != 0; i++)
	{
		if(t->cache[i].pid == pid)
		{
			pw = t->cache[i].wrapper;
			_cache_pushdown(t,pw,pid);
			return pw;
		}
	}

	// not found in cache...
	root_ptr.key = t->pagemap_root_key;
	root_ptr.pg  = t->pagemap_root_wrap;
	root_ptr.offset = 0;

	// find pid in pagemap
	if(st_insert(t,&root_ptr,(cdat)&pid,sizeof(pid)))
	{
		/* not found: call pager to get page */
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

		// insert pw into pagemap
		st_append(t,&root_ptr,(cdat)&pw,sizeof(pw));
	}
	else
	{
		// found: read address of page_wrapper
		if(st_get(t,&root_ptr,(char*)&pw,sizeof(pw)) != -1)
			pw = 0;	// panic
	}

	// into cache
	_cache_pushdown(t,pw,pid);

	return pw;
}

key* _tk_get_ptr(task* t, page_wrap** pg, key* me)
{
	ptr* pt = (ptr*)me;
	if(pt->koffset != 0)
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
	t->ps->unref_page(t->psrc_data,pg->pg);
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
	key* k;
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

	// setup pagemap
	t->pagemap_root_wrap = xt.stack;
	t->pagemap_root_key = p->used;

	k = (key*)((char*)p + p->used);
	memset(k,0,sizeof(key) + 2);
	k->length = 1;

	p->used += sizeof(key) + 2;

	// pagecache
	memset(t->cache,0,sizeof(struct pidcache) * PID_CACHE_SIZE);
	return t;
}

task* tk_clone_task(task* parent)
{
	return tk_create_task(parent->ps,parent->psrc_data);
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
			t->ps->unref_page(t->psrc_data,pg->pg);
		else
			// free internal (written) pages
			tk_mfree(t,pg->pg);
		pg = tmp;
	}

	// quit the pager here
	if(t->ps != 0)
		t->ps->pager_close(t->psrc_data);

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
	task* t;

	key* lastkey;

	uint halfsize;
	uint fullsize;
};

// create pointer and write page
static ushort _tk_make_ptr_and_write(struct _tk_create* map, page_wrap* pw, uint offset)
{
	ushort koff;
	ptr* pt;

	if(pw->pg->used + sizeof(ptr) + 1 > pw->pg->size)
	{
		overflow* ovf = pw->ovf;

		if(ovf == 0)
		{
			ovf = (overflow*)tk_malloc(map->t,OVERFLOW_GROW);

			ovf->size = OVERFLOW_GROW;
			ovf->used = 16;

			pw->ovf = ovf;
		}
		else if(ovf->used + 16 > ovf->size)
		{
			ovf->size += OVERFLOW_GROW;

			ovf = (overflow*)tk_realloc(map->t,ovf,ovf->size);

			pw->ovf = ovf;
		}

		pt = (ptr*)((char*)ovf + ovf->used);

		koff = ovf->used >> 4;
		ovf->used += 16;
		koff |= 0x8000;
	}
	else
	{
		pw->pg->used += pw->pg->used & 1;
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
static ushort _tk_deep_copy(struct _tk_create* map, page_wrap* pw, key* parent, key* sour, int adjust)
{
	// next first
	ushort ptnxt = (sour->next != 0)?
		_tk_deep_copy(map,pw,parent,GOOFF(pw,sour->next),adjust) : 0;

	while(1)
	{
		// eliminate empty-keys
		// while: will we ever see chains of empty keys?
		// not sure, but just to be sure: loop through and eliminate
		while(sour->length == 1)
		{
			ushort tmpoffset = sour->offset;
			// nothing here...
			if(sour->sub == 0)
				return ptnxt;

			// replace...
			sour = GOOFF(pw,sour->sub);
			// forward offset (replace "1")
			sour->offset = tmpoffset;
		}

		// pointers
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
					// key-continue 
					if(parent != 0)
					{
						char* dest = (char*)map->dest + map->dest->used;
						uint bytelength = (sour->length + 7) >> 3;
						map->dest->used += bytelength;

						memcpy(dest,KDATA(sour),bytelength);
						adjust += parent->length;
						map->lastkey->length += sour->length;

						if(sour->sub == 0)
							return 0;

						sour = GOOFF(pw,sour->sub);
						parent = 0;
					}
				}
				else
				{
					// copy pointer
					ptr* dest;
					ushort ref;
					map->dest->used += map->dest->used & 1;
					ref = map->dest->used;
					map->dest->used += sizeof(ptr);
					dest = (ptr*)((char*)map->dest + ref);

					dest->zero = dest->koffset = 0;
					dest->next = ptnxt;
					dest->offset = pt->offset + adjust;
					dest->pg = pt->pg;
					return ref;
				}
			}
			else
			{
				// internal ptr (transparent)
				ushort tmpoffset = sour->offset;
				pw = (page_wrap*)pt->pg;
				sour = GOKEY(pw,pt->koffset);
				sour->offset = tmpoffset;
			}
		}
		else
		{
			uint bytelength = (sour->length + 7) >> 3;
			// key-continue?
			if(parent != 0 && parent->length == sour->offset)
			{
				char* dest = (char*)map->dest + map->dest->used;
				memcpy(dest,KDATA(sour),bytelength);
				adjust += parent->length;
				map->lastkey->length += sour->length;
				map->dest->used += bytelength;

				if(sour->sub == 0)
					return 0;

				parent = sour;
				sour = GOOFF(pw,sour->sub);
			}
			else
			{
				key* dest;
				ushort ref;
				map->dest->used += map->dest->used & 1;
				ref = map->dest->used;
				map->dest->used += bytelength + sizeof(key);
				dest = map->lastkey = (key*)((char*)map->dest + ref);

				memcpy(KDATA(dest),KDATA(sour),bytelength);
				dest->length = sour->length;
				dest->offset = sour->offset + adjust;
				dest->next = ptnxt;
				dest->sub = (sour->sub != 0)?
					_tk_deep_copy(map,pw,sour,GOOFF(pw,sour->sub),0) : 0;

				return ref;
			}
		}
	}
}

static uint _tk_overflow_mincut(struct _tk_create* map, page_wrap* pw, key* sub, key* k, uint size)
{
	// pageoverflow on min-cut?
	if(sub->length - k->offset + size <= map->fullsize - (sizeof(key) << 3))
		return k->offset - 1;	// no: use it

	// yes: cut out piece of sub as new page
	//if(k->next != 0)
	//	;

	unimplm();

	return 0;
}

static ushort _tk_make_page(struct _tk_create* map, page_wrap* pw, key* sub, key* prev, uint size)
{
	key* dest;
	uint ksize,byteoffset,offset = 0;

	// cut between prev and prev->next
	if(prev != 0)
	{
		// pageoverflow on max-cut?
		if(sub->length - prev->offset + size > map->fullsize - (sizeof(key) << 3))
			offset = _tk_overflow_mincut(map,pw,sub,GOOFF(pw,prev->next),size);
		else
			offset = prev->offset + 1;
	}
	// cut between 0 and sub->sub
	else if(sub->sub != 0)
	{
		// pageoverflow on max-cut?
		if(sub->length + size > map->fullsize - (sizeof(key) << 3))
			offset = _tk_overflow_mincut(map,pw,sub,GOOFF(pw,sub->sub),size);
	}
	// single key - page overflow?
	else if(sub->length > map->fullsize - (sizeof(key) << 3))
		offset = sub->length - (map->fullsize - (sizeof(key) << 3));

	// copy first (part-of) key and setup dest-page
	byteoffset = offset >> 3;
	ksize = ((sub->length + 7) >> 3) - byteoffset;

	dest = map->lastkey = (key*)((char*)map->dest + sizeof(page));
	map->dest->used = ksize + sizeof(page) + sizeof(key);

	memcpy(KDATA(dest),KDATA(sub) + byteoffset,ksize);

	dest->length = sub->length - (byteoffset << 3);
	dest->offset = dest->sub = dest->next = 0;

	// deep-copy rest of page-content
	if(prev != 0)
	{
		if(prev->next != 0)
			dest->sub = _tk_deep_copy(map,pw,sub,GOOFF(pw,prev->next),-(byteoffset << 3));
	}
	else if(sub->sub != 0)
		dest->sub = _tk_deep_copy(map,pw,sub,GOOFF(pw,sub->sub),-(byteoffset << 3));

	// not to be confused with a pointer
	if(offset == 0)
		offset = 1;

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

ms_tailcall:
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
				// build more on (single-)key-continue...?
				if(ksub->offset == k->length && sub_size + k->length < map->fullsize - (sizeof(key) << 3))
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
				goto ms_tailcall;
			}
			else
				size += sizeof(ptr) << 3;
		}
		else
		{
			// internal page-ptr
			pw = (page_wrap*)pt->pg;
			k = GOKEY(pw,pt->koffset);
			goto ms_tailcall;
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

			map.t = t;
			map.lastkey = 0;

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

			// reset and copy remaing rootpage
			map.dest->used = sizeof(page);
			_tk_deep_copy(&map,pgw,0,GOKEY(pgw,sizeof(page)),0);

			t->ps->write_page(t->psrc_data,pgw->ext_pageid,map.dest);

			// release old (now rebuild) page (if we found parent)
			if(map.id != 0)
				t->ps->remove_page(t->psrc_data,map.id);

			tk_mfree(t,map.dest);
		}
		else
		/* just write it */
			t->ps->write_page(t->psrc_data,pgw->ext_pageid,pg);

		ret = t->ps->pager_error(t->psrc_data);
		pgw = tmp;
	}

	if(ret == 0)
		ret = t->ps->pager_commit(t->psrc_data);
	else
		t->ps->pager_rollback(t->psrc_data);

	tk_drop_task(t);

	return ret;
}
