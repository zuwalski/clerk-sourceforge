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

#include "cle_struct.h"

// TODO cle_task.c : from recursive commit/free to looping

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

static task_page* _tk_alloc_page(task* t, uint page_size) {
	task_page* pg = (page*) tk_malloc(t, page_size + sizeof(task_page) - sizeof(page));

	pg->pg.id = 0;
	pg->pg.size = page_size;
	pg->pg.used = sizeof(page);
	pg->pg.waste = 0;
	pg->pg.parent = 0;

	pg->refcount = 1;
	pg->next = t->stack;
	pg->ovf = 0;
	return pg;
}

void _tk_stack_new(task* t) {
	t->stack = _tk_alloc_page(t, PAGE_SIZE);
}

void* tk_alloc(task* t, uint size, struct page** pgref) {
	task_page* pg = t->stack;
	uint offset;

	if (pg->pg.used + size + 7 > PAGE_SIZE) {
		if (size > PAGE_SIZE - sizeof(page)) {
			// big chunk - alloc specific
			task_page* tmp = _tk_alloc_page(t, size + sizeof(page));

			tmp->pg.used = tmp->pg.size;

			tmp->next = t->stack->next;
			t->stack->next = tmp;

			if (pgref != 0)
				*pgref = tmp;
			return (void*) ((char*) tmp + sizeof(task_page));
		}

		_tk_stack_new(t);
		pg = t->stack;
	}

	// align 8 (avoidable? dont use real pointers from here - eg. always copy?)
	if (pg->pg.used & 7)
		pg->pg.used += 8 - (pg->pg.used & 7);

	offset = pg->pg.used;
	pg->pg.used += size;

	t->stack->refcount++;

	if (pgref != 0)
		*pgref = &t->stack->pg;
	return (void*) ((char*) &pg->pg + offset);
}

static void _tk_release_page(task* t, task_page* wp) {

}

// TODO tag pointer as loaded - use bit 0 set to 1 when loaded.
static page* _tk_load_page(task* t, cle_pageid pid, page* parent) {
	st_ptr root_ptr = t->pagemap;
	page* pw;

	// have a writable copy of the page?
	if (t->wpages == 0 || st_move(t, &root_ptr, (cdat) &pid, sizeof(pid))) {
		// TODO get from pager (if first time)
		pw = (page*) pid;

		if (parent != 0 && pw->parent == 0)
			pw->parent = parent;
	}
	// found: read address of page-copy
	else if (st_get(t, &root_ptr, (char*) &pw, sizeof(pw)) != -1)
		cle_panic(t); // map corrupted

	return pw;
}

static page* _tk_load_orig(task* t, page* p) {
	if (p->id)
		p = (p->id == ROOT_ID ) ? t->ps->root_page(t->psrc_data) : (page*) p->id;

	return p;
}

page* _tk_check_page(task* t, page* pw) {
	if (pw->id != 0 && t->wpages != 0) {
		st_ptr root_ptr = t->pagemap;

		// have a writable copy of the page?
		if (st_move(t, &root_ptr, (cdat) &pw->id, sizeof(cle_pageid)) == 0)
			if (st_get(t, &root_ptr, (char*) &pw, sizeof(pw)) != -1)
				cle_panic(t); // map corrupted
	}
	return pw;
}

page* _tk_check_ptr(task* t, st_ptr* pt) {
	pt->pg = _tk_check_page(t, pt->pg);
	return pt->pg;
}

/* copy to new (internal) page */
page* _tk_write_copy(task* t, page* pg) {
	st_ptr root_ptr;
	task_page* npg;

	if (pg->id == 0)
		return pg;

	// add to map of written pages
	root_ptr = t->pagemap;

	if (st_insert(t, &root_ptr, (cdat) &pg->id, sizeof(cle_pageid)) == 0) {
		if (st_get(t, &root_ptr, (char*) &pg, sizeof(pg)) != -1)
			cle_panic(t); // map corrupted
		return pg;
	}

	npg = _tk_alloc_page(t, pg->size);

	memcpy(&npg->pg, pg, pg->used + (8 & (7 + (7 & pg->used))));

	// pg in written pages list
	npg->next = t->wpages;
	t->wpages = npg;

	pg = &npg->pg;

	st_append(t, &root_ptr, (cdat) &pg, sizeof(page*));

	return pg;
}

key* _tk_get_ptr(task* t, page** pg, key* me) {
	ptr* pt = (ptr*) me;
	if (pt->koffset != 0) {
		*pg = (page*) pt->pg;
		me = GOKEY(*pg,pt->koffset); /* points to a key - not an ovf-ptr */
	} else {
		*pg = _tk_load_page(t, pt->pg, *pg);
		/* go to root-key */
		me = GOKEY(*pg,sizeof(page));
	}
	return me;
}

ushort _tk_alloc_ptr(task* t, task_page* pg) {
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

void _tk_remove_tree(task* t, page* pg, ushort off) {

}

void tk_unref(task* t, struct page* pg) {
	if (pg->id == 0) {
		task_page* tp = TO_TASK_PAGE(pg);

		if (--tp->refcount == 0)
			_tk_release_page(t, tp);
	}
}

void tk_free_ptr(task* t, st_ptr* ptr) {
	tk_unref(t, ptr->pg);
}

void tk_ref_ptr(st_ptr* ptr) {
	if (ptr->pg->id == 0)
		TO_TASK_PAGE(ptr->pg) ->refcount++;
}

void tk_root_ptr(task* t, st_ptr* pt) {
	key* k;
	_tk_check_ptr(t, &t->root);

	k = GOKEY(t->root.pg,t->root.key);
	if (ISPTR(k)) {
		k = _tk_get_ptr(t, &t->root.pg, k);
		t->root.key = (ushort) ((char*) k - (char*) t->root.pg);
	}

	*pt = t->root;
}

void tk_dup_ptr(st_ptr* to, st_ptr* from) {
	*to = *from;
	tk_ref_ptr(to);
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

	t->stack = 0;
	t->wpages = 0;
	t->segment = 1; // TODO get from pager
	t->ps = ps;
	t->psrc_data = psrc_data;

	_tk_stack_new(t);

	st_empty(t, &t->pagemap);

	if (ps) {
		t->root.pg = ps->root_page(psrc_data);
		t->root.key = sizeof(page);
		t->root.offset = 0;
	} else {
		st_empty(t, &t->root);
	}

	return t;
}

task* tk_clone_task(task* parent) {
	return tk_create_task(parent->ps, (parent->ps == 0) ? 0 : parent->ps->pager_clone(parent->psrc_data));
}

static void _tk_free_page_list(task_page* pw) {
	while (pw) {
		task_page* next = pw->next;

		tk_mfree(0, pw->ovf);
		tk_mfree(0, pw);

		pw = next;
	}
}

void tk_drop_task(task* t) {
	_tk_free_page_list(t->stack);

	_tk_free_page_list(t->wpages);

	// quit the pager here
	if (t->ps != 0)
		t->ps->pager_close(t->psrc_data);

	// last: free initial alloc
	tk_mfree(0, t);
}

/////////////////////////////// Sync v1 ///////////////////////////////////

struct trace_ptr {
	st_ptr* base;
	st_ptr ptr;
};

struct _tk_trace_page_hub {
	struct _tk_trace_page_hub* next;
	page* pgw;
	uint from, to;
};

struct _tk_trace_base {
	struct _tk_trace_page_hub* free;
	struct _tk_trace_page_hub* chain;
	ushort* kstack;
	task* t;
	uint ssize;
	uint sused;
};

static void _tk_base_reset(struct _tk_trace_base* base) {
	base->sused = 0;

	while (base->chain != 0) {
		struct _tk_trace_page_hub* n = base->chain->next;

		base->chain->next = base->free;
		base->free = base->chain;

		base->chain = n;
	}
}

static struct _tk_trace_page_hub* _tk_new_hub(struct _tk_trace_base* base, page_wrap* pgw, uint from, uint to) {
	struct _tk_trace_page_hub* r;
	if (base->free != 0) {
		r = base->free;
		base->free = r->next;
	} else {
		r = tk_alloc(base->t, sizeof(struct _tk_trace_page_hub), 0);
	}

	r->next = 0;
	r->pgw = pgw;
	r->from = from;
	r->to = to;

	return r;
}

static void _tk_push_key(struct _tk_trace_base* base, ushort k) {
	if (base->sused == base->ssize) {
		base->ssize += PAGE_SIZE;
		base->kstack = tk_realloc(base->t, base->kstack, base->ssize * sizeof(ushort));
	}

	base->kstack[base->sused++] = k;
}

static st_ptr _tk_trace_write(struct _tk_trace_base* base, page* pgw, st_ptr ptr, uint from, uint to, ushort offset) {
	if (from != to) {
		key* kp = GOOFF(pgw,base->kstack[from]);
		uchar* dat = KDATA(kp);
		uint i;

		for (i = from + 1; i < to; i++) {
			kp = GOOFF(pgw,base->kstack[i]);

			st_insert(base->t, &ptr, dat, kp->offset >> 3);

			dat = KDATA(kp);
		}

		st_insert(base->t, &ptr, dat, offset >> 3);
	}

	return ptr;
}

static st_ptr _tk_trace(struct _tk_trace_base* base, page* pgw, struct trace_ptr* pt, uint start_depth, ushort offset) {
	if (pt->ptr.pg == 0) {
		struct _tk_trace_page_hub* lead = base->chain;

		pt->ptr = *pt->base;

		while (lead != 0) {
			key* k = GOOFF(lead->pgw,base->kstack[lead->to]);

			pt->ptr = _tk_trace_write(base, lead->pgw, pt->ptr, lead->from, lead->to, k->offset);

			lead = lead->next;
		}
	}

	return _tk_trace_write(base, pgw, pt->ptr, start_depth, base->sused, offset);
}

static void _tk_insert_trace(struct _tk_trace_base* base, page* pgw, struct trace_ptr* insert_tree, uint start_depth,
		page* lpgw, ushort lkey, ushort offset) {
	// round up - make sure the last (diff'ed on) byte is there
	st_ptr root = _tk_trace(base, pgw, insert_tree, start_depth, offset + 7);

	ushort pt = _tk_alloc_ptr(base->t, TO_TASK_PAGE(root.pg) );

	key* kp = GOOFF(root.pg,root.key);

	ptr* ptrp = (ptr*) GOPTR(root.pg,pt);

	ptrp->pg = lpgw;
	ptrp->koffset = lkey;
	// the diff was on the last byte - adjust the offset
	ptrp->offset = root.offset - (8 - (offset & 7));

	ptrp->next = 0;
	ptrp->ptr_id = PTR_ID;

	if (kp->sub == 0) {
		kp->sub = pt;
	} else {
		kp = GOOFF(root.pg,kp->sub);

		while (kp->next != 0) {
			kp = GOOFF(root.pg,kp->next);
		}

		kp->next = pt;
	}
}

static void _tk_delete_trace(struct _tk_trace_base* base, page_wrap* pgw, struct trace_ptr* del_tree, uint start_depth,
		ushort found, ushort expect) {
	const page* orig = _tk_load_orig(base->t, pgw);
	const ushort lim = orig->used;
	// skip new keys
	while (found >= lim) {
		key* kp = GOOFF(pgw,found);

		found = kp->next;
	}

	while (found != expect) {
		// expect was deleted
		key* ok = (key*) ((char*) orig + expect);

		st_ptr root = _tk_trace(base, pgw, del_tree, start_depth, ok->offset);

		st_insert(base->t, &root, KDATA(ok), 1);

		expect = ok->next;
	}
}

static void _tk_trace_change(struct _tk_trace_base* base, page* pgw, struct trace_ptr* delete_tree,
		struct trace_ptr* insert_tree) {
	const page* orig = _tk_load_orig(base->t, pgw);
	const uint start_depth = base->sused, lim = orig->used;
	uint k = sizeof(page);

	while (1) {
		key* kp = GOOFF(pgw,k);

		// new key
		if (k >= lim) {
			if (ISPTR(kp)) {
				ptr* pt = (ptr*) kp;
				_tk_insert_trace(base, pgw, insert_tree, start_depth, pt->pg, pt->koffset, pt->offset);
			} else
				_tk_insert_trace(base, pgw, insert_tree, start_depth, pgw, k, kp->offset);
		} else {
			// old key - was it changed? (deletes only)
			key* ok = (key*) ((char*) orig + k);

			// changed next
			if (kp->next != ok->next)
				_tk_delete_trace(base, pgw, delete_tree, start_depth, kp->next, ok->next);

			if (ISPTR(kp) == 0) {
				// push content step
				_tk_push_key(base, k);

				// changed sub
				if (kp->sub != ok->sub)
					_tk_delete_trace(base, pgw, delete_tree, start_depth, kp->sub, ok->sub);

				// shortend key
				if (kp->length < ok->length) {
					st_ptr root = _tk_trace(base, pgw, delete_tree, start_depth, kp->length);

					st_insert(base->t, &root, KDATA(ok) + (kp->length >> 3), 1);
				}
				// appended to
				else if (kp->length > ok->length)
					_tk_trace(base, pgw, insert_tree, start_depth, kp->length + 7);

				if (kp->sub != 0) {
					k = kp->sub;
					// keep k on stack
					continue;
				}

				base->sused--;
			}
		}

		if (kp->next != 0)
			k = kp->next;
		else {
			// pop content step
			do {
				if (start_depth == base->sused)
					return;

				k = base->kstack[--base->sused];
				kp = GOOFF(pgw,k);
				k = kp->next;
			} while (k == 0);
		}
	}
}

static struct _tk_trace_page_hub* _tk_trace_page_ptr(struct _tk_trace_base* base, page* pgw, page* find) {
	const uint start_depth = base->sused;
	uint k = sizeof(page);

	while (1) {
		key* kp = GOOFF(pgw,k);

		if (ISPTR(kp)) {
			ptr* pt = (ptr*) kp;
			if (pt->koffset == 0 && pt->pg == find->id) {
				struct _tk_trace_page_hub* h;
				const uint to = base->sused + 1;

				_tk_push_key(base, k);

				if (pgw->parent != 0) {
					struct _tk_trace_page_hub* r = _tk_trace_page_ptr(base, pgw->parent, pgw);
					if (r == 0)
						return 0;

					h = _tk_new_hub(base, pgw, start_depth, to);
					r->next = h;
				} else {
					h = _tk_new_hub(base, pgw, start_depth, to);
					base->chain = h;
				}

				return h;
			}
		}

		if (ISPTR(kp) == 0 && kp->sub != 0) {
			// push content step
			_tk_push_key(base, k);
			k = kp->sub;
		} else if (kp->next != 0)
			k = kp->next;
		else {
			// pop content step
			do {
				if (start_depth == base->sused)
					return 0;

				k = base->kstack[--base->sused];
				kp = GOOFF(pgw,k);
				k = kp->next;
			} while (k == 0);
		}
	}
}

// returns res & 1 => deletes , res & 2 => inserts
int tk_delta(task* t, st_ptr* delete_tree, st_ptr* insert_tree) {
	struct _tk_trace_base base;
	task_page* pgw;
	int res = 0;

	base.sused = base.ssize = 0;
	base.chain = base.free = 0;
	base.kstack = 0;
	base.t = t;

	for (pgw = t->wpages; pgw != 0; pgw = pgw->next) {
		if (pgw->pg.parent == 0 || _tk_trace_page_ptr(&base, pgw->pg.parent, &pgw->pg)) {
			struct trace_ptr t_delete, t_insert;

			t_delete.base = delete_tree;
			t_insert.base = insert_tree;
			t_delete.ptr.pg = t_insert.ptr.pg = 0;

			_tk_trace_change(&base, &pgw->pg, &t_delete, &t_insert);
			res |= (t_delete.ptr.pg != 0) | (t_insert.ptr.pg != 0) << 1;
		}

		_tk_base_reset(&base);
	}

	tk_mfree(t, base.kstack);

	return res;
}

/////////////////////////////// Commit v4 /////////////////////////////////
struct _tk_setup {
	page* dest;
	task* t;

	uint halfsize;
	uint fullsize;

	ushort o_pt;
	ushort l_pt;
};

static void _tk_compact_copy(struct _tk_setup* setup, page* pw, key* parent, ushort* rsub, ushort next, int adjoffset) {
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
			adjoffset += k->offset;
		else if ((parent != 0) && (k->offset + adjoffset == parent->length)) // append to parent key?
				{
			adjoffset = parent->length & 0xFFF8; // 'my' subs are offset by parent-length

			memcpy(KDATA(parent) + (parent->length >> 3), KDATA(k), CEILBYTE(k->length));
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

static ushort _tk_link_and_create_page(struct _tk_setup* setup, page* pw, int ptr_offset) {
	// first: create a link to new page
	ptr* pt;
	ushort pt_offset;

	// use exsisting key?
	if (setup->l_pt + sizeof(key) * 8 >= sizeof(ptr) * 8) {
		pt_offset = setup->o_pt;
		pt = (ptr*) GOOFF(pw,pt_offset);
	}
	// room for ptr in page?
	else if (pw->used + sizeof(ptr) <= pw->size) {
		pt_offset = pw->used;
		pt = (ptr*) GOKEY(pw,pt_offset);
		pw->used += sizeof(ptr);
	} else {
		pt_offset = _tk_alloc_ptr(setup->t, TO_TASK_PAGE(pw) ); // might change ptr-address!
		pt = (ptr*) GOPTR(pw,pt_offset);
	}

	pt->offset = ptr_offset;
	pt->ptr_id = PTR_ID;
	pt->koffset = pt->next = 0;

	// then: create new page (copy dest) and put pageid in ptr
	pt->pg = setup->t->ps->new_page(setup->t->psrc_data, setup->dest);

	return pt_offset;
}

static int _tk_adjust_cut(page* pw, key* copy, key* prev, int cut_bid) {
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

static uint _tk_cut_key(struct _tk_setup* setup, page* pw, key* copy, key* prev, int cut_bid) {
	int cut_adj = _tk_adjust_cut(pw, copy, prev, cut_bid);

	key* root = _tk_create_root_key(setup, copy, cut_adj);

	// cut-off 'copy'
	copy->length = cut_adj;

	// start compact-copy
	setup->o_pt = setup->l_pt = 0;
	_tk_compact_copy(setup, pw, root, &root->sub, (prev != 0) ? prev->next : copy->sub, -(cut_adj & 0xFFF8));

	// link ext-pointer to new page
	if (prev != 0)
		prev->next = _tk_link_and_create_page(setup, pw, cut_adj);
	else
		copy->sub = _tk_link_and_create_page(setup, pw, cut_adj);

	return (sizeof(ptr) * 8);
}

static uint _tk_measure(struct _tk_setup* setup, page* pw, key* parent, ushort kptr) {
	key* k = GOOFF(pw,kptr);
	uint size = (k->next == 0) ? 0 : _tk_measure(setup, pw, parent, k->next);
	k = GOOFF(pw,kptr); //if mem-ptr _ptr_alloc might have changed it

	// parent over k->offset
	if (parent != 0)
		while (1) {
			int cut_offset = size + parent->length - k->offset + ((sizeof(key) + 1) * 8) - setup->halfsize;
			if (cut_offset <= 0) // upper-cut
				break;
			size = _tk_cut_key(setup, pw, parent, k, cut_offset + k->offset);
		}

	if (ISPTR(k)) {
		ptr* pt = (ptr*) k;
		uint subsize;
		if (pt->koffset != 0)
			subsize = _tk_measure(setup, (page*) pt->pg, 0, pt->koffset);
		else
			subsize = (sizeof(ptr) * 8);

		return size + subsize;
	} else // cut k below limit (length | sub->offset)
	{
		uint subsize = (k->sub == 0) ? 0 : _tk_measure(setup, pw, k, k->sub); // + size);
		const uint target_size = (sizeof(key) * 8) - (k->length > setup->halfsize ? setup->fullsize : setup->halfsize);

		while (1) {
			int cut_offset = subsize + k->length + target_size;
			//			int cut_offset = subsize + k->length + (sizeof(key)*8) - setup->halfsize;
			if (cut_offset < 0)
				break;
			subsize = _tk_cut_key(setup, pw, k, 0, cut_offset); // + size;
		}

		size += subsize;
		//		size = subsize;
	}

	return size + k->length + ((sizeof(key) + 1) * 8);
}

static uint _tk_to_mem_ptr(task* t, page* pw, page* to, ushort k) {
	while (k != 0) {
		key* kp = GOOFF(pw,k);

		if (ISPTR(kp)) {
			ptr* pt = (ptr*) kp;

			if (pt->koffset == 0 && pt->pg == to->id) {
				// is it writable? Make sure
				pw = _tk_write_copy(t, pw);
				pt = (ptr*) GOOFF(pw,k);

				pt->koffset = sizeof(page);
				pt->pg = to;

				// fix offset like mem-ptr
				GOKEY(to,sizeof(page)) ->offset = pt->offset;
				return 1;
			}
		} else if (_tk_to_mem_ptr(t, pw, to, kp->sub))
			return 1;

		k = kp->next;
	}

	return 0;
}

static uint _tk_link_written_pages(task* t, task_page* pgw) {
	uint max_size = 0;

	while (pgw != 0) {
		if (pgw->pg.size > max_size)
			max_size = pgw->pg.size;

		if (pgw->pg.parent != 0 && (pgw->ovf != 0 || pgw->pg.waste > pgw->pg.size / 2)) {
			page* parent = _tk_load_page(t, pgw->pg.parent->id, 0); // pgw->parent;

			// make mem-ptr from parent -> pg (if not found - link was deleted, then just dont build it)
			if (_tk_to_mem_ptr(t, parent, &pgw->pg, sizeof(page)))
				// force rebuild of parent
				parent->waste = parent->size;

			// this page will be rebuild from parent (or dont need to after all)
			pgw->refcount = 0;
			t->ps->remove_page(t->psrc_data, pgw->pg.id);
		}

		pgw = pgw->next;
	}

	return max_size;
}

int tk_commit_task(task* t) {
	struct _tk_setup setup;
	task_page* pgw;
	int ret = 0;

	uint max_size = _tk_link_written_pages(t, t->wpages);

	setup.t = t;

	for (pgw = t->wpages; pgw != 0 && ret == 0; pgw = pgw->next) {
		page* pg;

		if (pgw->refcount == 0)
			continue;

		/* overflowed or cluttered? */
		pg = &pgw->pg;
		if (pgw->ovf || pg->waste > pg->size / 2) {
			char bf[max_size];
			ushort sub = 0;

			setup.dest = (page*) bf;

			setup.dest->size = pg->size;
			setup.dest->waste = 0;

			setup.fullsize = (uint) (pg->size - sizeof(page)) << 3;
			setup.halfsize = setup.fullsize >> 1;
			setup.fullsize -= setup.halfsize >> 1;

			_tk_measure(&setup, pg, 0, sizeof(page));

			// reset and copy remaining rootpage
			setup.dest->used = sizeof(page);
			setup.dest->id = pg->id;

			_tk_compact_copy(&setup, pg, 0, &sub, sizeof(page), 0);

			t->ps->write_page(t->psrc_data, setup.dest->id, setup.dest);
		} else
			t->ps->write_page(t->psrc_data, pg->id, pg);

		ret = t->ps->pager_error(t->psrc_data);
	}

	if (ret != 0)
		t->ps->pager_rollback(t->psrc_data);
	else
		ret = t->ps->pager_commit(t->psrc_data);

	tk_drop_task(t);

	return ret;
}
