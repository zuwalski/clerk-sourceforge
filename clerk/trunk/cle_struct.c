/* Copyrigth(c) Lars Szuwalski, 2005 */
#include <stdlib.h>

#include "cle_clerk.h"
#include "cle_struct.h"

static struct _st_lkup_res
{
	page_wrap* pg;
	key*	prev;
	key*	sub;
	cdat	path;
	uint	length;
	uint	diff;
};

static uint _st_lookup(struct _st_lkup_res* rt)
{
	key* me   = rt->sub;
	cdat ckey = KDATA(me) + (rt->diff >> 3);

	while(1)
	{
		uint max = (rt->length + rt->diff < me->length)?rt->length + rt->diff:me->length;

		while(rt->diff < max)/* TEST: >40%, x10 invocations. TODO: UTF-8 */
		{
			uint a = *(rt->path) ^ *ckey;
			
			if(a || max - rt->diff < 8)
			{
				while((a & 0x80) == 0)
				{
					a <<= 1;
					rt->diff++;
					if(rt->diff == max) break;
    			}
    			break;
   			}

			rt->path++;ckey++;
			rt->length -= 8;
			rt->diff += 8;
		}

		rt->sub  = me;
		rt->prev = 0;

		if(rt->length && me->sub)
		{
			me = GOOFF(rt->pg,me->sub);
			/* TEST: 10-20%, x4 invocations */
			while(me->offset < rt->diff)
			{
				rt->prev = me;

				if(!me->next)
					break;
			
				me = GOOFF(rt->pg,me->next);
			}

			if(me->offset == rt->diff)
			{
				rt->diff = 0;
				if(me->length == 0)
					me = _tk_get_ptr(&rt->pg,me);
				ckey = KDATA(me);
                continue;
			}
		}
        break;
	}
	return (rt->length == 0);
}

static ptr* _st_page_overflow(struct _st_lkup_res* rt, task* t, uint size)
{
	overflow* ovf = rt->pg->ovf;
	ptr*      pt;
	ushort    nkoff;

	if(ovf == 0)	/* alloc overflow-block */
	{
		ovf = (overflow*)tk_malloc(OVERFLOW_GROW);

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

		ovf = (overflow*)tk_realloc(ovf,ovf->size);

		rt->pg->ovf = ovf;
		/* rebuild prev-pointer in (possibly) new ovf */
		if(prev_offset)
			rt->prev = (key*)((uint)ovf + prev_offset);

		resize_count++;	// TEST
		overflow_size += OVERFLOW_GROW;	// TEST
	}

	if(t->stack == 0 || size + t->stack->pg.used > PAGE_SIZE)
		_tk_stack_new(t);

	/* make pointer */
	nkoff = (ovf->used >> 4) + PAGE_SIZE;
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

	pt->pg = t->stack;
	pt->koffset = t->stack->pg.used;
	pt->offset = rt->diff;
	pt->zero = 0;

	/* reset values */
	rt->pg = t->stack;
	rt->prev = rt->sub = 0;
	rt->diff = 0;
	
	return pt;	// for update to manipulate pointer
}

#define IS_LAST_KEY(k,pag) ((uint)(k) + ((k)->length >> 3) - (uint)&(pag->pg) + sizeof(page) + sizeof(key) + 3 > (pag)->pg.used)

/* PRE: length < PAGE_SIZE (allways) */
static void _st_write(struct _st_lkup_res* rt, task* t)
{
	key*   newkey;
	uint   size = rt->length >> 3;
	ushort nkoff;

	/* continue/append (last)key? */
	if(rt->diff == rt->sub->length && IS_LAST_KEY(rt->sub,rt->pg))
	{
		uint length = (rt->pg->pg.used + size > PAGE_SIZE)? PAGE_SIZE - rt->pg->pg.used : size;

		memcpy(KDATA(rt->sub) + (rt->diff >> 3),rt->path,length);
		rt->sub->length += length << 3;
		rt->pg->pg.used = (uint)rt->sub + (rt->sub->length >> 3) - (uint)&(rt->pg->pg) + sizeof(key);
		rt->pg->pg.used += rt->pg->pg.used & 1;

		rt->diff = rt->sub->length;		
		size -= length;
		if(size == 0)	// no remaining data?
			return;

		rt->length = size << 3;
		rt->path += length;
	}

	size += sizeof(key);

	if(size + rt->pg->pg.used > PAGE_SIZE)	/* page-overflow */
		_st_page_overflow(rt,t,size);

	nkoff  = rt->pg->pg.used;
	newkey = GOKEY(rt->pg,nkoff);
	rt->pg->pg.used += size + (size & 1);	/* short aligned */
	
	newkey->offset = rt->diff;
	newkey->length = rt->length;
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

	memcpy(KDATA(newkey),rt->path,rt->length >> 3);
	
	rt->sub  = newkey;
	rt->diff = newkey->length;
}

/* Interface-functions */

void st_empty(task* t, st_ptr* pt)
{
	key* nk;
	if(t->stack == 0 || t->stack->pg.used + sizeof(key) + 2 > PAGE_SIZE)
		_tk_stack_new(t);

	pt->key = t->stack->pg.used;
	nk = GOKEY(t->stack,pt->key);
	t->stack->pg.used += sizeof(key) + 2;

	memset(nk,0,sizeof(key) + 2);
	nk->length = 1;

	pt->pg = t->stack;
	pt->offset = 0;
}

uint st_is_empty(st_ptr* pt)
{
	key* k = GOKEY(pt->pg,pt->offset);
	if(k->length == 1) return 1;
	if(pt->offset == k->length)
	{
		if(k->sub == 0) return 1;
		k = GOOFF(pt->pg,k->sub);
		while(k->next) k = GOOFF(pt->pg,k->next);
		return (k->offset == pt->offset);
	}
	return 0;
}

uint st_exsist(st_ptr* pt, cdat path, uint length)
{
	struct _st_lkup_res rt;
	rt.path   = path;
	rt.length = length << 3;
	rt.pg     = pt->pg;
	rt.sub    = GOKEY(pt->pg,pt->key);
	rt.diff   = pt->offset;

	return _st_lookup(&rt);
}

uint st_move(st_ptr* pt, cdat path, uint length)
{
	struct _st_lkup_res rt;
	rt.path   = path;
	rt.length = length << 3;
	rt.pg     = pt->pg;
	rt.sub    = GOKEY(pt->pg,pt->key);
	rt.diff   = pt->offset;

	if(_st_lookup(&rt))
	{
		pt->pg  = rt.pg;
		pt->key = (uint)rt.sub - (uint)&rt.pg->pg;
		pt->offset = rt.diff;
	}
	return (rt.length != 0);
}

uint st_insert(task* t, st_ptr* pt, cdat path, uint length)
{
	struct _st_lkup_res rt;
	rt.path   = path;
	rt.length = length << 3;
	rt.pg     = pt->pg;
	rt.sub    = GOKEY(pt->pg,pt->key);
	rt.diff   = pt->offset;

	if(!_st_lookup(&rt))
		_st_write(&rt,t);

	pt->pg     = rt.pg;
	pt->key    = (uint)rt.sub - (uint)&rt.pg->pg;
	pt->offset = rt.diff;
	return (rt.length != 0);
}

uint st_update(task* t, st_ptr* pt, cdat path, uint length)
{
	struct _st_lkup_res rt;
	page_wrap* rm_pg;
	ushort remove = 0;
	ushort waste;

	rm_pg = rt.pg = pt->pg;
	rt.diff = pt->offset;
	rt.sub  = GOKEY(pt->pg,pt->key);
	rt.prev = 0;

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
			_st_write(&rt,t);
		}
	}

	if(rm_pg->page_adr)	// should we care about cleanup?
	{
		rm_pg->pg.waste += waste >> 3;
		_tk_remove_tree(t,rm_pg,remove);
	}

	pt->pg     = rt.pg;
	pt->key    = (uint)rt.sub - (uint)&rt.pg->pg;
	pt->offset = rt.diff;
	return 0;
}

uint st_append(task* t, st_ptr* pt, cdat path, uint length)
{
	struct _st_lkup_res rt;
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

			rt.sub  = (nxt->length == 0)?_tk_get_ptr(&rt.pg,nxt):nxt;
			if(rt.sub->sub == 0)
			{
				rt.diff = rt.sub->length;
				break;
			}

			nxt = GOOFF(rt.pg,rt.sub->sub);
		}
	}

	_st_write(&rt,t);

	pt->pg     = rt.pg;
	pt->key    = (uint)rt.sub - (uint)&rt.pg->pg;
	pt->offset = rt.diff;
	return 0;
}

uint st_prepend(task* t, st_ptr* pt, cdat path, uint length)
{
	struct _st_lkup_res rt;
	key* me,*nxt = 0;
	ushort waste = 0, k = 0;

	rt.path   = path;
	rt.length = length << 3;
	rt.pg     = pt->pg;
	rt.prev   = 0;
	me = rt.sub = GOKEY(pt->pg,pt->key);
	rt.diff   = me->length != 1? pt->offset : 1;

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

	_st_write(&rt,t);	// write new

	if(k && nxt == 0)
	{
		rt.sub->sub = k;	// attach sub-tree
		nxt = GOKEY(pt->pg,k);
		nxt->offset = rt.sub->length;
	}
	
	if(me->length > pt->offset && me->length > 1)	// rest? (not just append)
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
	cdat ckey     = KDATA(me) + (pt->offset >> 3);
	uint klen     = me->length - pt->offset;

	offset <<= 3;

	while(1)
	{
		key* nxt = 0;
		uint max;

		if(me->sub)
		{
			nxt = GOOFF(pg,me->sub);
			klen = nxt->offset;
		}

		max = offset > klen?klen:offset;
		offset -= max;

		// move to next key for more data?
		if(offset > 0 && nxt && nxt->offset == me->length)
		{
			me = (nxt->length == 0)?_tk_get_ptr(&pg,nxt):nxt;
			ckey = KDATA(me);
			klen = me->length;
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

int st_get(st_ptr* pt, char* buffer, uint length)
{
	page_wrap* pg = pt->pg;
	key* me       = GOKEY(pt->pg,pt->key);
	cdat ckey     = KDATA(me) + (pt->offset >> 3);
	uint klen     = me->length - pt->offset;
	uint offset   = pt->offset;

	length <<= 3;

	while(1)
	{
		key* nxt = 0;
		uint max = 0;

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
			if(nxt)
				klen = nxt->offset - offset;
		}

		if(klen > 0)
		{
			max = (length > klen?klen:length)>>3;
			memcpy(buffer,ckey,max);
			buffer += max;
			length -= max << 3;
		}

		// move to next key for more data?
		if(length > 0)
		{
			// no next key! or trying to read past split?
			if(nxt == 0 || nxt->offset < me->length)
				return (length >> 3);

			me = (nxt->length == 0)?_tk_get_ptr(&pg,nxt):nxt;
			ckey = KDATA(me);
			klen = me->length;
			offset = 0;
		}
		// is there any more data?
		else
		{
			max <<= 3;
			max += offset;

			if(max < klen)	// more on this key?
				pt->offset = max;
			else if(nxt && nxt->offset == me->length)	// continuing key?
			{
				pt->offset = 0;
				me = (nxt->length == 0)?_tk_get_ptr(&pg,nxt):nxt;
			}
			else
				return 0;	// no more!

			// yes .. tell caller and move st_ptr
			pt->pg  = pg;
			pt->key = (uint)me - (uint)&pg->pg;
			return -1;
		}
	}
}

char* st_get_all(st_ptr* pt, uint* length)
{
	char* buffer  = 0;
	page_wrap* pg = pt->pg;
	key* me       = GOKEY(pt->pg,pt->key);
	cdat ckey     = KDATA(me) + (pt->offset >> 3);
	uint klen     = me->length - pt->offset;
	uint offset   = pt->offset;
	uint nlength  = 128;
	uint rlength  = 0;
	uint boffset  = 0;

	while(1)
	{
		key* nxt = 0;

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
			if(nxt)
				klen = nxt->offset - offset;
		}

		klen >>= 3;
		if(klen > 0)
		{
			if(klen + boffset > rlength)
			{
				nlength = klen > nlength? klen : nlength;
				buffer = (char*)tk_realloc(buffer,nlength);
				rlength = nlength;
				nlength <<= 1;
			}

			memcpy(buffer + boffset,ckey,klen);
			boffset += klen;
		}

		// no next key! or trying to read past split?
		if(nxt == 0 || nxt->offset < me->length)
		{
			*length = boffset;
			return buffer;
		}

		// move to next key for more data
		me = (nxt->length == 0)?_tk_get_ptr(&pg,nxt):nxt;
		ckey = KDATA(me);
		klen = me->length;
		offset = 0;
	}
}
