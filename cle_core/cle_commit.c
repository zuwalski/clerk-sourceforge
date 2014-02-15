/*
 Clerk application and storage engine.
 Copyright (C) 2014  Lars Szuwalski

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

struct _tk_setup {
    char* trans;
    
	page* dest;
	task* t;
    
    long trans_used;
    long trans_size;
    
	uint halfsize;
	uint fullsize;
    
	ushort o_pt;
	ushort l_pt;
};

static void _cmt_trans_next_page(struct _tk_setup* setup) {
    if (setup->dest != 0) {
        setup->trans_used += setup->dest->used;
        
        //setup->dest->size = setup->dest->used;

        setup->trans_used += 8 - (setup->trans_used & 7);
    }

    if (setup->trans_size - setup->trans_used < setup->fullsize) {
        setup->trans_size += setup->fullsize;
        
        setup->trans = tk_realloc(setup->t, setup->trans, (uint) setup->trans_size);
    }
    
    setup->dest = (page*) (setup->trans + setup->trans_used);
    setup->dest->used = sizeof(page);
    setup->dest->size = setup->fullsize;
    setup->dest->parent = 0;
    setup->dest->waste = 0;

    setup->dest->id = (cle_pageid) setup->trans_used;
}

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
			if (pt->koffset > 1) {
				pw = (page*) pt->pg;
				k = GOKEY(pw,pt->koffset);
			} else {
				ptr* newptr;
				setup->dest->used += setup->dest->used & 1;
				newptr = (ptr*) ((char*) setup->dest + setup->dest->used);
				newptr->ptr_id = PTR_ID;
				newptr->koffset = pt->koffset;
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
            if (parent->length & 7) {
                setup->dest->used--;
            }
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

static key* _tk_create_root_key(struct _tk_setup* setup, key* copy, int offset) {
	// copy first/root key
	key* root = (key*) ((char*) setup->dest + sizeof(page));

    // TODO point to root
    setup->o_pt = setup->l_pt = 0;
    
	root->offset = root->next = root->sub = 0;
	root->length = copy->length - (offset & 0xFFF8);
    
	memcpy(KDATA(root), KDATA(copy) + (offset >> 3), CEILBYTE(root->length));
    
	setup->dest->used = sizeof(page) + sizeof(key) + CEILBYTE(root->length);
    
	return root;
}

static ushort _tk_link_and_create_page(struct _tk_setup* setup, page* pw, int ptr_offset) {
	ptr* pt;
    
	// no large enough exsisting key?
	if (setup->l_pt + sizeof(key) * 8 < sizeof(ptr) * 8) {
		setup->o_pt = _tk_alloc_ptr(setup->t, TO_TASK_PAGE(pw) ); // might change ptr-address!
	}
    
	// create a link to new page
    pt = (ptr*) GOOFF(pw,setup->o_pt);
	pt->offset = ptr_offset;
	pt->ptr_id = PTR_ID;
	pt->koffset = 1; // magic marker
    pt->next = 0;
    pt->pg = setup->dest->id;
    
	return setup->o_pt;
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

static uint _tk_cut_key(struct _tk_setup* setup, page* pw, key* copy, key* prev, int cut_bid) {
    _cmt_trans_next_page(setup);
    //setup->dest = setup->t->ps->new_page(setup->t->psrc_data);
    
	int cut_adj = _tk_adjust_cut(pw, copy, prev, cut_bid);
	
    key* root = _tk_create_root_key(setup, copy, cut_adj);
    ushort* link = (prev != 0) ? &prev->next : &copy->sub;
    
	// cut-off 'copy'
	copy->length = cut_adj;
    
	// start compact-copy
	_tk_compact_copy(setup, pw, root, &root->sub, *link, -(cut_adj & 0xFFF8));
    
    assert(setup->dest->used <= setup->dest->size);
    
	// link ext-pointer to new page
    *link = _tk_link_and_create_page(setup, pw, cut_adj);
    
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
		if (pt->koffset > 1)
			subsize = _tk_measure(setup, (page*) pt->pg, 0, pt->koffset);
		else
			subsize = (sizeof(ptr) * 8);
        
		return size + subsize;
	} else // cut k below limit (length | sub->offset)
	{
		uint subsize = (k->sub == 0) ? 0 : _tk_measure(setup, pw, k, k->sub); // + size);
        
		while (1) {
			int cut_offset = subsize + k->length - setup->halfsize;
			if (cut_offset < 0)
				break;

			subsize = _tk_cut_key(setup, pw, k, 0, cut_offset); // + size;
		}
        
		size += subsize;
	}
    
	return size + k->length + ((sizeof(key) + 1) * 8);
}

static ushort _cmt_find_ptr(page* cpg, page* find, ushort koff) {
    ushort stack[32];
    uint idx = 0;
    
    while (1) {
        key* k = GOKEY(cpg, koff);
        
        if (ISPTR(k)) {
            ptr* pt = (ptr*) k;
            if (pt->koffset == 0 && pt->pg == find->id)
                return koff;
        } else if (k->sub) {
            if ((idx & 0xE0) == 0) {
                stack[idx++] = k->sub;
            } else {
                koff = _cmt_find_ptr(cpg, find, k->sub);
                if(koff)
                    return koff;
            }
        }
        
        if ((koff = k->next) == 0) {
            if (idx == 0)
                break;
            koff = stack[--idx];
        }
    }
    return 0;
}

static page* _cmt_mark_and_link(task* t) {
    task_page *end = 0;
    page* root = 0;
    
    while (t->wpages != end) {
        task_page* tp, *first = t->wpages;
        
        for (tp = first; tp != end; tp = tp->next) {
            page* parent = tp->pg.parent;
            page* find = &tp->pg;
            
            if (parent) {
                ushort pt_off = _cmt_find_ptr(parent, find, sizeof(page));
                if (pt_off) {
                    ptr* pt;
                    // not yet writable?
                    if (parent == parent->id)
                        parent = _tk_write_copy(t, parent);
                    
                    pt = (ptr*) GOOFF(parent, pt_off);
                    pt->koffset = sizeof(page);
                    pt->pg = find;
                    
                    // fix offset like mem-ptr
                    GOKEY(find,sizeof(page))->offset = pt->offset;
                }
            } else
                root = find;
        }
        
        end = first;
    }
    
    return root;
}

static int _CHECK_PTR(page* pg, uint k) {
    if (k) {
        key* me = GOOFF(pg, k);
        if(_CHECK_PTR(pg, me->next))
            return 1;
        
        if (ISPTR(me)) {
            ptr* p = (ptr*) me;
            if (p->koffset != 0) {
                return 1;
            }
        } else
            if(_CHECK_PTR(pg, me->sub))
                return 1;
    }
    return 0;
}

static void _cmt_update_all_linked_pages(struct _tk_setup* setup, page* pg) {
	uint i = sizeof(page);
    pg->id = pg;
    
	do {
		const key* k = GOKEY(pg, i);
        
		if (ISPTR(k)) {
            ptr* pt = (ptr*) k;
            if (pt->koffset == 1) {
                page* nxt_pg = (page*) (setup->trans + (long) pt->pg);
                
                pt->koffset = 0;
                pt->pg = nxt_pg;
                
                nxt_pg->parent = pg->id;
                
                // recurse onto rebuild page
                _cmt_update_all_linked_pages(setup, nxt_pg);
            } else {
                ((page*) (pt->pg))->parent = pg->id;
            }

			i += sizeof(ptr);
		} else {
			i += sizeof(key);
            i += CEILBYTE(k->length);
            i += i & 1;
		}
	} while (i < pg->used);
    
    if(_CHECK_PTR(pg, sizeof(page))) {
        i = sizeof(page);
        
        do {
            const key* k = GOKEY(pg, i);
            
            if (ISPTR(k)) {
                ptr* pt = (ptr*) k;

                i += sizeof(ptr);
            } else {
                i += sizeof(key);
                i += CEILBYTE(k->length);
                i += i & 1;
            }
        } while (i < pg->used);
    }
}

/**
 * Rebuild all changes into new root => create new db-version and switch to it.
 *
 * return 0 if ok - also if no changes was written.
 */
int cmt_commit_task(task* t) {
    int stat = 0;
	page* root = _cmt_mark_and_link(t);
    
	// rebuild from root => new root
	if (root) {
        struct _tk_setup setup;
        ushort sub = 0;
        
        setup.t = t;
        setup.fullsize = (uint) (root->size);
        setup.halfsize = (uint) (setup.fullsize - sizeof(page)) << 2;   // in bits
        //setup.fullsize -= setup.halfsize >> 2;
        
        setup.trans_size = setup.trans_used = 0;
        setup.trans = 0;
        setup.dest = 0;
        
        _tk_measure(&setup, root, 0, sizeof(page));
        
        // reset and copy remaining rootpage
        _cmt_trans_next_page(&setup);
        //setup.dest = t->ps->new_page(t->psrc_data);
        
        _tk_compact_copy(&setup, root, 0, &sub, sizeof(page), 0);
        
        {
            key* k = GOKEY(setup.dest, sizeof(page));
            if(ISPTR(k)){
                ptr* p = (ptr*) k;
                setup.dest = (page*) (setup.trans + (long) p->pg);
                setup.trans_used = (char*) setup.dest - setup.trans;
                setup.trans_used += setup.dest->used;
            }
        }
        // link old pages to new root
        _cmt_update_all_linked_pages(&setup, setup.dest);
        
        // swap root
        stat = t->ps->pager_commit(t->psrc_data, setup.dest);
	}
    
	tk_drop_task(t);
    
	return stat;
}
