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

#include "cle_struct.h"
#include <string.h>
#include <assert.h>

#define STACK_GROW 256

struct _cmt_stack {
    task* t;
    ushort* stack;
    uint idx;
    uint idx_max;
    
    // stats
    uint pages;
};

struct _cmt_base_msr {
    struct _cmt_stack* s;
	page* cpg;
	ushort target;
	ushort size;
	ushort r;
	ushort n;

	ushort* cut_link;
	st_ptr cut;
	ushort cut_size;
};

struct _cmt_base_copy {
    struct _cmt_stack* s;
	char* dest;
	char* dest_begin;
    char* dest_max;
	ushort* parent_key;
    page* pg;
	ushort ptr_spot;
};

static const key EMPTY = { 0, 0, 0, 0 };

#define ALIGN2(a) align2(a)
#define valid_kptr(p) assert(((p) & 1) == 0 || (p) & 0x8000)

static uint align2(uint a){
    return a + (a & 1);
}

static void _cmt_push_key(struct _cmt_stack* const b, ushort k) {
	if (b->idx == b->idx_max) {
		b->idx_max += STACK_GROW;
		b->stack = tk_realloc(b->t, b->stack, sizeof(ushort) * b->idx_max);
	}

	b->stack[b->idx++] = k;
}

static ushort _cmt_pop_key(struct _cmt_stack* const s){
    return s->stack[--s->idx];
}

static key* _cmt_alloc(struct _cmt_base_copy* const b, uint size) {
	key* r;
	b->dest += (long long) b->dest & 1;
	r = (key*) b->dest;

	//memset(r, 0, sizeof(key));

	//r->next = *b->parent_key;
	//*b->parent_key = b->dest - b->dest_begin;

	b->dest += size;
	return r;
}

static key* _cmt_root_key(struct _cmt_base_copy* const b, page* cpg, ushort r, ushort n, short adjoff) {
	key* nk, *rt = GOOFF(cpg, r);
	ushort tmp = 0;
    
    assert(ISPTR(rt) == 0);

    // does root-key contain space enough for the ptr-spot?
	if (adjoff == 0){
		n = rt->sub;
        
        if(sizeof(key) + CEILBYTE(rt->length) >= sizeof(ptr))
            b->ptr_spot = r;
    }
    else if(CEILBYTE(rt->length - adjoff) > sizeof(ptr))
        b->ptr_spot = align2(r + sizeof(key) + CEILBYTE(adjoff));

	// push root-linked keys
	while (n != 0) {
		key* kp = GOOFF(cpg,n);
		_cmt_push_key(b->s, r);
		_cmt_push_key(b->s, n);
		n = kp->next;
	}

	// allocate and copy root key
	b->parent_key = &tmp;
	nk = _cmt_alloc(b, sizeof(key));
	b->parent_key = &nk->sub;
    
    // round off
    adjoff &= 0xFFF8;

	//nk->length = rt->length - adjoff;

    // copy all of root key - or from adjoff
	//memcpy(b->dest, KDATA(rt) + (adjoff >> 3), CEILBYTE(rt->length - adjoff));
	b->dest += CEILBYTE(rt->length - adjoff);

    assert(b->dest < b->dest_max);
	return nk;
}

static void _cmt_copy_page(struct _cmt_base_copy* const b, page* cpg, ushort r, ushort n, short adjoff) {
    struct _cmt_stack* s = b->s;
	const uint stop = s->idx;
	key* nk = _cmt_root_key(b, cpg, r, n, adjoff);

    // round off and prepare for offset-adjustment
	adjoff = -(adjoff & 0xFFF8); // * -1;

	// tree copy rest of page-content
	while (s->idx != stop) {
		key* rt, *kp;

		n = _cmt_pop_key(s);
        
        // change-page marker
        if(n == 0xFFFF){
            cpg = cpg->parent;
            continue;
        }

        // pop root-key marker
		if (n == 0) {
			adjoff = _cmt_pop_key(s);
			r = _cmt_pop_key(s);
            
			b->parent_key = (ushort*) (b->dest_begin + r);
            continue;
		}

        // consider next key kp linked from root rt
		r = _cmt_pop_key(s);

		rt = GOOFF(cpg,r);
		kp = GOOFF(cpg,n);
        
        // find a place to put a pointer (note: ptr->length == 0xFFFF)
        if(b->ptr_spot == 0 && (sizeof(key) + CEILBYTE(kp->length) >= sizeof(ptr))){
            b->ptr_spot = n;
        }

    isptr:
        // handle pointers
		if (ISPTR(kp)) {
			ptr* p = (ptr*) kp;

            // static-ptr => just copy
			if (p->koffset < 2) {
				ptr* nptr = (ptr*) _cmt_alloc(b, sizeof(ptr));

/*				nptr->ptr_id = PTR_ID;
				nptr->offset = p->offset + adjoff;
                nptr->koffset = p->koffset;
				nptr->pg = p->pg;
  */
				continue;
			} else {
                // memptr => push old page and continue on next
				_cmt_push_key(s, 0xFFFF);

				((page*) p->pg)->parent = cpg;
				cpg = p->pg;

				kp = GOOFF(cpg, p->koffset);
                
                // transfer offsetƒ
                kp->offset = p->offset;
                n = p->koffset;
                
                if(ISPTR(kp)) goto isptr;
			}
		}

		// if not continue-key
		if (rt->length != kp->offset) {
			if (kp->length != 0) {
				// create new key
				nk = _cmt_alloc(b, sizeof(key));

				//nk->offset = kp->offset + adjoff;
			}
		} else if (kp->offset & 7){
            b->dest--;
		}

        // copy key content
		//memcpy(b->dest, KDATA(kp), CEILBYTE(kp->length));
		b->dest += CEILBYTE(kp->length);

        //assert(b->dest < b->dest_max);

		//nk->length += kp->length;

        // if this key has subs => continues on those (depth-first)
		if ((r = kp->sub)) {
			_cmt_push_key(s, (char*) b->parent_key - b->dest_begin);
			_cmt_push_key(s, adjoff);
			_cmt_push_key(s, 0);    // marker

			b->parent_key = &nk->sub;
            
            if(kp->offset == rt->length)
                adjoff += kp->offset;

			do {
                assert(r < cpg->size || r & 0x8000);
                
				_cmt_push_key(s, n);
				_cmt_push_key(s, r);
				kp = GOOFF(cpg,r);
			} while ((r = kp->next));
		}
	}
    
    b->pg->used = b->dest - b->dest_begin;
}

static void _cmt_setup_copy(struct _cmt_base_copy* cpy, struct _cmt_stack* s){
    // prepare new page
    cpy->s = s;
    cpy->pg = s->t->ps->new_page(s->t->psrc_data);
    
    cpy->dest_begin = (char*) cpy->pg;
    cpy->dest = cpy->dest_begin + sizeof(page);
    cpy->dest_max = cpy->dest_begin + cpy->pg->size;
	cpy->ptr_spot = 0;
    cpy->parent_key = 0;
    
    s->pages++;
}

static uint _cmt_cut_and_copy_page(struct _cmt_stack* const s, page* cpg, ushort r, ushort* n, const short adjoff) {
    struct _cmt_base_copy cpy;
	key* rk = GOOFF(cpg,r);
	ptr* pt;
    
    assert(r < cpg->size || r & 0x8000);
    assert(ISPTR(rk) == 0);
    assert(adjoff <= rk->length);
    
    _cmt_setup_copy(&cpy, s);
    // copy
	_cmt_copy_page(&cpy, cpg, r, n == 0? 0 : *n, adjoff);
    
    // cut-and-link ...
	rk->length = adjoff;
    
    if (cpy.ptr_spot == 0)
        cpy.ptr_spot = _tk_alloc_ptr(s->t, TO_TASK_PAGE(cpg) );
    
    pt = (ptr*) GOOFF(cpg, cpy.ptr_spot);
    pt->ptr_id = PTR_ID;
    pt->koffset = 1;	// magic marker: this links to a rebuilded paged
    pt->offset = adjoff;
    pt->next = 0;
    pt->pg = cpy.pg;    // the pointer (to be updated when copied)
    
    // link to new ptr
    if (n != 0)
        *n = cpy.ptr_spot;
    else if (cpy.ptr_spot != r){
        rk->sub = cpy.ptr_spot;
        rk->next = rk->length = rk->offset = 0;
    }
    
    return cpy.pg->used;
}

static void _cmt_set_cut(struct _cmt_base_msr* const b, ushort k, ushort* link, ushort offset, uint size) {
    assert(ISPTR(GOOFF(b->cpg, k)) == 0);
    assert(size < b->target);
    
    if(b->cut_size <= size)
    {
        b->cut.key = k;
        b->cut.pg = b->cpg;
        b->cut_link = link;
        b->cut_size = size;
        b->cut.offset = offset;

//        printf("cut %d at %d (%d)\n", key, offset, b->cut_size);
    }
}

static void _cmt_cut_between(struct _cmt_base_msr* const b, key* cut, ushort* link, const ushort low) {
	const ushort high_mark = (*link) ? GOOFF(b->cpg, *link)->offset : cut->length;
	const ushort k = (char*) cut - (char*) b->cpg;
    
    assert(b->size >= b->cut_size);
    assert(ISPTR(cut) == 0);
    
    // overflow: execute last cut
	if (b->size + sizeof(key) + CEILBYTE(cut->length - high_mark) > b->target) {
		uint sz = _cmt_cut_and_copy_page(b->s, b->cut.pg, b->cut.key, b->cut_link, b->cut.offset);
        
		b->size -= b->cut_size;
        b->cut_size = 0;
	}
    
    // still overflow: cut now
    if (b->size + sizeof(key) + CEILBYTE(cut->length - low) > b->target) {
        const int bits_left = ((int)b->target - (int)b->size - sizeof(key)) * 8;
        const int optimal_cut = (int) cut->length - bits_left;
        
		uint sz = _cmt_cut_and_copy_page(b->s, b->cpg, k, link, optimal_cut >= low ? optimal_cut : low);
        
        b->size = 0;
        b->cut_size = 0;
    }
    else
        _cmt_set_cut(b, k, link, low, b->size + sizeof(key) + CEILBYTE(cut->length - low));
    
    if(b->cut_size > 0){
        //printf("break");
    }
}

static void _cmt_cut_over(struct _cmt_base_msr* const b, ushort cut_key, key* over) {
	key* cut = GOOFF(b->cpg, cut_key);
	const uint cut_size = cut->length - over->offset;
    
    assert(ISPTR(cut) == 0);
    
    if(cut_size != 0){
        const uint tsize = align2(b->size) + sizeof(key) + CEILBYTE(cut_size);
        
        if (tsize < b->target) {
            
            _cmt_set_cut(b, cut_key, &over->next, over->offset + 1, tsize);
        } else
            _cmt_cut_between(b, cut, &over->next, over->offset + 1);
    }
}

static uint _cmt_cut_low(struct _cmt_base_msr* const b, ushort cut_key, uint size) {
	key* cut = GOOFF(b->cpg, cut_key);
    const uint size_all = size + sizeof(key) + CEILBYTE(cut->length);
    
    assert(ISPTR(cut) == 0);
    
    if (size_all < b->target) {
        _cmt_set_cut(b, cut_key, &cut->sub, 0, size_all);
        
        return size_all;
    } else {
        _cmt_cut_between(b, cut, &cut->sub, 0);
        
        return b->size + sizeof(key) + CEILBYTE(cut->length);
    }
}

static void _cmt_pop_ptr(struct _cmt_base_msr* const b) {
	if (b->s->stack[b->s->idx - 1] == 0x7FFF) {
        _cmt_set_cut(b, b->r, 0, 0, b->size);
        
		b->cpg = b->cpg->parent;
		b->s->idx--;
        
        b->size = align2(b->size) + _cmt_pop_key(b->s);
        if(b->size > b->target){
            //printf("");
        }
    }
}

// b-r never ptr -
static int _cmt_pop(struct _cmt_base_msr* const b) {
    while (b->s->idx) {
        _cmt_pop_ptr(b);

        b->n = _cmt_pop_key(b->s);
        b->r = _cmt_pop_key(b->s);
        
        valid_kptr(b->n);
        valid_kptr(b->r);
        
        // did we pop from branch?
        if (b->n == 0) {
            b->size = _cmt_cut_low(b, b->r, align2(b->size));
            
            b->size += _cmt_pop_key(b->s);
        } else {
            key* kn = GOOFF(b->cpg, b->n);
            // measure cut on root over b->n->offset
            _cmt_cut_over(b, b->r, kn);
            
            if(ISPTR(kn)) {
                const ptr* pt = (ptr*) kn;
                
                if (pt->koffset < 2) {
                    b->size += sizeof(ptr);
                    continue;
                } else {
                    page* pg = (page*) pt->pg;
                    // mark page-change
                    _cmt_push_key(b->s, b->size);
                    _cmt_push_key(b->s, 0x7FFF);

                    pg->parent = b->cpg;
                    b->cpg = pg;
                    b->size = 0;
                    
                    b->n = b->r = pt->koffset;
                }
            }
            break;
        }
    }
    
	return b->s->idx;
}

static void _cmt_measure(struct _cmt_stack* const s, page* cpg, uint n, ushort target) {
    struct _cmt_base_msr b;
    b.target = target;
	b.n = b.r = n;
	b.cpg = cpg;
    b.cut_size = 0;
	b.size = 0;
	s->idx = 0;
    b.s = s;

	do {
        // PRE: kp is never a ptr
		const key* kp = GOOFF(b.cpg,b.n);
        
        assert(ISPTR(kp) == 0);

		if ((n = kp->sub)) {
			_cmt_push_key(s, b.size);
			_cmt_push_key(s, b.n);
			_cmt_push_key(s, 0);    // marker
            b.size = 0;

			do {
				_cmt_push_key(s, b.n);
				_cmt_push_key(s, n);
				kp = GOOFF(b.cpg,n);
			} while ((n = kp->next));
		} else {
            // b.n is a stand-alone key
            b.size += _cmt_cut_low(&b, b.n, 0);
        }
	} while (_cmt_pop(&b));
}

static page* _cmt_mark_and_link(struct _cmt_stack* b, page* cpg, page* find) {
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

                k = _cmt_pop_key(b);
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

static void _cmt_update_all_linked_pages(const page* pg) {
	uint i = sizeof(page);
	do {
		const key* k = GOKEY(pg, i);
		if (ISPTR(k)) {
            ptr* pt = (ptr*) k;
			((page*) (pt->pg))->parent = pg->id;
			i += sizeof(ptr);
            if ( pt->koffset != 0) {
                pt->koffset = 0;
                // recurse onto rebuild page
                _cmt_update_all_linked_pages(pt->pg);
            }
		} else {
			i += sizeof(key) + CEILBYTE(k->length);
			i += i & 1;
		}
	} while (i < pg->used);
}

/**
 * Rebuild all changes into new root => create new db-version and switch to it.
 *
 * return 0 if ok - also if no changes was written.
 */
int cmt_commit_task(task* t) {
	struct _cmt_stack stack;
	task_page* tp;
	page* root = 0;
    int stat = 0;

	stack.t = t;
	stack.stack = 0;
	stack.idx_max = stack.idx = stack.pages = 0;

	// link up all w-pages back to root (and make them writable)
	for (tp = t->wpages; tp != 0; tp = tp->next) {
		page *parent, *find = &tp->pg;

		for (parent = tp->pg.parent; parent != 0; find = parent, parent = parent->parent) {
			find = _cmt_mark_and_link(&stack, parent, find);
			if (find == 0)
				break;
		}

		// touched root?
		if (find != 0)
			root = find;
	}

	// rebuild from root => new root
	if (root) {
        struct _cmt_base_copy cpy;
		// start measure from root
		_cmt_measure(&stack, root, sizeof(page), root->size - sizeof(page));

		// copy "rest" to new root
        _cmt_setup_copy(&cpy, &stack);
		_cmt_copy_page(&cpy, root, sizeof(page), 0, 0);
        
        // link old pages to new root
        //_cmt_update_all_linked_pages(cpy.pg);

        stat = t->ps->pager_commit(t->psrc_data, cpy.pg);
	}

	tk_mfree(t, stack.stack);

	tk_drop_task(t);

	return stat;
}

void test_copy(task* t, page* dst, st_ptr src) {
    struct _cmt_base_copy cpy;
    struct _cmt_stack stack;
    
    stack.t = t;
	stack.stack = 0;
	stack.idx_max = stack.idx = 0;
    
    cpy.s = &stack;
    cpy.pg = dst;
    cpy.ptr_spot = 0;
	cpy.dest_begin = (char*) dst;
	cpy.dest = cpy.dest_begin + sizeof(page);
    cpy.dest_max = cpy.dest_begin + dst->size;

	_cmt_copy_page(&cpy, src.pg, src.key, 0, 0);
}
