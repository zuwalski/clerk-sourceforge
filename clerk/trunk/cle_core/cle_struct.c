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
#include <string.h>

#include "cle_clerk.h"
#include "cle_struct.h"

struct _st_lkup_res
{
	task* t;
	page_wrap* pg;
	page_wrap* d_pg;
	key*  prev;
	key*  sub;
	key*  d_prev;
	key*  d_sub;
	cdat  path;
	uint  length;
	uint  diff;
};

static uint _st_lookup(struct _st_lkup_res* rt)
{
	key* me   = rt->sub;
	cdat ckey = KDATA(me) + (rt->diff >> 3);
	rt->d_sub = 0;

	while(1)
	{
		uint max = me->length - rt->diff;
		if(rt->length < max) max = rt->length;

		while(max > 0)
		{
			uint a = *(rt->path) ^ *ckey;	// compare bytes

			if(a)
			{
				// fold 1's after msb
				a |= (a >> 1);
				a |= (a >> 2);
				a |= (a >> 4);
				// lzc(a)
				a -= ((a >> 1) & 0x55);
				a = (((a >> 2) & 0x33) + (a & 0x33));
				a = (((a >> 4) + a) & 0x0f);

				a = 8 - a;
				if(a < max) max = a;
				break;
			}
			else if(max < 8)
				break;

			rt->path++;ckey++;
			rt->length -= 8;
			rt->diff += 8;
			max -= 8;
		}

		rt->diff += max;
		rt->sub  = me;
		rt->prev = 0;

		if(rt->length == 0 || me->sub == 0)
			break;

		me = GOOFF(rt->pg,me->sub);
		while(me->offset < rt->diff)
		{
			rt->prev = me;

			if(me->next == 0)
				break;
		
			me = GOOFF(rt->pg,me->next);
		}

		if(me->offset != rt->diff)
			break;

		// for st_delete
		if(rt->sub->length != me->offset || (rt->d_sub == 0 && rt->sub->length != 0))
		{
			rt->d_pg   = rt->pg;
			rt->d_sub  = rt->sub;
			rt->d_prev = rt->prev;
		}

		if(ISPTR(me))
			me = _tk_get_ptr(rt->t,&rt->pg,me);
		ckey = KDATA(me);
		rt->diff = 0;		
	}
	return (rt->length == 0);
}

static uint _prev_offset(page_wrap* pg, key* prev)
{
	return (pg->ovf != 0 && (char*)prev > (char*)pg->ovf && (char*)prev < (char*)pg->ovf + pg->ovf->size)?
		(char*)prev - (char*)pg->ovf : 0;
}

static ptr* _st_page_overflow(struct _st_lkup_res* rt, uint size)
{
	ptr* pt;

	// rt->prev might move in _tk_alloc_ptr
	uint prev_offset = _prev_offset(rt->pg,rt->prev); 

	// allocate pointer
	ushort ptr_off = _tk_alloc_ptr(rt->t,rt->pg);

	/* rebuild prev-pointer in (possibly) new ovf */
	if(prev_offset)
		rt->prev = (key*)((char*)rt->pg->ovf + prev_offset);

	// +1 make sure new data can be aligned as well
	if(size + rt->t->stack->pg->used + (rt->t->stack->pg->used & 1) > rt->t->stack->pg->size)
		_tk_stack_new(rt->t);

	// init mem-pointer
	pt = (ptr*)GOPTR(rt->pg,ptr_off);

	pt->pg = rt->t->stack;
	pt->koffset = rt->t->stack->pg->used + (rt->t->stack->pg->used & 1);
	pt->offset = rt->diff;
	pt->ptr_id = PTR_ID;

	if(rt->prev)
	{
		pt->next = rt->prev->next;
		rt->prev->next = ptr_off;
	}
	else /* sub MUST be there */
	{
		pt->next = rt->sub->sub;
		rt->sub->sub = ptr_off;
	}

	/* reset values */
	rt->pg = rt->t->stack;
	rt->prev = rt->sub = 0;

	return pt;
}

/* make a writable copy of external pages before write */
static void _st_make_writable(struct _st_lkup_res* rt)
{
	page* old = rt->pg->pg;
	
	if(old->id == 0 || rt->pg->orig != 0)
		return;
	
	_tk_write_copy(rt->t,rt->pg);

	/* fix pointers */
	if(rt->prev)
		rt->prev = GOKEY(rt->pg,(char*)rt->prev - (char*)old);

	if(rt->sub)
		rt->sub = GOKEY(rt->pg,(char*)rt->sub - (char*)old);
}

#define IS_LAST_KEY(k,pag) ((char*)(k) + ((k)->length >> 3) + sizeof(key) + 3 - (char*)(pag) > (pag)->used)

static void _st_write(struct _st_lkup_res* rt)
{
	key* newkey;
	uint size = rt->length >> 3;

	_st_make_writable(rt);

	/* continue/append (last)key? */
	if((rt->sub->length == 0 || rt->diff == rt->sub->length) && IS_LAST_KEY(rt->sub,rt->pg->pg))
	{
		uint length = (rt->pg->pg->used + size > rt->pg->pg->size)? rt->pg->pg->size - rt->pg->pg->used : size;

		memcpy(KDATA(rt->sub) + (rt->diff >> 3),rt->path,length);
		rt->sub->length = ((rt->diff >> 3) + length) << 3;
		rt->pg->pg->used = ((char*)KDATA(rt->sub) + ((uint)rt->sub->length >> 3)) - (char*)(rt->pg->pg);

		rt->diff = rt->sub->length;		
		size -= length;
		if(size == 0)	// no remaining data?
			return;

		rt->length = size << 3;
		rt->path += length;
	}

	do
	{
		// -1 make sure it can be aligned
		uint   room   = rt->pg->pg->size - rt->pg->pg->used - (rt->pg->pg->used & 1);
		uint   length = rt->length;
		uint   pgsize = size;
		ushort nkoff;

		pgsize += sizeof(key);

		if(pgsize > room)	/* page-overflow */
		{
			if(room > sizeof(key) + 2)	/* room for any data at all? */
				pgsize = room;		/* as much as we can */
			else
			{
				_st_page_overflow(rt,pgsize);	/* we need a new page */

				room = rt->pg->pg->size - rt->pg->pg->used;
				pgsize = room > pgsize? pgsize : room;
			}

			length      = (pgsize - sizeof(key)) << 3;
			rt->length -= length;
		}

		nkoff = rt->pg->pg->used;
		nkoff += nkoff & 1; /* short aligned */
		rt->pg->pg->used = nkoff + pgsize;
		newkey = GOKEY(rt->pg,nkoff);
		
		newkey->offset = rt->diff;
		newkey->length = length;
		newkey->sub = newkey->next = 0;
		
		if(rt->prev)
		{
			newkey->next   = rt->prev->next;
			rt->prev->next = nkoff;
		}
		else if(rt->sub)
		{
			newkey->next = rt->sub->sub;
			rt->sub->sub = nkoff;
		}

		memcpy(KDATA(newkey),rt->path,length >> 3);

		rt->prev = 0;
		rt->sub  = newkey;
		rt->diff = length;
		rt->path += length >> 3;
		size     -= pgsize - sizeof(key);
	}
	while(size);	
}

static void _pt_move(st_ptr* pt, struct _st_lkup_res* rt)
{
	pt->pg  = rt->pg;
	pt->key = (char*)rt->sub - (char*)rt->pg->pg;
	pt->offset = rt->diff;
}

/* Interface-functions */

uint st_empty(task* t, st_ptr* pt)
{
	key* nk = tk_alloc(t,sizeof(key) + 2,&pt->pg);	// dont use alloc (8-byte alignes) and waste 2 bytes here

	pt->key = (char*)nk - (char*)pt->pg->pg;
	pt->offset = 0;

	memset(nk,0,sizeof(key));
	return 0;
}

uint st_is_empty(st_ptr* pt)
{
	key* k;
	ushort offset;
	if(pt == 0 || pt->pg == 0) return 1;
	k = GOOFF(pt->pg,pt->key);
	offset = pt->offset;
	while(1)
	{
		if(offset < k->length) return 0;
		if(k->sub == 0) return 1;
		k = GOOFF(pt->pg,k->sub);
		offset = 0;
	}
}

uint st_exsist(task* t, st_ptr* pt, cdat path, uint length)
{
	struct _st_lkup_res rt;
	rt.t      = t;
	rt.path   = path;
	rt.length = length << 3;
	rt.pg     = pt->pg;
	rt.sub    = GOOFF(pt->pg,pt->key);
	rt.diff   = pt->offset;

	return _st_lookup(&rt);
}

uint st_move(task* t, st_ptr* pt, cdat path, uint length)
{
	struct _st_lkup_res rt;
	rt.t      = t;
	rt.path   = path;
	rt.length = length << 3;
	rt.pg     = pt->pg;
	rt.sub    = GOOFF(pt->pg,pt->key);
	rt.diff   = pt->offset;

	if(_st_lookup(&rt))
		_pt_move(pt,&rt);

	return (rt.length != 0);
}

uint st_insert(task* t, st_ptr* pt, cdat path, uint length)
{
	struct _st_lkup_res rt;
	rt.t      = t;
	rt.path   = path;
	rt.length = length << 3;
	rt.pg     = pt->pg;
	rt.sub    = GOOFF(pt->pg,pt->key);
	rt.diff   = pt->offset;

	if(!_st_lookup(&rt))
		_st_write(&rt);
	else rt.path = 0;

	_pt_move(pt,&rt);
	return (rt.path != 0);
}

struct _prepare_update
{
	ushort remove;
	ushort waste;
};

static struct _prepare_update _st_prepare_update(struct _st_lkup_res* rt, task* t, st_ptr* pt)
{
	struct _prepare_update pu;
	pu.remove = 0;

	rt->pg = pt->pg;
	rt->diff = pt->offset;
	rt->sub  = GOOFF(pt->pg,pt->key);
	rt->prev = 0;
	rt->t    = t;

	_st_make_writable(rt);

	if(rt->sub->sub)
	{
		key* nxt = GOOFF(rt->pg,rt->sub->sub);
		while(nxt->next && nxt->offset < pt->offset)
		{
			rt->prev = nxt;
			nxt = GOOFF(rt->pg,nxt->next);
		}

		if(rt->prev)
		{
			pu.remove = rt->prev->next;
			rt->prev->next = 0;
		}
		else
		{
			pu.remove = rt->sub->sub;
			rt->sub->sub = 0;
		}
	}

	pu.waste = rt->sub->length - rt->diff;
	rt->sub->length = rt->diff;

	return pu;
}

uint st_update(task* t, st_ptr* pt, cdat path, uint length)
{
	struct _st_lkup_res rt;
	struct _prepare_update pu = _st_prepare_update(&rt,t,pt);

	if(length > 0)
	{
		length <<= 3;
		/* old key has room for this update (and its a local page) */
		if(pu.waste >= length && pt->pg->pg->id == 0)
		{
			memcpy(KDATA(rt.sub)+(rt.diff >> 3),path,length >> 3);
			rt.diff = rt.sub->length = length + rt.diff;
			pu.waste -= length;
		}
		else
		{
			rt.path   = path;
			rt.length = length;
			_st_write(&rt);
		}
	}

	if(rt.pg->pg->id)	// should we care about cleanup?
	{
		rt.pg->pg->waste += pu.waste >> 3;
		_tk_remove_tree(t,rt.pg,pu.remove);
	}

	_pt_move(pt,&rt);
	return 0;
}

uint st_delete(task* t, st_ptr* pt, cdat path, uint length)
{
	struct _st_lkup_res rt;
	rt.t      = t;
	rt.path   = path;
	rt.length = length << 3;
	rt.pg     = pt->pg;
	rt.sub    = GOOFF(pt->pg,pt->key);
	rt.diff   = pt->offset;

	if(_st_lookup(&rt) == 0)
		return 1;

	if(rt.prev == 0 && rt.sub->sub)
	{
		key* k = GOOFF(rt.pg,rt.sub->sub);
		while(k->offset < rt.diff)
		{
			rt.prev = k;
			if(k->next == 0)
				break;
			k = GOOFF(rt.pg,k->next);
		}
	}

	if(rt.prev)
	{
		_st_make_writable(&rt);
		//waste  = rt.sub->length - rt.prev->offset;
		//remove = rt.prev->next;
		//rm_pg  = rt.pg;

		rt.sub->length = rt.prev->offset;
		rt.prev->next = 0;
	}
	else if(rt.d_sub)
	{
		page* orig = rt.d_pg->pg;
		key* k;
		
		_st_make_writable(&rt);
		
		// fix pointer
		rt.d_sub = GOKEY(rt.d_pg,(char*)rt.d_sub - (char*)orig);

		if(rt.d_prev)
		{
			// fix pointer
			rt.d_prev = GOKEY(rt.d_pg,(char*)rt.d_prev - (char*)orig);

			k = GOOFF(rt.d_pg,rt.d_prev->next);
			rt.d_prev->next = k->next;

			if(k->offset == rt.d_sub->length)
				rt.d_sub->length = rt.d_prev->offset;
		}
		else
		{
			k = GOOFF(rt.d_pg,rt.d_sub->sub);
			rt.d_sub->sub = k->next;

			if(k->offset == rt.d_sub->length)
				rt.d_sub->length = 0;
		}
	}
	else
		_st_prepare_update(&rt,t,pt);

	return 0;
}

uint st_dataupdate(task* t, st_ptr* pt, cdat path, uint length)
{
	struct _st_lkup_res rt;
	rt.pg   = pt->pg;
	rt.diff = pt->offset;
	rt.sub  = GOOFF(pt->pg,pt->key);
	rt.prev = 0;
	rt.t    = t;

	if(length > 0)
		_st_make_writable(&rt);

	while(length > 0)
	{
		uint wlen;

		if(ISPTR(rt.sub))
		{
			rt.sub = _tk_get_ptr(t,&rt.pg,rt.sub);
			_st_make_writable(&rt);
		}

		wlen = (rt.sub->length - rt.diff) >> 3;
		if(wlen != 0)
		{
			wlen = length > wlen? wlen : length;
			memcpy(KDATA(rt.sub)+(rt.diff >> 3),path,wlen);
			length -= wlen;
			rt.diff = 0;
		}

		if(rt.sub->sub == 0)
			break;

		wlen = rt.sub->length;
		rt.sub = GOOFF(rt.pg,rt.sub->sub);
		while(rt.sub->offset != wlen)
		{
			if(rt.sub->next == 0)
				return (length > 0);

			rt.sub = GOOFF(rt.pg,rt.sub->next);
		}
	}

	return (length > 0);
}

uint st_link(task* t, st_ptr* to, st_ptr* from)
{
	struct _st_lkup_res rt;
	struct _prepare_update pu = _st_prepare_update(&rt,t,to);
	ptr* pt = _st_page_overflow(&rt,0);

	if(from->offset != 0)
	{
		cle_panic(t);	// unimpl
	}
	else
	{
		pt->pg = from->pg;
		pt->koffset = from->key;
	}

	if(rt.pg->pg->id)	// should we care about cleanup?
	{
		rt.pg->pg->waste += pu.waste >> 3;
		_tk_remove_tree(t,rt.pg,pu.remove);
	}
	return 0;
}

uint st_append(task* t, st_ptr* pt, cdat path, uint length)
{
	struct _st_lkup_res rt;
	rt.t      = t;
	rt.path   = path;
	rt.length = length << 3;
	rt.pg     = pt->pg;
	rt.sub    = GOOFF(pt->pg,pt->key);
	rt.diff   = rt.sub->length;
	rt.prev   = 0;

	if(rt.sub->sub)
	{
		key* nxt = GOOFF(rt.pg,rt.sub->sub);
		while(nxt->offset < pt->offset)
		{
			rt.prev = nxt;

			if(nxt->next == 0)
				break;
			nxt = GOOFF(rt.pg,nxt->next);
		}

		if(nxt->offset == rt.sub->length)
		{
			while(1)
			{
				rt.sub  = (ISPTR(nxt))?_tk_get_ptr(t,&rt.pg,nxt):nxt;
				if(rt.sub->sub == 0)
				{
					rt.diff = rt.sub->length;
					rt.prev = 0;
					break;
				}

				nxt = GOOFF(rt.pg,rt.sub->sub);
				if(nxt->offset != rt.sub->length)
					return 1;
			}
		}
		else if(nxt->offset >= pt->offset)
			return 1;
	}

	_st_write(&rt);

	_pt_move(pt,&rt);
	return 0;
}

static key* _trace_nxt(st_ptr* pt)
{
	key* nxt;
	key* me = GOOFF(pt->pg,pt->key);
	// deal with offset
	if(me->sub == 0)
		return 0;

	nxt = GOOFF(pt->pg,me->sub);
	while(nxt->offset < pt->offset)
	{
		if(nxt->next == 0)
			return 0;

		nxt = GOOFF(pt->pg,nxt->next);
	}
	return nxt;
}

// return read lenght. Or -1 => eof data, -2 more data, buffer full
int st_get(task* t, st_ptr* pt, char* buffer, uint length)
{
	page_wrap* pg = pt->pg;
	key* me      = GOOFF(pt->pg,pt->key);
	key* nxt;
	cdat ckey    = KDATA(me) + (pt->offset >> 3);
	uint offset  = pt->offset;
	uint klen;
	int read     = 0;

	nxt = _trace_nxt(pt);

	klen = ((nxt)? nxt->offset : me->length) - offset;

	length <<= 3;

	while(1)
	{
		uint max = 0;

		if(klen > 0)
		{
			max = (length > klen?klen:length)>>3;
			memcpy(buffer,ckey,max);
			buffer += max;
			read   += max;
			length -= max << 3;
		}

		// move to next key for more data?
		if(length > 0)
		{
			// no next key! or trying to read past split?
			if(nxt == 0 || (nxt->offset < me->length && me->length != 0))
			{
				pt->offset = max + (offset & 0xFFF8);
				break;
			}

			offset = 0;
			me = (ISPTR(nxt))?_tk_get_ptr(t,&pg,nxt):nxt;
			ckey = KDATA(me);
			if(me->sub)
			{
				nxt = GOOFF(pg,me->sub);
				klen = nxt->offset;
			}
			else
			{
				klen = me->length;
				nxt = 0;
			}
		}
		// is there any more data?
		else
		{
			max <<= 3;

			if(max < klen)	// more on this key?
				pt->offset = max + (offset & 0xFFF8);
			else if(nxt && nxt->offset == me->length)	// continuing key?
			{
				pt->offset = 0;
				me = (ISPTR(nxt))?_tk_get_ptr(t,&pg,nxt):nxt;
			}
			else
			{
				pt->offset = me->length;
				read = -1;	// no more!
				break;
			}

			// yes .. tell caller and move st_ptr
			read = -2;
			break;
		}
	}

	pt->key = (char*)me - (char*)pg->pg;
	pt->pg  = pg;
	return read;
}

uint st_offset(task* t, st_ptr* pt, uint offset)
{
	page_wrap* pg = pt->pg;
	key* me       = GOOFF(pt->pg,pt->key);
	key* nxt;
	uint klen;

	nxt = _trace_nxt(pt);

	klen = (nxt)? nxt->offset : me->length;

	offset = (offset << 3) + pt->offset;

	while(1)
	{
		uint max = offset > klen?klen:offset;
		offset -= max;

		// move to next key for more data?
		if(offset > 0 && nxt && nxt->offset == me->length)
		{
			me = (ISPTR(nxt))?_tk_get_ptr(t,&pg,nxt):nxt;
			if(me->sub)
			{
				nxt = GOOFF(pg,me->sub);
				klen = nxt->offset;
			}
			else
			{
				klen = me->length;
				nxt = 0;
			}
		}
		// is there any more data?
		else
		{
			if(max <= klen)
				pt->offset = max;
			else if(nxt)
			{
				pt->offset = 0;
				me = (ISPTR(nxt))?_tk_get_ptr(t,&pg,nxt):nxt;
			}
			else
				pt->offset = me->length;

			// move st_ptr
			pt->pg  = pg;
			pt->key = (char*)me - (char*)pg->pg;
			return (offset >> 3);
		}
	}
}

int st_scan(task* t, st_ptr* pt)
{
	key* k = GOOFF(pt->pg,pt->key);

	while(1)
	{
		uint tmp;

		if((k->length - pt->offset) & 0xfff8)
		{
			tmp = pt->offset >> 3;
			pt->offset += 8;
			return *(KDATA(k) + tmp);
		}

		if(k->sub == 0)
			return -1;

		tmp = k->length;
		k = GOOFF(pt->pg,k->sub);

		while(k->offset != tmp)
		{
			if(k->next == 0)
				return -1;

			k = GOOFF(pt->pg,k->next);
		}
		
		if(ISPTR(k))
			k = _tk_get_ptr(t,&pt->pg,k);

		pt->offset = 0;
		pt->key = (char*)k - (char*)pt->pg->pg;
	}
}

static uint _dont_use(void* ctx) {return -2;}

int st_map(task* t, st_ptr* str, uint(*fun)(void*,cdat,uint), void* ctx)
{
	return st_map_st(t,str,fun,_dont_use,_dont_use,ctx);
}

static uint _mv_st(struct _st_lkup_res* rt, cdat txt, uint len)
{
	rt->path = txt;
	rt->length = len << 3;
	return (_st_lookup(rt) == 0);
}

uint st_move_st(task* t, st_ptr* mv, st_ptr* str)
{
	uint ret;
	struct _st_lkup_res rt;
	rt.t      = t;
	rt.pg     = mv->pg;
	rt.sub    = GOOFF(mv->pg,mv->key);
	rt.diff   = mv->offset;

	if(ret = st_map_st(t,str,_mv_st,_dont_use,_dont_use,&rt))
		return ret;

	_pt_move(mv,&rt);
	return 0;
}

struct _del_ctx
{
	task* t;
	st_ptr from;
};

// TODO: this will not work as expected...
static uint _del_st(struct _del_ctx* ctx, cdat txt, uint len)
{
	return st_delete(ctx->t,&ctx->from,txt,len);
}

uint st_delete_st(task* t, st_ptr* from, st_ptr* str)
{
	struct _del_ctx del;
	del.from = *from;
	del.t = t;
	return st_map_st(t,str,_del_st,_dont_use,_dont_use,&del);
}

struct _st_insert
{
	struct _st_lkup_res rt;
	uint no_lookup;
};

static uint _ins_st(struct _st_insert* sins, cdat txt, uint len)
{
	sins->rt.path = txt;
	sins->rt.length = len << 3;
	if(sins->no_lookup || _st_lookup(&sins->rt) == 0)
	{
		sins->no_lookup = 1;
		_st_write(&sins->rt);
	}
	return 0;
}

uint st_insert_st(task* t, st_ptr* to, st_ptr* from)
{
	struct _st_insert sins;
	uint ret;
	sins.rt.t      = t;
	sins.rt.pg     = to->pg;
	sins.rt.sub    = GOOFF(to->pg,to->key);
	sins.rt.diff   = to->offset;
	sins.no_lookup = 0;

	if(ret = st_map_st(t,from,_ins_st,_dont_use,_dont_use,&sins))
		return ret;

	_pt_move(to,&sins.rt);
	return 0;
}

int st_compare_st(task* t, st_ptr* p1, st_ptr* p2)
{
	struct _st_lkup_res rt;
	rt.t      = t;
	rt.pg     = p1->pg;
	rt.sub    = GOOFF(p1->pg,p1->key);
	rt.diff   = p1->offset;

	return st_map_st(t,p2,_mv_st,_dont_use,_dont_use,&rt);
}

static uint _cpy_dat(task* t, st_ptr* to, cdat dat, uint len)
{
	st_insert(t,to,dat,len);
	return 0;
}

uint st_copy_st(task* t, st_ptr* to, st_ptr* from)
{
	return 1;
//	st_ptr _to = *to;
//	return st_map_ptr(t,from,&_to,_cpy_dat);
}

struct _st_map_worker_struct
{
	uint(*dat)(void*,cdat,uint);
	uint(*push)(void*);
	uint(*pop)(void*);
	void* ctx;
	task* t;
};

static uint _st_map_worker(struct _st_map_worker_struct* work, page_wrap* pg, key* me, key* nxt, uint offset)
{
	struct {
		page_wrap* pg;
		key* me;
		key* nxt;
	} mx[16];
	uint ret = 0,idx = 0;
	while(1)
	{
		uint klen = (nxt != 0)? (nxt->offset + 7): (me->length + 7);
		klen >>= 3;
		klen -= offset >> 3;
		if(klen != 0)
		{
			cdat ckey = KDATA(me) + (offset >> 3);
			if(ret = work->dat(work->ctx,ckey,klen))
				break;
		}

		if(nxt == 0)
		{
_map_pop:
			if(idx-- == 0 || (ret = work->pop(work->ctx)))
				break;

			me = mx[idx].me;
			nxt = mx[idx].nxt;
			pg = mx[idx].pg;

			offset = nxt->offset;
			nxt = (nxt->next != 0)? GOOFF(pg,nxt->next) : 0;
		}
		else
		{
			if(nxt->offset < me->length && me->length != 0)
			{
				if(ret = work->push(work->ctx))
					break;

				if(idx & 0xF0)
				{
					me = (ISPTR(nxt))?_tk_get_ptr(work->t,&pg,nxt) : nxt;
					nxt = (me->sub != 0)? GOOFF(pg,me->sub) : 0;

					if(ret = _st_map_worker(work,pg,me,nxt,0))
						break;
					goto _map_pop;
				}
				
				mx[idx].me = me;
				mx[idx].nxt = nxt;
				mx[idx].pg = pg;
				idx++;
			}

			me = (ISPTR(nxt))?_tk_get_ptr(work->t,&pg,nxt) : nxt;
			nxt = (me->sub != 0)? GOOFF(pg,me->sub) : 0;
			offset = 0;
		}
	}

	return ret;
}

uint st_map_st(task* t, st_ptr* from, uint(*dat)(void*,cdat,uint),uint(*push)(void*),uint(*pop)(void*), void* ctx)
{
	struct _st_map_worker_struct work;
	work.ctx = ctx;
	work.dat = dat;
	work.pop = pop;
	work.push = push;
	work.t = t;

	return _st_map_worker(&work,from->pg,GOOFF(from->pg,from->key),_trace_nxt(from),from->offset);
}

struct _cmp_ctx
{
	task* t;
	st_ptr ptr;
};

static uint _cmp_dat(void* p, cdat str, uint len)
{
	struct _cmp_ctx* ctx = (struct _cmp_ctx*)p;
	return 0;
}

static uint _cmp_pop(void* p)
{
	struct _cmp_ctx* ctx = (struct _cmp_ctx*)p;
	return 0;
}

static uint _cmp_push(void* p)
{
	struct _cmp_ctx* ctx = (struct _cmp_ctx*)p;
	return 0;
}

uint st_compare(task* t, st_ptr* pt1, st_ptr* pt2)
{
	struct _cmp_ctx ctx;
	ctx.t = t;

	return st_map_st(t,pt1,_cmp_dat,_cmp_push,_cmp_pop,&ctx);
}

ptr_list* ptr_list_reverse(ptr_list* e)
{
	ptr_list* link = 0;
	do
	{
		ptr_list* prev = e->link;
		e->link = link;
		link = e;
		e = prev;
	}
	while(e != 0);
	return link;
}
