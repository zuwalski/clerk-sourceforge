/*
 Clerk application and storage engine.
 Copyright (C) 2013  Lars Szuwalski

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

#include <string.h>

#include "cle_struct.h"

struct _cmt_base {
	task* t;
	ushort* stack;
	page* cpg;
	char* dest;
	char* dest_begin;
	ushort* parent_key;
	uint idx;
	uint idx_max;
	ushort target;
	ushort size;
	ushort r;
	ushort n;

	ushort* cut_link;
	st_ptr cut;
	ushort cut_size;
};

static const key EMPTY = { 0, 0, 0, 0 };

#define STACK_GROW 256

#define ALIGN2(a) ((a) + ((uint)(a) & 1))

static void _cmt_push_key(struct _cmt_base* const b, ushort k) {
	if (b->idx == b->idx_max) {
		b->idx_max += STACK_GROW;
		b->stack = tk_realloc(b->t, b->stack, sizeof(ushort) * b->idx_max);
	}

	b->stack[b->idx++] = k;
}

static key* _cmt_alloc(struct _cmt_base* const b, uint size) {
	key* r;
	b->dest += (long long) b->dest & 1;
	r = (key*) b->dest;

	memset(r, 0, sizeof(key));

	r->next = *b->parent_key;
	*b->parent_key = b->dest - b->dest_begin;

	b->dest += size;
	return r;
}

static void _cmt_copy_key(struct _cmt_base* const b, const key* k, const int adj) {
	const uint cp_len = CEILBYTE(k->length - adj);
	memcpy(b->dest, KDATA(k) + (adj >> 3), cp_len);
	b->dest += cp_len;
}

static void _cmt_pop_page(struct _cmt_base* const b) {
	if (b->stack[b->idx - 1] == 0xFFFF) {
		b->cpg = b->cpg->parent;
		b->idx--;
	}
}

static void _cmt_copy_page(struct _cmt_base* const b, page* cpg, ushort r, ushort n, short adjoff) {
	const key* kp, *rt = GOKEY(cpg, r);
	key* nk;
	const uint stop = b->idx;
	ushort tmp = 0;

	if (adjoff == 0)
		n = rt->sub;

	// push root-linked keys
	while (n != 0) {
		_cmt_push_key(b, r);
		_cmt_push_key(b, n);
		kp = GOOFF(cpg,n);
		n = kp->next;
	}

	// allocate and copy root key
	b->parent_key = &tmp;
	nk = _cmt_alloc(b, sizeof(key));

	adjoff = (adjoff & 0xFFF8);

	nk->length = rt->length - adjoff;

	_cmt_copy_key(b, rt, adjoff);

	adjoff *= -1;
	b->parent_key = &nk->sub;

	// tree copy rest of page-content
	while (b->idx != stop) {
		_cmt_pop_page(b);

		n = b->stack[--b->idx];

		while (n == 0) {
			adjoff = b->stack[--b->idx];

			r = b->stack[--b->idx];
			b->parent_key = (ushort*) (b->dest_begin + r);

			n = b->stack[--b->idx];
		}

		r = b->stack[--b->idx];

		rt = GOOFF(cpg,r);
		kp = GOOFF(cpg,n);

		if (ISPTR(kp)) {
			ptr* p = (ptr*) kp;

			if (p->koffset == 0) {
				ptr* nptr = (ptr*) _cmt_alloc(b, sizeof(ptr));

				nptr->ptr_id = PTR_ID;
				nptr->offset = p->offset + adjoff;
				nptr->pg = p->pg;

				kp = &EMPTY;
			} else {
				_cmt_push_key(b, 0xFFFF);

				((page*) p->pg)->parent = cpg;
				cpg = p->pg;

				kp = GOOFF(cpg, p->koffset);
			}
		}

		// if not continue-key
		if (rt->length != kp->offset) {
			if (rt->length != 0) {
				// create new key
				nk = _cmt_alloc(b, sizeof(key));

				nk->offset = kp->offset + adjoff;
			}
		} else {
			adjoff += kp->offset;

			if (kp->offset & 7)
				b->dest--;
		}

		_cmt_copy_key(b, kp, 0);

		nk->length += kp->length;

		if ((r = kp->sub)) {
			_cmt_push_key(b, (char*) b->parent_key - b->dest_begin);
			_cmt_push_key(b, adjoff);
			_cmt_push_key(b, 0);

			b->parent_key = &nk->sub;
			adjoff = 0;

			do {
				_cmt_push_key(b, n);
				_cmt_push_key(b, r);
				kp = GOOFF(cpg,r);
			} while ((r = kp->next));
		}
	}
}

static void _cmt_set_cut(struct _cmt_base* const b, ushort key, ushort* link, ushort offset) {
	b->cut.key = key;
	b->cut.pg = b->cpg;
	b->cut_link = link;
	b->cut_size = ALIGN2(b->size);
	b->cut.offset = offset;
}

static void _cmt_cut_between(struct _cmt_base* const b, key* cut, ushort* link, const ushort low) {
	const ushort high = (*link) ? GOOFF(b->cpg, *link)->offset : cut->length;
	const ushort k = (char*) cut - (char*) b->cpg;
	int optimal_cut;

	if (b->size + sizeof(key) + ((cut->length - high) >> 3) > b->target) {
		// execute last cut
		_cmt_copy_page(b, b->cut.pg, b->cut.key, *b->cut_link, b->cut.offset);

		b->size -= b->cut_size;
	}

	optimal_cut = (int) cut->length - (int) ((b->target - b->size - sizeof(key)) << 3);

	if (optimal_cut >= low) {

		_cmt_copy_page(b, b->cpg, k, *link, optimal_cut);

		b->size = 0;
	}

	_cmt_set_cut(b, k, link, low);
}

static void _cmt_cut_over(struct _cmt_base* const b, ushort cut_key, ushort cut_over_key) {
	key* over = GOOFF(b->cpg, cut_over_key);
	key* cut = GOOFF(b->cpg, cut_key);
	const uint cut_size = cut->length - over->offset;

	if (cut_size == 0) {
		// key-continue (no cut-over)
		b->size -= (b->size > sizeof(key)) ? sizeof(key) : b->size;

	} else if (b->size + sizeof(key) + CEILBYTE(cut_size) < b->target) {

		_cmt_set_cut(b, cut_key, &over->next, over->offset + 1);
	} else
		_cmt_cut_between(b, cut, &over->next, over->offset + 1);
}

static void _cmt_cut_low(struct _cmt_base* const b, ushort cut_key) {
	key* cut = GOOFF(b->cpg, cut_key);
	const uint size_all = b->size + sizeof(key) + CEILBYTE(cut->length);

	if (size_all < b->target) {
		b->size = size_all;

		_cmt_set_cut(b, cut_key, &cut->sub, 0);
	} else {
		_cmt_cut_between(b, cut, &cut->sub, 0);

		b->size += sizeof(key) + ALIGN2(CEILBYTE(cut->length));
	}
}

static int _cmt_pop(struct _cmt_base* const b) {
	const ushort pr = b->r;
	const ushort pn = b->n;

	_cmt_pop_page(b);

	b->n = b->stack[--b->idx];
	b->r = b->stack[--b->idx];

	// if tn didn't promote to root it's stand-alone
	if (b->r != pn)
		_cmt_cut_low(b, pn);

	if (b->r != pr) {
		// root changed
		if (b->r == pn) {
			// push to new branch
			b->size = 0;
		} else {
			// pop from branch
			b->size += b->stack[--b->idx];

			_cmt_cut_low(b, pr);
		}
	}

	// measure cut on root over b->n->offset
	if (b->r)
		_cmt_cut_over(b, b->r, b->n);

	return b->r;
}

static const key* _cmt_ptr(struct _cmt_base* const b, ptr* pt) {
	if (pt->koffset == 0) {
		b->size += sizeof(ptr);
		return &EMPTY;
	}

	// mark page-change
	_cmt_push_key(b, 0xFFFF);

	((page*) pt->pg)->parent = b->cpg;
	b->cpg = pt->pg;

	return GOOFF(b->cpg,pt->koffset);
}

static void _cmt_measure(struct _cmt_base* const b, page* cpg, uint n) {
	b->n = b->r = n;
	b->cpg = cpg;
	b->size = 0;
	b->idx = 0;

	_cmt_set_cut(b, 0, 0, n);

	_cmt_push_key(b, 0);
	_cmt_push_key(b, 0);

	do {
		const key* kp = GOOFF(b->cpg,b->n);

		if (ISPTR(kp))
			kp = _cmt_ptr(b, (ptr*) kp);

		if ((n = kp->sub)) {
			_cmt_push_key(b, 0);
			b->stack[b->idx - 1] = b->stack[b->idx - 2];
			b->stack[b->idx - 2] = b->stack[b->idx - 3];
			b->stack[b->idx - 3] = b->size;

			do {
				_cmt_push_key(b, b->n);
				_cmt_push_key(b, n);
				kp = GOOFF(b->cpg,n);
			} while ((n = kp->next));
		}
	} while (_cmt_pop(b));
}

static page* _cmt_mark_and_link(struct _cmt_base* b, page* cpg, page* find) {
	page* org;
	ptr* pt;
	ushort k = sizeof(page);

	b->idx = 0;

	while (1) {
		key* kp = GOOFF(cpg,k);

		if (ISPTR(kp)) {
			pt = (ptr*) kp;

			if (pt->koffset == 0 && pt->pg == find->id)
				break;
		}

		if (ISPTR(kp) == 0 && kp->sub != 0) {
			// push content step
			_cmt_push_key(b, k);
			k = kp->sub;
		} else if (kp->next != 0)
			k = kp->next;
		else {
			// pop content step
			do {
				if (b->idx == 0)
					return 0;	// link not found (deleted) => stop

				k = b->stack[--b->idx];
				kp = GOOFF(cpg,k);
				k = kp->next;
			} while (k == 0);
		}
	}

	// is it writable? Make sure
	org = _tk_check_page(b->t, cpg);

	if (org == org->id)
		cpg = _tk_write_copy(b->t, cpg);
	else
		cpg = org;

	pt = (ptr*) GOOFF(cpg,k);

	pt->koffset = sizeof(page);
	pt->pg = find;

	// fix offset like mem-ptr
	GOKEY(find,sizeof(page)) ->offset = pt->offset;

	return org != org->id ? 0 : cpg;
}

/**
 * Rebuild all changes into new root => create new db-version and switch to it.
 *
 * return 0 if ok - also if no changes was written.
 */
int cmt_commit_task(task* t) {
	struct _cmt_base base;
	task_page* tp;
	page* root = 0;

	base.t = t;
	base.stack = 0;
	base.idx_max = base.idx = 0;

	// link up all pages back to root (and make them writable)
	for (tp = t->wpages; tp != 0; tp = tp->next) {
		page *rpg, *parent, *find = &tp->pg;

		for (parent = tp->pg.parent; parent != 0; find = parent, parent = parent->parent) {
			rpg = _cmt_mark_and_link(&base, parent, find);
			if (rpg == 0)
				break;
		}

		// touched root?
		if (rpg != 0)
			root = rpg;
	}

	// rebuild from root => next root
	if (root) {
		base.target = root->size - sizeof(page);
		// start measure from root
		_cmt_measure(&base, root, sizeof(page));

		// copy "rest" to new root
		_cmt_copy_page(&base, root, sizeof(page), 0, 0);
	}

	tk_mfree(t, base.stack);

	tk_drop_task(t);

	return 0;
}

void test_copy(task* t, page* dst, st_ptr src) {
	struct _cmt_base base;

	base.t = t;
	base.stack = 0;
	base.idx_max = base.idx = 0;
	base.target = dst->size;
	base.dest_begin = (char*) dst;
	base.dest = base.dest_begin + sizeof(page);

	_cmt_copy_page(&base, src.pg, src.key, 0, 0);

	dst->used = base.dest - base.dest_begin;
}
