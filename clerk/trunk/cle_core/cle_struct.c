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
#include "cle_clerk.h"
#include "cle_struct.h"

struct _st_lkup_res
{
	task* t;
	page_wrap* pg;
	key*  prev;
	key*  sub;
	cdat  path;
	uint  length;
	uint  diff;
};

static uint _st_lookup(struct _st_lkup_res* rt)
{
	key* me   = rt->sub;
	cdat ckey = KDATA(me) + (rt->diff >> 3);

	while(1)
	{
		uint max = (rt->length + rt->diff < me->length) ? rt->length + rt->diff : me->length;

		if(rt->diff < max)
		{
			while(1)
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

					rt->diff += 8 - a;
					if(rt->diff > max)
						rt->diff = max;
					break;
				}

				rt->diff += 8;
				if(rt->diff > max)
				{
					rt->diff = max;
					break;
				}
				rt->length -= 8;
				rt->path++;ckey++;
			}
		}

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

		if(me->length == 0)
			me = _tk_get_ptr(rt->t,&rt->pg,me);
		ckey = KDATA(me);
		rt->diff = 0;
	}
	return (rt->length == 0);
}

static ptr* _st_page_overflow(struct _st_lkup_res* rt, uint size)
{
	overflow* ovf = rt->pg->ovf;
	ptr*      pt;
	ushort    nkoff;

	if(ovf == 0)	/* alloc overflow-block */
	{
		ovf = (overflow*)tk_malloc(rt->t,OVERFLOW_GROW);

		ovf->size = OVERFLOW_GROW;
		ovf->used = 16;

		rt->pg->ovf = ovf;

		overflow_size += OVERFLOW_GROW;	// TEST
	}
	else if(ovf->used == ovf->size)	/* resize overflow-block */
	{
		/* as ovf may change and prev might point inside it .. */
		uint prev_offset = 
		((uint)rt->prev > (uint)ovf && (uint)rt->prev < (uint)ovf + ovf->size)?
			(uint)rt->prev - (uint)ovf : 0;

		ovf->size += OVERFLOW_GROW;

		ovf = (overflow*)tk_realloc(rt->t,ovf,ovf->size);

		rt->pg->ovf = ovf;
		/* rebuild prev-pointer in (possibly) new ovf */
		if(prev_offset)
			rt->prev = (key*)((uint)ovf + prev_offset);

		resize_count++;	// TEST
		overflow_size += OVERFLOW_GROW;	// TEST
	}

	if(size + rt->t->stack->pg->used > rt->t->stack->pg->size)
		_tk_stack_new(rt->t);

	/* make pointer */
	nkoff = (ovf->used >> 4) | 0x8000;
	pt = (ptr*)((char*)ovf + ovf->used);
	ovf->used += 16;

	if(rt->prev)
	{
		pt->next = rt->prev->next;
		rt->prev->next = nkoff;
	}
	else /* sub MUST be there */
	{
		pt->next = rt->sub->sub;
		rt->sub->sub = nkoff;
	}

	pt->pg = rt->t->stack;
	pt->koffset = rt->t->stack->pg->used;
	pt->offset = rt->diff;
	pt->zero = 0;

	/* reset values */
	rt->pg = rt->t->stack;
	rt->prev = rt->sub = 0;
//	rt->diff = 0;			// goes into "offset" - still relates to parent (?)
	
	rt->pg->refcount++;
	return pt;	// for update to manipulate pointer
}

#define IS_LAST_KEY(k,pag) ((uint)(k) + ((k)->length >> 3) - (uint)(pag) + sizeof(page) + sizeof(key) + 3 > (pag)->used)

static void _st_write(struct _st_lkup_res* rt)
{
	key* newkey;
	uint size = rt->length >> 3;

	/* readonly pages? */
	if(rt->t == 0)
		return;

	/* make a writable copy of external pages before write */
	if(rt->pg->pg->id)
	{
		page* old = rt->pg->pg;
		
		_tk_write_copy(rt->t,rt->pg);

		/* fix pointers */
		if(rt->prev)
			rt->prev = GOKEY(rt->pg,(uint)rt->prev - (uint)old);

		if(rt->sub)
			rt->sub = GOKEY(rt->pg,(uint)rt->sub - (uint)old);
	}

	/* continue/append (last)key? */
	if(rt->diff == rt->sub->length && IS_LAST_KEY(rt->sub,rt->pg->pg))
	{
		uint length = (rt->pg->pg->used + size > rt->pg->pg->size)? rt->pg->pg->size - rt->pg->pg->used : size;

		memcpy(KDATA(rt->sub) + (rt->diff >> 3),rt->path,length);
		rt->sub->length = (rt->sub->length & 0xFFF8) + (length << 3);
		rt->pg->pg->used = (uint)rt->sub + (rt->sub->length >> 3) - (uint)(rt->pg->pg) + sizeof(key);

		rt->diff = rt->sub->length;		
		size -= length;
		if(size == 0)	// no remaining data?
			return;

		rt->length = size << 3;
		rt->path += length;
	}

	do
	{
		uint   room   = rt->pg->pg->size - rt->pg->pg->used;
		uint   length = rt->length;
		uint   pgsize = size;
		ushort nkoff;

		pgsize += sizeof(key);

		if(pgsize > room)	/* page-overflow */
		{
			if(room > sizeof(key))	/* room for any data at all? */
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

		nkoff  = rt->pg->pg->used;
		newkey = GOKEY(rt->pg,nkoff);
		rt->pg->pg->used += pgsize + (pgsize & 1);	/* short aligned */
		
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

		rt->diff = length;
		rt->path += length >> 3;
		size     -= pgsize - sizeof(key);
	}
	while(size);
	
	rt->sub  = newkey;
	rt->diff = newkey->length;
}

/* Interface-functions */

uint st_empty(task* t, st_ptr* pt)
{
	key* nk;

	/* readonly ? */
	if(t == 0)
		return 1;
	
	nk = tk_alloc(t,sizeof(key) + 2);

	pt->key = (uint)nk - (uint)t->stack->pg;
	pt->pg = t->stack;
	pt->offset = 0;

	memset(nk,0,sizeof(key) + 2);
	nk->length = 1;
	return 0;
}

uint st_is_empty(st_ptr* pt)
{
	key* k = GOKEY(pt->pg,pt->key);
	if(k->length == 1) return 1;
	if(pt->offset != k->length) return 0;
	if(k->sub == 0) return 1;
	k = GOOFF(pt->pg,k->sub);
	while(k->next && k->offset < pt->offset) k = GOOFF(pt->pg,k->next);
	return (k->offset < pt->offset);
}

uint st_exsist(task* t, st_ptr* pt, cdat path, uint length)
{
	struct _st_lkup_res rt;
	rt.t      = t;
	rt.path   = path;
	rt.length = length << 3;
	rt.pg     = pt->pg;
	rt.sub    = GOKEY(pt->pg,pt->key);
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
	rt.sub    = GOKEY(pt->pg,pt->key);
	rt.diff   = pt->offset;

	if(_st_lookup(&rt))
	{
		pt->pg  = rt.pg;
		pt->key = (uint)rt.sub - (uint)rt.pg->pg;
		pt->offset = rt.diff;
	}
	return (rt.length != 0);
}

uint st_insert(task* t, st_ptr* pt, cdat path, uint length)
{
	struct _st_lkup_res rt;
	rt.t      = t;
	rt.path   = path;
	rt.length = length << 3;
	rt.pg     = pt->pg;
	rt.sub    = GOKEY(pt->pg,pt->key);
	rt.diff   = pt->offset;

	if(!_st_lookup(&rt))
		_st_write(&rt);
	else rt.path = 0;

	pt->pg     = rt.pg;
	pt->key    = (uint)rt.sub - (uint)rt.pg->pg;
	pt->offset = rt.diff;
	return (rt.path != 0);
}

uint st_update(task* t, st_ptr* pt, cdat path, uint length)
{
	struct _st_lkup_res rt;
	page_wrap* rm_pg;
	ushort remove = 0;
	ushort waste;

	if(t == 0)
		return 1;

	rm_pg = rt.pg = pt->pg;
	rt.diff = pt->offset;
	rt.sub  = GOKEY(pt->pg,pt->key);
	rt.prev = 0;
	rt.t    = t;

	if(rt.sub->sub)
	{
		key* nxt = GOOFF(rt.pg,rt.sub->sub);
		while(nxt->next && nxt->offset < pt->offset)
		{
			rt.prev = nxt;
			nxt = GOOFF(rt.pg,nxt->next);
		}

		if(rt.prev)
		{
			remove = rt.prev->next;
			rt.prev->next = 0;
		}
		else
		{
			remove = rt.sub->sub;
			rt.sub->sub = 0;
		}
	}

	waste = rt.sub->length - rt.diff;
	rt.sub->length = (rt.diff > 0)? rt.diff : 1;

	if(length > 0)
	{
		length <<= 3;
		/* old key has room for this update*/
		if(waste >= length)
		{
			memcpy(KDATA(rt.sub)+(rt.diff >> 3),path,length >> 3);
			rt.diff = rt.sub->length = length + rt.diff;
			waste -= length;
		}
		else
		{
			rt.path   = path;
			rt.length = length;
			_st_write(&rt);
		}
	}

	if(rm_pg->pg->id)	// should we care about cleanup?
	{
		rm_pg->pg->waste += waste >> 3;
		_tk_remove_tree(t,rm_pg,remove);
	}

	pt->pg     = rt.pg;
	pt->key    = (uint)rt.sub - (uint)rt.pg->pg;
	pt->offset = rt.diff;
	return 0;
}

uint st_append(task* t, st_ptr* pt, cdat path, uint length)
{
	struct _st_lkup_res rt;
	rt.t      = t;
	rt.path   = path;
	rt.length = length << 3;
	rt.pg     = pt->pg;
	rt.sub    = GOKEY(pt->pg,pt->key);
	rt.diff   = rt.sub->length;
	rt.prev   = 0;

	if(rt.sub->sub)
	{
		key* nxt = GOOFF(rt.pg,rt.sub->sub);
		while(nxt->next && nxt->offset < pt->offset)
			nxt = GOOFF(rt.pg,nxt->next);

		while(1)
		{
			if(nxt->offset != rt.sub->length)
				return 1;

			rt.sub  = (nxt->length == 0)?_tk_get_ptr(t,&rt.pg,nxt):nxt;
			if(rt.sub->sub == 0)
			{
				rt.diff = rt.sub->length;
				break;
			}

			nxt = GOOFF(rt.pg,rt.sub->sub);
		}
	}

	_st_write(&rt);

	pt->pg     = rt.pg;
	pt->key    = (uint)rt.sub - (uint)rt.pg->pg;
	pt->offset = rt.diff;
	return 0;
}

int st_get(task* t, st_ptr* pt, char* buffer, uint length)
{
	page_wrap* pg = pt->pg;
	key* me      = GOKEY(pt->pg,pt->key);
	key* nxt     = 0;
	cdat ckey    = KDATA(me) + (pt->offset >> 3);
	uint offset  = pt->offset;
	uint klen;
	int read     = 0;

	if(me->sub)
	{
		nxt = GOOFF(pg,me->sub);
		while(nxt->offset < offset)
		{
			if(nxt->next == 0)
			{
				nxt = 0;
				break;
			}
			nxt = GOOFF(pg,nxt->next);
		}
	}

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
			if(nxt == 0 || (nxt->offset < me->length && me->length != 1))
			{
				pt->offset = max + (offset & 0xFFF8);
				break;
			}

			offset = 0;
			me = (nxt->length == 0)?_tk_get_ptr(t,&pg,nxt):nxt;
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
				me = (nxt->length == 0)?_tk_get_ptr(t,&pg,nxt):nxt;
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

	pt->key = (uint)me - (uint)pg->pg;
	pt->pg  = pg;
	return read;
}
/*
// should use repeatede calls to st_get(...) with own buffer
char* st_get_all(task* t, st_ptr* pt, uint* length)
{
	char* buffer  = 0;
	page_wrap* pg = pt->pg;
	key* me       = GOKEY(pt->pg,pt->key);
	key* nxt      = 0;
	cdat ckey     = KDATA(me) + (pt->offset >> 3);
	uint rlength  = 0;
	uint boffset  = 0;
	uint klen;

	if(me->sub)
	{
		nxt = GOOFF(pg,me->sub);
		while(nxt->offset < pt->offset)
		{
			if(nxt->next == 0)
			{
				nxt = 0;
				break;
			}
			nxt = GOOFF(pg,nxt->next);
		}
	}

	klen = ((nxt)? nxt->offset : me->length) - pt->offset;

	while(1)
	{
		klen >>= 3;
		if(klen > 0)
		{
			uint max = klen;
			//for(klen = 0; klen < max && ckey[klen]; klen++);	// find zero-term

			if(klen + boffset > rlength)
			{
				rlength += klen > GET_ALL_BUFFER? klen : GET_ALL_BUFFER;
				buffer = (char*)tk_realloc(t,buffer,rlength);
			}

			memcpy(buffer + boffset,ckey,klen);
			boffset += klen;

			if(max != klen)	// stop at zero-term
			{
				pt->pg = pg;
				pt->key = (uint)me - (uint)pg;
				pt->offset = klen << 3;
				*length = boffset;
				return buffer;
			}
		}

		// no next key! or trying to read past split?
		if(nxt == 0 || (nxt->offset < me->length && me->length != 1))
		{
			pt->pg = pg;
			pt->key = (uint)me - (uint)pg->pg;
			pt->offset = nxt? nxt->offset : me->length;
			*length = boffset;
			return buffer;
		}

		// move to next key for more data
		me = (nxt->length == 0)?_tk_get_ptr(t,&pg,nxt):nxt;
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
}

// ------ USED? ----------------------

// FIXME 
uint st_prepend(task* t, st_ptr* pt, cdat path, uint length, uint replace_length)
{
	struct _st_lkup_res rt;
	key* me,*nxt = 0;
	ushort waste = 0, k = 0;

	rt.path   = path;
	rt.length = length << 3;
	rt.pg     = pt->pg;
	rt.prev   = 0;
	me = rt.sub = GOKEY(pt->pg,pt->key);
	rt.diff   = rt.sub->length == 1? 1 : pt->offset;

	if(pt->offset > 0 && me->sub)
	{
		nxt = GOOFF(rt.pg,me->sub);
		while(nxt->offset < pt->offset)
		{
			rt.prev = nxt;
			if(nxt->next == 0)
				break;
			nxt = GOOFF(rt.pg,nxt->next);
		}

		k = nxt->next;
		nxt->next = 0;
	}
	else
	{
		k = me->sub;
		me->sub = 0;
	}

	_st_write(&rt);	// write new

	if(k && nxt == 0)
	{
		rt.sub->sub = k;	// attach sub-tree
		nxt = GOKEY(pt->pg,k);
		nxt->offset = rt.sub->length;
	}
	
	if((me->length & 0xFFF8) > pt->offset)	// rest? (not just append)
	{
		const uint dec = pt->offset & 0xFFF8;
		waste = me->length + 7 - dec;

		rt.path   = KDATA(me) + (pt->offset >> 3);
		rt.length = (me->length + 7 >> 3) - (pt->offset >> 3);
		rt.diff   = length << 3;
		rt.prev   = 0;	// sub updated from last write

		_st_write(&rt,t);	// write rest

		if(pt->pg != rt.pg) 
			exit(-1);

		rt.sub->sub = k;	// move subs
		while(dec && k)
		{
			nxt = GOKEY(pt->pg,k);
			nxt->offset -= dec;
			k = nxt->next;
		}
	}

	me->length = (pt->offset > 0)? pt->offset : 1;

	waste >>= 3;
	if(waste > 0 && pt->pg->page_adr)	// should we care about cleanup?
	{
		pt->pg->pg.waste += waste;
		_tk_remove_tree(t,pt->pg,0);
	}

	return 0;
}

uint st_offset(st_ptr* pt, uint offset)
{
	page_wrap* pg = pt->pg;
	key* me       = GOKEY(pt->pg,pt->key);
	key* nxt;
	cdat ckey     = KDATA(me) + (pt->offset >> 3);
	uint klen;

	if(me->sub)
	{
		nxt = GOOFF(pg,me->sub);
		while(nxt->offset < pt->offset)
		{
			if(nxt->next == 0)
			{
				nxt = 0;
				break;
			}
			nxt = GOOFF(pg,nxt->next);
		}
	}

	klen = ((nxt)? nxt->offset : me->length) - pt->offset;

	offset <<= 3;

	while(1)
	{
		uint max = offset > klen?klen:offset;
		offset -= max;

		// move to next key for more data?
		if(offset > 0 && nxt && nxt->offset == me->length)
		{
			me = (nxt->length == 0)?_tk_get_ptr(&pg,nxt):nxt;
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

			if(max <= klen)
				pt->offset = max;
			else if(nxt)
			{
				pt->offset = 0;
				me = (nxt->length == 0)?_tk_get_ptr(&pg,nxt):nxt;
			}
			else
				pt->offset = me->length;

			// move st_ptr
			pt->pg  = pg;
			pt->key = (uint)me - (uint)&pg->pg;
			return (offset >> 3);
		}
	}
}
*/
