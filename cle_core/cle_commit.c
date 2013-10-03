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

struct _cmt_base {
	task* t;
	ushort* stack;
	uint idx;
	uint max;
	ushort r, n;
	page* cpg;
};

#define STACK_GROW 256

static void _tk_push_key(struct _cmt_base* b, ushort k) {
	if (b->max <= b->idx) {
		b->max += STACK_GROW;
		b->stack = tk_realloc(b->t, b->stack, sizeof(ushort) * b->max);
	}

	b->stack[b->idx++] = k;
}

static uint _tk_copy(struct _cmt_base* b, page* cpg, ushort k, ushort offset) {
	const uint stop = b->idx;

	while (1) {
		key* kp = GOOFF(cpg,k);

		if (ISPTR(kp)) {
			ptr* pt = (ptr*) kp;
		}

		if (ISPTR(kp) == 0 && kp->sub != 0) {
			// push content step
			b->stack[b->idx++] = k;
			k = kp->sub;
		} else if (kp->next != 0)
			k = kp->next;
		else {
			// pop content step
			do {
				if (b->idx == stop)
					return 1;	// link deleted => stop

				k = b->stack[--b->idx];
				kp = GOOFF(cpg,k);
				k = kp->next;
			} while (k == 0);
		}
	}

	return 0;
}

static int _pop(struct _cmt_base* b) {
	const ushort tr = b->r;
	const ushort tn = b->n;

	if (b->idx == 0) {
		b->n = b->r = 0;
	} else {
		b->n = b->stack[--b->idx];
		b->r = b->stack[--b->idx];
	}

	if (tn != b->r)
		printf("cut %d any\n", tn);

	if (tr != b->r) {
		if (b->r == tn)
			printf("push sum \n");
		else
			printf("cut %d low (add to pop sum)\n", tr);
	}

	if (b->r == 0)
		return 0;

	printf("cut %d over %d\n", b->r, b->n);
	return 1;
}

static void _tk_measure(struct _cmt_base* b, page* cpg, uint n) {
	b->n = b->r = n;
	b->cpg = cpg;
	b->idx = 0;

	do {
		key* kp = GOOFF(b->cpg,b->n);

		if (ISPTR(kp)) {
			ptr* pt = (ptr*) kp;
			if (pt->koffset != 0) {
				// push b->cpg
				b->cpg = pt->pg;
				kp = GOOFF(b->cpg,pt->koffset);
			}
		}

		for (n = kp->sub; n != 0; n = kp->next) {
			_tk_push_key(b, b->n);
			_tk_push_key(b, n);
			kp = GOOFF(b->cpg,n);
		}
	} while (_pop(b));
}

static page* _tk_mark_and_link(struct _cmt_base* b, page* cpg, page* find) {
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
			_tk_push_key(b, k);
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
int tk_commit_task2(task* t) {
	struct _cmt_base base;
	task_page* tp;
	page* root = 0;

	if (t->wpages == 0) {
		tk_drop_task(t);
		return 0;
	}

	base.t = t;
	base.stack = 0;
	base.max = base.idx = 0;

// link up all pages back to root (and make them writable)
	for (tp = t->wpages; tp != 0; tp = tp->next) {
		page *rpg, *parent, *find = &tp->pg;

		for (parent = tp->pg.parent; parent != 0; find = parent, parent = parent->parent) {
			rpg = _tk_mark_and_link(&base, parent, find);
			if (rpg == 0)
				break;
		}

		// touched root?
		if (rpg != 0)
			root = rpg;
	}

// rebuild from root => next root
	if (root) {
		// start measure from root
		_tk_measure(&base, root, sizeof(page));

		// copy "rest" to new root
		_tk_copy(&base, root, sizeof(page), 0);
	}

	tk_mfree(t, base.stack);

	tk_drop_task(t);

	return 0;
}

void cle_panic(task* t) {

}

int main() {
	task* t = tk_create_task(0, 0);
	struct _cmt_base base;
	st_ptr root, p;

	base.t = t;
	base.stack = 0;
	base.max = base.idx = 0;

	st_empty(t, &root);

	p = root;
	st_insert(t, &p, "12345", 6);
	p = root;
	st_insert(t, &p, "12346", 6);
	p = root;
	st_insert(t, &p, "12366", 6);
	p = root;
	st_insert(t, &p, "12666", 6);
	p = root;
	st_insert(t, &p, "12364", 6);

	_tk_measure(&base, root.pg, root.key);

	return 0;
}
