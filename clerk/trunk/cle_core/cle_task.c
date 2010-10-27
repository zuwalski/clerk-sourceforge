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
#include <string.h>

#include "cle_clerk.h"
#include "cle_struct.h"

const page_wrap rt_wrapper = { 0, 0, 0, 0, 0, 0, 0 };

// TODO cle_task.c : from recursive commit/free to looping
// TODO - : map-cache should only hold written pages

/* mem-manager */
// TODO: call external allocator
// TODO: should not be used outside task.c -> make private
void* tk_malloc(task* t, uint size) {
	void* m = malloc(size);
	if (m == 0)
		cle_panic(t);

	return m;
}

// TODO: remove -> use tk_alloc
void* tk_realloc(task* t, void* mem, uint size) {
	void* m = realloc(mem, size);
	if (m == 0)
		cle_panic(t);

	return m;
}
// TODO: (see tk_malloc)
void tk_mfree(task* t, void* mem) {
	free(mem);
}

static void _page_ptr(task* t, st_ptr* pt) {
	pt->key = t->pagemap_root_key;
	pt->pg = t->pagemap_root_wrap;
	pt->offset = 0;
}

static void _tk_init_pw_block(task* t, pw_block* b) {
	int i;

	memset(b, 0, sizeof(pw_block));

	for (i = 0; i < PW_BLOCK_SIZE; i++) {
		b->pws[i].next = t->pwfree;
		t->pwfree = b->pws + i;
	}
}

static page_wrap* _tk_new_wrapper(task* t) {
	page_wrap* pw = t->pwfree;
	if (pw == 0) {
		pw_block* b = (pw_block*) tk_malloc(t, sizeof(pw_block));

		_tk_init_pw_block(t, b);

		b->next = t->pws.next;
		t->pws.next = b;

		pw = t->pwfree;
	}

	t->pwfree = pw->next;
	return pw;
}

static page_wrap* _tk_alloc_page(task* t, uint page_size) {
	page* pg = (page*) tk_malloc(t, page_size);
	page_wrap* tmp = _tk_new_wrapper(t);

	pg->id = 0;
	pg->size = page_size;
	pg->used = sizeof(page);
	pg->waste = 0;

	tmp->ext_pageid = 0;
	tmp->refcount = 1;
	tmp->next = 0;
	tmp->ovf = 0;
	tmp->pg = pg;
	return tmp;
}

void _tk_stack_new(task* t) {
	t->stack = _tk_alloc_page(t, PAGE_SIZE);
}

void* tk_alloc(task* t, uint size, struct page_wrap** pgref) {
	page* pg = t->stack->pg;
	uint offset;

	if (pg->used + size + 7 > PAGE_SIZE) {
		if (size > PAGE_SIZE - sizeof(page)) {
			// big chunk - alloc specific
			page_wrap* tmp = _tk_alloc_page(t, size + sizeof(page));

			if (pgref != 0)
				*pgref = tmp;
			return (void*) ((char*) tmp->pg + sizeof(page));
		}

		_tk_stack_new(t);
		pg = t->stack->pg;
	}

	// align 8 (avoidable? dont use real pointers from here - eg. always copy?)
	if (pg->used & 7)
		pg->used += 8 - (pg->used & 7);

	offset = pg->used;
	pg->used += size;

	t->stack->refcount++;

	if (pgref != 0)
		*pgref = t->stack;
	return (void*) ((char*) pg + offset);
}

static void _tk_release_page(task* t, page_wrap* wp) {
	// sanity: don't touch written external pages!
	if (wp->ext_pageid != 0 && wp->pg->id == 0)
		cle_panic(t);

	/* unref linked pages */
	if (wp->ovf != 0) {
		int off;
		for (off = 16; off < wp->ovf->used; off += 16) {
			ptr* pt = (ptr*) ((char*) wp->ovf + off);
			page_wrap* wpt = (page_wrap*) pt->pg;

			wpt->refcount--;
			if (wpt->refcount == 0)
				_tk_release_page(t, wpt);
		}

		/* release ovf */
		tk_mfree(t, wp->ovf);
		wp->ovf = 0;
	}

	if (wp->ext_pageid != 0) {
		st_ptr root_ptr;
		_page_ptr(t, &root_ptr);

		st_delete(t, &root_ptr, (cdat) &wp->ext_pageid, sizeof(cle_pageid));

		t->ps->unref_page(t->psrc_data, wp->pg);
	} else {
		/* free internal page */
		tk_mfree(t, wp->pg);
		wp->pg = 0;
	}

	// recycle wrapper
	wp->next = t->pwfree;
	t->pwfree = wp;
}

static void _cache_pushdown(task* t, page_wrap* wrapper, cle_pageid pid) {
	cle_pageid firstpid = pid;
	uint i;
	for (i = 0; i < PID_CACHE_SIZE && pid != 0; i++) {
		page_wrap* twrap = t->cache[i].wrapper;
		cle_pageid tid = t->cache[i].pid;

		t->cache[i].wrapper = wrapper;
		t->cache[i].pid = pid;

		if (tid == firstpid)
			return; // cache-entries just reordered. No ref adjust

		pid = tid;
		wrapper = twrap;
	}

	// entry is new in cache
	t->cache[0].wrapper->refcount++;

	// this entry is pushed out of the cache
	if (pid != 0) {
		wrapper->refcount--;

		// if its not unused make sure the wrapper is stored
		if (wrapper->refcount != 0 || wrapper->pg->id == 0) {
			st_ptr root_ptr;
			_page_ptr(t, &root_ptr);

			if (st_insert(t, &root_ptr, (cdat) &pid, sizeof(cle_pageid)))
				// insert pw into pagemap
				st_append(t, &root_ptr, (cdat) &wrapper, sizeof(page_wrap*));
		} else
			_tk_release_page(t, wrapper);
	}
}

static page_wrap* _tk_load_page(task* t, cle_pageid pid, const page_wrap* parent) {
	/* have a wrapper? */
	st_ptr root_ptr;
	page_wrap* pw;
	uint i;

	// try cache..
	for (i = 0; i < PID_CACHE_SIZE && t->cache[i].pid != 0; i++) {
		if (t->cache[i].pid == pid) {
			pw = t->cache[i].wrapper;
			_cache_pushdown(t, pw, pid);
			return pw;
		}
	}

	// not found in cache...
	_page_ptr(t, &root_ptr);

	// find pid in pagemap
	if (st_move(t, &root_ptr, (cdat) &pid, sizeof(pid))) {
		/* not found: call pager to get page */
		page* npage = t->ps->read_page(t->psrc_data, pid);

		pw = _tk_new_wrapper(t);
		pw->ext_pageid = pid;
		pw->ovf = 0;
		pw->pg = npage;
		pw->refcount = 0;

		pw->parent_0 = parent->ext_pageid;
		pw->parent_1 = parent->parent_0;
	}
	// found: read address of page_wrapper
	else if (st_get(t, &root_ptr, (char*) &pw, sizeof(pw)) != -1)
		cle_panic(t); // map corrupted

	// into cache
	_cache_pushdown(t, pw, pid);

	return pw;
}

void _tk_write_copy(task* t, page_wrap* pg) {
	/* copy to new (internal) page */
	page* npg = (page*) tk_malloc(t, pg->pg->size);
	memcpy(npg, pg->pg, pg->pg->used);
	npg->id = 0;

	/* swap pages */
	t->ps->unref_page(t->psrc_data, pg->pg);
	pg->pg = npg;

	// pg in written pages list
	pg->next = t->wpages;
	t->wpages = pg;
}

key* _tk_get_ptr(task* t, page_wrap** pg, key* me) {
	ptr* pt = (ptr*) me;
	if (pt->koffset != 0) {
		*pg = (page_wrap*) pt->pg;
		me = GOKEY(*pg,pt->koffset); /* points to a key - not an ovf-ptr */
	} else {
		*pg = _tk_load_page(t, pt->pg, *pg);
		/* go to root-key */
		me = GOKEY(*pg,sizeof(page));
	}
	return me;
}

ushort _tk_alloc_ptr(task* t, page_wrap* pg) {
	overflow* ovf = pg->ovf;
	ushort nkoff;

	if (ovf == 0) /* alloc overflow-block */
	{
		ovf = (overflow*) tk_malloc(t, OVERFLOW_GROW);

		ovf->size = OVERFLOW_GROW;
		ovf->used = 16;

		pg->ovf = ovf;
	} else if (ovf->used == ovf->size) /* resize overflow-block */
	{
		ovf->size += OVERFLOW_GROW;

		ovf = (overflow*) tk_realloc(t, ovf, ovf->size);

		pg->ovf = ovf;
	}

	/* make pointer */
	nkoff = (ovf->used >> 4) | 0x8000;
	ovf->used += 16;

	pg->refcount++;
	return nkoff;
}

void _tk_remove_tree(task* t, page_wrap* pg, ushort off) {
	while (off) {
		key* k = GOOFF(pg,off);
		if (ISPTR(k)) {
			ptr* pt = (ptr*) k;
			if (pt->koffset == 0) // external page
			{
				// queue dead page -> search to childpages to remove them
				t->ps->remove_page(t->psrc_data, pg->ext_pageid);
			} else {
				page_wrap* pw = (page_wrap*) pt->pg;

				//_tk_remove_tree(t,pw,pt->koffset);

				pw->refcount--;
				if (pw->refcount == 0)
					_tk_release_page(t, pw);
			}

			pg->pg->waste += sizeof(ptr);
		} else {
			pg->pg->waste += CEILBYTE(k->length) + sizeof(key);
			_tk_remove_tree(t, pg, k->sub);
		}

		off = k->next;
	}
}

void tk_unref(task* t, page_wrap* pg) {
	/* dead page ? */
	if (pg->refcount == 1)
		_tk_release_page(t, pg);
	else
		pg->refcount--;
}

void tk_free_ptr(task* t, st_ptr* ptr) {
	tk_unref(t, ptr->pg);
}

void tk_ref_ptr(st_ptr* ptr) {
	ptr->pg->refcount++;
}

void tk_root_ptr(task* t, st_ptr* pt) {
	key* k;
	pt->pg = _tk_load_page(t, ROOT_ID, &rt_wrapper);
	pt->key = sizeof(page);
	pt->offset = 0;
	k = GOKEY(pt->pg,sizeof(page));
	if (ISPTR(k)) {
		k = _tk_get_ptr(t, &pt->pg, k);
		pt->key = (ushort) ((char*) k - (char*) pt->pg->pg);
	}
}

void tk_dup_ptr(st_ptr* to, st_ptr* from) {
	*to = *from;
	to->pg->refcount++;
}

ushort tk_segment(task* t) {
	return t->segment;
}

segment tk_new_segment(task* t) {
	cle_panic(t); // not impl yet - will delegate to pagesource
	return 0;
}

task* tk_create_task(cle_pagesource* ps, cle_psrc_data psrc_data) {
	// initial alloc
	task* t = (task*) tk_malloc(0, sizeof(task));
	page* p;
	key* k;

	// pagecache
	memset(t->cache, 0, sizeof(t->cache));

	t->pwfree = 0;
	_tk_init_pw_block(t, &t->pws);

	_tk_stack_new(t);

	t->psrc_data = psrc_data;
	t->ps = ps;
	t->wpages = 0;
	t->segment = 1; // TODO get from pager

	// setup pagemap
	p = t->stack->pg;
	t->pagemap_root_wrap = t->stack;
	t->pagemap_root_key = p->used;

	k = (key*) ((char*) p + p->used);
	memset(k, 0, sizeof(key));

	p->used += sizeof(key);

	return t;
}

task* tk_clone_task(task* parent) {
	return tk_create_task(parent->ps, (parent->ps == 0) ? 0
			: parent->ps->pager_clone(parent->psrc_data));
}

void tk_drop_task(task* t) {
	pw_block* b = &t->pws;

	// release pages and overflows
	while (b != 0) {
		int i;
		for (i = 0; i < PW_BLOCK_SIZE; i++) {
			page_wrap* w = b->pws + i;
			tk_mfree(t, w->ovf);

			if (w->pg != 0) {
				// page belongs to page-source
				if (w->ext_pageid != 0 && w->pg->id != 0)
					t->ps->unref_page(t->psrc_data, w->pg);
				else
					tk_mfree(t, w->pg);
			}
		}

		b = b->next;
	}

	// free all pw-blocks
	b = t->pws.next;
	while (b != 0) {
		pw_block* tmp = b;
		b = b->next;

		tk_mfree(0, tmp);
	}

	// quit the pager here
	if (t->ps != 0)
		t->ps->pager_close(t->psrc_data);

	// last: free initial alloc
	tk_mfree(0, t);
}

/////////////////////////////// Commit v3 /////////////////////////////////
struct _tk_setup {
	void* id;
	page_wrap* pg;

	page* dest;
	task* t;

	uint halfsize;
	uint fullsize;

	ushort o_pt;
	ushort l_pt;
};

static void _tk_compact_copy(struct _tk_setup* setup, page_wrap* pw,
		key* parent, ushort* rsub, ushort next, int adjoffset) {
	while (next != 0) {
		key* k = GOOFF(pw,next);
		// trace to end-of-next's
		if (k->next != 0)
			_tk_compact_copy(setup, pw, parent, rsub, k->next, adjoffset);

		// trace a place for a pointer on this page (notice: PTR_ID == MAX-USHORT)
		if (setup->l_pt < k->length) {
			setup->l_pt = k->length;
			setup->o_pt = next;
		}

		while (ISPTR(k)) // pointer
		{
			ptr* pt = (ptr*) k;
			if (pt->koffset != 0) {
				pw = (page_wrap*) pt->pg;
				k = GOKEY(pw,pt->koffset);
			} else if (setup->id == pt->pg) {
				pw = setup->pg;
				k = GOKEY(pw,sizeof(page));

				adjoffset += pt->offset;
			} else {
				ptr* newptr;
				setup->dest->used += setup->dest->used & 1;
				newptr = (ptr*) ((char*) setup->dest + setup->dest->used);
				newptr->koffset = 0;
				newptr->ptr_id = PTR_ID;
				newptr->offset = pt->offset + adjoffset;
				newptr->next = *rsub;
				newptr->pg = pt->pg;
				*rsub = setup->dest->used;
				setup->dest->used += sizeof(ptr);
				return;
			}
		}

		if (k->length == 0) // empty key? (skip)
		{
			adjoffset += k->offset;
		} else if ((parent != 0) && (k->offset + adjoffset == parent->length)) // append to parent key?
		{
			adjoffset = parent->length & 0xFFF8; // 'my' subs are offset by parent-length

			memcpy(KDATA(parent) + (parent->length >> 3), KDATA(k),
					CEILBYTE(k->length));
			parent->length = k->length + adjoffset;
			setup->dest->used += CEILBYTE(k->length);
		} else // key w/data
		{
			setup->dest->used += setup->dest->used & 1;
			parent = (key*) ((char*) setup->dest + setup->dest->used);
			parent->offset = k->offset + adjoffset;
			parent->length = k->length;
			parent->next = *rsub;
			parent->sub = 0;
			memcpy(KDATA(parent), KDATA(k), CEILBYTE(k->length));
			*rsub = setup->dest->used;
			rsub = &parent->sub;
			setup->dest->used += sizeof(key) + CEILBYTE(k->length);
			adjoffset = 0;
		}
		next = k->sub;
	}
}

static ushort _tk_link_and_create_page(struct _tk_setup* setup, page_wrap* pw,
		int ptr_offset) {
	// first: create a link to new page
	ptr* pt;
	ushort pt_offset;

	// use exsisting key?
	if (setup->l_pt + sizeof(key) * 8 >= sizeof(ptr) * 8) {
		pt_offset = setup->o_pt;
		pt = (ptr*) GOOFF(pw,pt_offset);
	}
	// room for ptr in page?
	else if (pw->pg->used + sizeof(ptr) <= pw->pg->size) {
		pt_offset = pw->pg->used;
		pt = (ptr*) GOKEY(pw,pt_offset);
		pw->pg->used += sizeof(ptr);
	} else {
		pt_offset = _tk_alloc_ptr(setup->t, pw); // might change ptr-adresses!
		pt = (ptr*) GOPTR(pw,pt_offset);
	}

	pt->offset = ptr_offset;
	pt->ptr_id = PTR_ID;
	pt->koffset = pt->next = 0;

	// then: create new page (copy dest) and put pageid in ptr
	pt->pg = setup->t->ps->new_page(setup->t->psrc_data, setup->dest);

	return pt_offset;
}

static int _tk_adjust_cut(page_wrap* pw, key* copy, key* prev, int cut_bid) {
	int limit = copy->length;

	if (prev != 0) {
		if (prev->next != 0)
			limit = GOOFF(pw,prev->next)->offset;
	} else if (copy->sub != 0)
		limit = (GOOFF(pw,copy->sub)->offset) & 0xFFF8;

	return (cut_bid > limit) ? limit : cut_bid;
}

static key* _tk_create_root_key(struct _tk_setup* setup, key* copy, int cut_adj) {
	// copy first/root key
	key* root = (key*) ((char*) setup->dest + sizeof(page));

	root->offset = root->next = root->sub = 0;
	root->length = copy->length - (cut_adj & 0xFFF8);

	memcpy(KDATA(root), KDATA(copy) + (cut_adj >> 3), CEILBYTE(root->length));

	setup->dest->used = sizeof(page) + sizeof(key) + CEILBYTE(root->length);

	return root;
}

static uint _tk_cut_key(struct _tk_setup* setup, page_wrap* pw, key* copy,
		key* prev, int cut_bid) {
	int cut_adj = _tk_adjust_cut(pw, copy, prev, cut_bid);

	key* root = _tk_create_root_key(setup, copy, cut_adj);

	// cut-off 'copy'
	copy->length = cut_adj;

	// start compact-copy
	setup->o_pt = setup->l_pt = 0;
	_tk_compact_copy(setup, pw, root, &root->sub, (prev != 0) ? prev->next
			: copy->sub, -(cut_adj & 0xFFF8));

	// link ext-pointer to new page
	if (prev != 0)
		prev->next = _tk_link_and_create_page(setup, pw, cut_adj);
	else
		copy->sub = _tk_link_and_create_page(setup, pw, cut_adj);

	return (sizeof(ptr) * 8);
}

static uint _tk_measure(struct _tk_setup* setup, page_wrap* pw, key* parent,
		ushort kptr) {
	key* k = GOOFF(pw,kptr);
	uint size = (k->next == 0) ? 0 : _tk_measure(setup, pw, parent, k->next);
	k = GOOFF(pw,kptr); //if mem-ptr _ptr_alloc might have changed it

	// parent over k->offset
	if (parent != 0)
		while (1) {
			int cut_offset = size + parent->length - k->offset + ((sizeof(key)
					+ 1) * 8) - setup->halfsize;
			if (cut_offset <= 0) // upper-cut
				break;
			size = _tk_cut_key(setup, pw, parent, k, cut_offset + k->offset);
		}

	if (ISPTR(k)) {
		ptr* pt = (ptr*) k;
		uint subsize;
		if (pt->koffset != 0)
			subsize = _tk_measure(setup, (page_wrap*) pt->pg, 0, pt->koffset);
		else if (setup->id == pt->pg)
			subsize = _tk_measure(setup, setup->pg, 0, sizeof(page));
		else
			subsize = (sizeof(ptr) * 8);

		return size + subsize;
	} else // cut k below limit (length | sub->offset)
	{
		uint subsize = (k->sub == 0) ? 0 : _tk_measure(setup, pw, k, k->sub);// + size);

		while (1) {
			int cut_offset = subsize + k->length + ((sizeof(key) + 1) * 8)
					- setup->halfsize;
			//			int cut_offset = subsize + k->length + (sizeof(key)*8) - setup->fullsize;
			if (cut_offset < 0)
				break;
			subsize = _tk_cut_key(setup, pw, k, 0, cut_offset);// + size;
		}

		size += subsize;
		//		size = subsize;
	}

	return size + k->length + ((sizeof(key) + 1) * 8);
}

int tk_commit_task(task* t) {
	page_wrap* pgw = t->wpages;
	int ret = 0;
	int have_pages_written = 0;

	while (pgw != 0) {
		page_wrap* tmp = pgw->next;
		page* pg = pgw->pg;

		// written to?
		if (pg->id == 0) {
			have_pages_written = 1;

			/* overflowed or cluttered? */
			if (pgw->ovf || pg->waste > pg->size / 2) {
				cle_pageid parent_1 = 0;
				struct _tk_setup setup;
				setup.dest = tk_malloc(t, pg->size);
				setup.dest->size = pg->size;
				setup.dest->waste = 0;

				setup.fullsize = (pg->size - sizeof(page)) << 3;
				setup.halfsize = setup.fullsize >> 1;
				setup.fullsize -= setup.halfsize >> 1;

				setup.t = t;

				// pick up parent and rebuild from there (if not root)
				if (pgw->parent_0 != 0) {
					parent_1 = pgw->parent_1;
					setup.id = pgw->ext_pageid;
					setup.pg = pgw;
					pgw = _tk_load_page(t, pgw->parent_0, &rt_wrapper);

					// make parent writable
					//if(pgw->pg->id != 0)
					//_tk_write_copy(t,pgw);
				} else {
					setup.id = 0;
					setup.pg = 0;
				}

				_tk_measure(&setup, pgw, 0, sizeof(page));

				while (1) {
					// reset and copy remaining rootpage
					ushort sub = 0;
					setup.dest->used = sizeof(page);
					_tk_compact_copy(&setup, pgw, 0, &sub, sizeof(page), 0);

					// dont rebuild again
					pgw->pg->id = pgw->ext_pageid;

					// rest is 'too small' (less than quarter page) [prevents ptr-to-ptr]
					if (setup.dest->used < (setup.halfsize / 16) && parent_1
							!= 0) {
						page_wrap* ppw = _tk_load_page(t, parent_1, &rt_wrapper);

						if (ppw->pg->used - ppw->pg->waste + setup.dest->used
								- sizeof(ptr) - sizeof(page) < setup.dest->size) {
							setup.id = pgw->ext_pageid;
							setup.pg = pgw;
							parent_1 = ppw->parent_0;

							if (pgw->pg->id != 0)
								pgw->pg = (page*) tk_alloc(t, setup.dest->used,
										0);

							memcpy(pgw->pg, setup.dest, setup.dest->used);

							// mem-pager: destroys pgw->pg ... make-writable (see prev note)
							//						t->ps->remove_page(t->psrc_data,setup.id);

							pgw = ppw;
							if (pgw->pg->id != 0)
								_tk_write_copy(t, pgw);
						}
						else
							break;
					} else
						break;
				}

				t->ps->write_page(t->psrc_data, pgw->ext_pageid, setup.dest);

				// release old (now rebuilded) page (if we found parent)
				//				if(setup.id != 0)
				//					t->ps->remove_page(t->psrc_data,setup.id);

				tk_mfree(t, setup.dest);
			} else
				/* just write it */
				t->ps->write_page(t->psrc_data, pgw->ext_pageid, pg);

			ret = t->ps->pager_error(t->psrc_data);
			if (ret != 0)
				break;
		}

		pgw = tmp;
	}

	if (have_pages_written) {
		if (ret == 0)
			ret = t->ps->pager_commit(t->psrc_data);
		else
			t->ps->pager_rollback(t->psrc_data);
	}

	tk_drop_task(t);

	return ret;
}
