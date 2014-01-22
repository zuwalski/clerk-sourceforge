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

static key EMPTY = { 0, 0, 0, 0 };

// BEGIN COPY
struct _cmt_cpy_ctx {
    char* dest_begin;
    char* dest;
    key* k;
    ptr* ptr_spot;
};

struct _cmt_cp {
    ushort* parent;
    ushort offset;
};

static void cp_dat(struct _cmt_cpy_ctx* ctx, cdat c, uint l) {
    ctx->k->length += l * 8;
    memcpy(ctx->dest, c, l);
    ctx->dest += l;
    
    if (ctx->ptr_spot == 0 && l >= sizeof(ptr))
        ctx->ptr_spot = (ptr*) c;
}

static void cp_pop(struct _cmt_cpy_ctx* ctx, struct _cmt_cp* lvl) {
    key* k;
    ctx->dest += (long long) ctx->dest & 1;
    ctx->k = k = (key*) ctx->dest;
    
    k->length = k->sub = 0;
    k->offset = lvl->offset;
    
    k->next = *lvl->parent;
    *lvl->parent = ctx->dest - ctx->dest_begin;
    
    ctx->dest += sizeof(key);
}

static struct _cmt_cp cp_push(struct _cmt_cpy_ctx* ctx, key* nxt) {
    struct _cmt_cp l = {
        &ctx->k->sub,
        ctx->k->length + (nxt->offset & 7)
    };
    
    if (ctx->ptr_spot == 0 && CEILBYTE(nxt->length) >= sizeof(ptr) - sizeof(key))
        ctx->ptr_spot = (ptr*) nxt;
    return l;
}

static key* cp_ptr(struct _cmt_cpy_ctx* ctx, page** pg, ptr* p) {
    key* k;
    
    if (p->koffset < 2) {
        ptr* np = (ptr*) ctx->k;
        ctx->dest += sizeof(ptr) - sizeof(key);
        
        np->ptr_id = PTR_ID;
        np->koffset = 1;
        np->pg = p->pg;
        
        k = &EMPTY;
    } else {
        *pg = p->pg;
        k = GOKEY(*pg, p->koffset);
    }
    return k;
}

static void _cmt_cp_worker(struct _cmt_cpy_ctx* ctx, page* pg, key* me, key* nxt, uint offset) {
    struct {
        page* pg;
        key* nxt;
        struct _cmt_cp lvl;
    } mx[16];
    uint idx = 0;
    
    while (1) {
        const uint klen = ((nxt != 0 ? nxt->offset : me->length) >> 3) - offset;
        if (klen != 0)
            cp_dat(ctx, KDATA(me) + offset, klen);
        
        if(nxt == 0) {
            if (idx-- == 0)
                return;
            
            nxt = mx[idx].nxt;
            pg = mx[idx].pg;

            cp_pop(ctx, &mx[idx].lvl);
        } else if (me->length != nxt->offset) {
            struct _cmt_cp lvl = cp_push(ctx, nxt);
            
            if ((idx & 0xF0) == 0) {
                mx[idx].nxt = nxt;
                mx[idx].lvl = lvl;
                mx[idx].pg = pg;
                ++idx;
                
                offset = nxt->offset >> 3;
                nxt = nxt->next? GOOFF(pg, nxt->next) : 0;
                continue;
            }
            
            _cmt_cp_worker(ctx, pg, me, nxt, offset);
            
            cp_pop(ctx, &lvl);
        }
        
        me = nxt;
        while (ISPTR(me)) {
            me = cp_ptr(ctx, &pg, (ptr*) me);
        }
        nxt = me->sub? GOOFF(pg, me->sub) : 0;
        offset = 0;
    }
}

static page* _cmt_copy_root(task* t, page* root) {
    struct _cmt_cpy_ctx work;
    struct _cmt_cp lvl;
    page* dst = t->ps->new_page(t->psrc_data);
    key* me = GOOFF(root, sizeof(page));
    key* nxt = me->sub ? GOOFF(root, me->sub) : 0;
    ushort dummy = 0;
    
    work.dest_begin = (char*) dst;
    work.dest = work.dest_begin + sizeof(page);
    lvl.parent = &dummy;
    lvl.offset = 0;
    
    cp_pop(&work, &lvl);
    
    _cmt_cp_worker(&work, root, me, nxt , 0);
    
    dst->used = work.dest - work.dest_begin;
    return dst;
}
// END COPY

void test_copy(task* t, page* dst, st_ptr src) {
    struct _cmt_cpy_ctx work;
    struct _cmt_cp lvl;
    
    key* me = GOOFF(src.pg, src.key);
    key* nxt = me->sub ? GOOFF(src.pg, me->sub) : 0;
    ushort dummy = 0;
    
    work.dest_begin = (char*) dst;
    work.dest = work.dest_begin + sizeof(page);
    lvl.parent = &dummy;
    lvl.offset = 0;
    
    cp_pop(&work, &lvl);
    
    _cmt_cp_worker(&work, src.pg, me, nxt , 0);
    
    dst->used = work.dest - work.dest_begin;
}

// BEGIN MEASURE
struct _cmt_cut {
    page* pg;
    key* me;
    key* nxt;
    ulong offset;
    ulong cut_size;
};

struct _cmt_meas_ctx {
    task* t;
    struct _cmt_cut cut;
    
    ulong size;
    ulong target;
};

static int _key_ptr(page* pg, key* k){
    int offset = 0;
    if (k) {
        offset = (int) ((char*) k - (char*) pg);
        
        if (offset < 0 || offset > pg->size) {
            offset = (int)((char*) k - (char*) TO_TASK_PAGE(pg)->ovf);
            offset = 0x8000 | (offset >> 4);
        }
    }
    return offset;
}

static void _cmt_do_copy_page(task* t, struct _cmt_cut* cut) {
    page* np = t->ps->new_page(t->psrc_data);
    ptr* pt;
    ushort* link;
    struct _cmt_cpy_ctx cpy;
    struct _cmt_cp lvl;
    ushort dummy = 0;
    
    cpy.dest_begin = (char*) np;
    cpy.dest = cpy.dest_begin + sizeof(page);
    cpy.ptr_spot = 0;

    link = (cut->nxt) ? &cut->nxt->next : &cut->me->sub;

    // create root-key
    lvl.parent = &dummy;
    lvl.offset = 0;
    
    cp_pop(&cpy, &lvl);
    // copy
    _cmt_cp_worker(&cpy, cut->pg, cut->me, (*link) ? GOOFF(cut->pg, *link) : 0, (uint)cut->offset);
    
    np->used = cpy.dest - cpy.dest_begin;
    
    // cut-and-link ...
	cut->me->length = cut->offset;
    
    if (cpy.ptr_spot == 0) {
        const ushort op = _tk_alloc_ptr(t, TO_TASK_PAGE(cut->pg));
        cpy.ptr_spot = (ptr*) GOOFF(cut->pg, op);
        *link = op;
    } else
        *link = _key_ptr(cut->pg, (key*) cpy.ptr_spot);
    
    pt = cpy.ptr_spot;
    pt->ptr_id = PTR_ID;
    pt->koffset = 1;	// magic marker: this links to a rebuilded page
    pt->offset = cut->offset;
    pt->pg = np->id;    // the pointer (to be updated when copied)
    pt->next = 0;
}

static void ms_pop(struct _cmt_meas_ctx* ctx, page* pg, key* me, key* nxt, struct _cmt_cut* cut, ulong size) {
    uint offset = nxt != 0? nxt->offset + 1 : 0;
    ulong subsize = ctx->size - size;
    
    printf("me %d nxt %d size %lu - %d\n", _key_ptr(pg, me), _key_(pg,nxt), ctx->size, offset);
    
    if (subsize > ctx->target) {
        const ulong rest = CEILBYTE(me->length - offset);
        const ulong overflow = subsize - ctx->target;
        
        // more data left than overflow?
        if (rest > overflow) {
            offset += overflow * 8; // reduce size
            subsize -= overflow * 8;
        } else {
            _cmt_do_copy_page(ctx->t, cut);
            
            ctx->size -= cut->cut_size;
            subsize -= cut->cut_size;
        }
    }
    
    cut = &ctx->cut;
    cut->cut_size = subsize;
    cut->offset = offset;
    cut->nxt = nxt;
    cut->me = me;
    cut->pg = pg;
}

static key* ms_ptr(struct _cmt_meas_ctx* ctx, page** pg, ptr* p) {
    key* k;
    
    if (p->koffset < 2) {
        ctx->size += sizeof(ptr) + (ctx->size & 1);
        k = &EMPTY;
    } else {
        *pg = p->pg;
        k = GOKEY(*pg, p->koffset);
    }
    return k;
}

static void _cmt_ms_worker(struct _cmt_meas_ctx* ctx, page* pg, key* me, key* nxt, uint offset) {
    struct {
        page* pg;
        key* nxt;
        key* me;
        struct _cmt_cut cut;
        ulong size;
    } mx[32];
    uint idx = 0;
    
    while (1) {
        if ((idx & 0xE0) == 0) {
            ushort key_nxt;
            mx[idx].size = ctx->size;
            mx[idx].cut = ctx->cut;
            mx[idx].nxt = nxt;
            mx[idx].me = me;
            mx[idx].pg = pg;
            ++idx;
            
            if (nxt) {
                ctx->size += (nxt->offset >> 3) - offset;
                offset = nxt->offset >> 3;
                
                key_nxt = nxt->next;
            } else {
                key_nxt = me->sub;
                offset = 0;
            }
            
            if (key_nxt) {
                nxt = GOOFF(pg, key_nxt);
                continue;
            }
            
            ctx->size += (me->length >> 3) - offset;
        } else
            _cmt_ms_worker(ctx, pg, me, nxt, offset);
        
        do {
            struct _cmt_cut* cut;
            if (idx-- == 0)
                return;
            
            me = mx[idx].nxt;
            pg = mx[idx].pg;
            
            if (me && me->offset != mx[idx].me->length)
                ctx->size += sizeof(key) + (ctx->size & 1);
            
            // best cut of the 2 sub-branches
            cut = (ctx->cut.cut_size < mx[idx].cut.cut_size)? &mx[idx].cut : &ctx->cut;

            ms_pop(ctx, pg, mx[idx].me, me, cut, mx[idx].size);
        } while (me == 0);
        
        while (ISPTR(me)) {
            me = ms_ptr(ctx, &pg, (ptr*) me);
        }
        nxt = 0;
    }
}
// END MEASURE
void test_measure(task* t, st_ptr src) {
    struct _cmt_meas_ctx work;
    
    key* me = GOOFF(src.pg, src.key);
    
    work.target = 2000;
    work.size = sizeof(page) + sizeof(key);
    work.cut.cut_size = 0;
    
    _cmt_ms_worker(&work, src.pg, me, 0 , 0);
}

static ptr* _cmt_find_ptr(page* cpg, page* find, ushort koff) {
    ushort stack[32];
    uint idx = 0;
    
    while (1) {
        key* k = GOKEY(cpg, koff);
        
        if (ISPTR(k)) {
            ptr* pt = (ptr*) k;
            if (pt->koffset == 0 && pt->pg == find->id)
                return pt;
        } else if (k->sub) {
            if ((idx & 0xE0) == 0) {
                stack[idx++] = k->sub;
            } else {
                ptr* pt = _cmt_find_ptr(cpg, find, k->sub);
                if(pt)
                    return pt;
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
                ptr* pt = _cmt_find_ptr(parent, find, sizeof(page));
                if (pt) {
                    // not yet writable?
                    if (parent == parent->id) {
                        parent = _tk_write_copy(t, parent);
                    }
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
    int stat = 0;
	page* root = _cmt_mark_and_link(t);

	// rebuild from root => new root
	if (root) {
        page* new_root;
        struct _cmt_meas_ctx work;
        
        work.t = t;
        work.target = root->size - sizeof(page);
        work.cut.cut_size = 0;
        work.size = 0;
        
		// start measure from root
        _cmt_ms_worker(&work, root, GOOFF(root, sizeof(page)),0 ,0);
        
		// copy "rest" to new root
        new_root = _cmt_copy_root(t, root);

        // link old pages to new root
        _cmt_update_all_linked_pages(new_root);

        stat = t->ps->pager_commit(t->psrc_data, new_root);
	}

	tk_drop_task(t);

	return stat;
}

