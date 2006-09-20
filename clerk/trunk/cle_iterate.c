/* Copyrigth(c) Lars Szuwalski, 2005 */
#include <stdlib.h>

#include "cle_clerk.h"
#include "cle_struct.h"

/* ---------- iterator -------------- */

static struct _st_lkup_it_res
{
	page_wrap* pg;
	page_wrap* low_pg;
	page_wrap* high_pg;
	key*	prev;
	key*	sub;
	key*	low;
	key*	low_prev;
	key*	high;
	key*	high_prev;
	uchar*	path;
	uchar*	low_path;
	uchar*	high_path;
	uint	length;
	uint	diff;
};

static void _it_lookup(struct _st_lkup_it_res* rt)
{
	key* me   = rt->sub;
	cdat ckey = KDATA(me) + (rt->diff >> 3);
	uchar* atsub = rt->path - (rt->diff >> 3);
	uint offset = rt->diff;

	rt->high = rt->high_prev = rt->low = rt->low_prev = 0;
	rt->high_path = rt->low_path = 0;

	while(1)
	{
		uint max = (rt->length + rt->diff < me->length)?rt->length + rt->diff:me->length;

		while(rt->diff < max)
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

		if(me->sub)
		{
			ckey = KDATA(me);
			me = GOOFF(rt->pg,me->sub);

			while(me->offset < rt->diff)
			{
				rt->prev = me;

				if(me->offset > offset)
				{
					if(*(ckey + (me->offset>>3)) & (0x80 >> (me->offset & 7)))
					{
						rt->low = me;
						rt->low_prev = 0;
						rt->low_path = atsub + (me->offset>>3);
						rt->low_pg = rt->pg;
					}
					else
					{
						rt->high = me;
						rt->high_prev = 0;
						rt->high_path = atsub + (me->offset>>3);
						rt->high_pg = rt->pg;
					}
				}

				if(!me->next)
					break;
			
				me = GOOFF(rt->pg,me->next);
			}

			if(rt->length != 0 && me->offset == rt->diff)
			{
				if(rt->diff != rt->sub->length)
				{
					if(*rt->path & (0x80 >> (rt->diff & 7)))
					{
						rt->low = rt->sub;
						rt->low_prev = me;
						rt->low_path = rt->path;
						rt->low_pg = rt->pg;
					}
					else
					{
						rt->high = rt->sub;
						rt->high_prev = me;
						rt->high_path = rt->path;
						rt->high_pg = rt->pg;
					}
				}
				rt->diff = 0;
				offset = 0;
				atsub = rt->path;
				// if this is a pointer - resolve it
				if(me->length == 0)
					me = _tk_get_ptr(&rt->pg,me);
				ckey = KDATA(me);
                continue;
			}
		}
        break;
	}
}

static void _it_grow_kdata(it_ptr* it, struct _st_lkup_it_res* rt)
{
	uint path_offset = (uint)rt->path - (uint)it->kdata;
	it->ksize += IT_GROW_SIZE;
	it->kdata = (uchar*)tk_realloc(it->kdata,it->ksize);
	rt->path = it->kdata + path_offset;
}

static void _it_get_prev(struct _st_lkup_it_res* rt, uint offset)
{
	if(offset && rt->sub->sub)
	{
		key* nxt = GOKEY(rt->pg,rt->sub->sub);
		while(nxt->offset < offset)
		{
			rt->prev = nxt;
			if(nxt->next == 0)
				break;
			nxt = GOKEY(rt->pg,nxt->next);
		}
	}
}

static void _it_next(it_ptr* it, struct _st_lkup_it_res* rt, uint offset)
{
	key* prev = rt->prev;
	key* sub  = rt->sub;

	it->kused = (uint)rt->path - (uint)it->kdata;

	do
	{
		cdat ckey;
		uint clen;

		if(sub->length == 0)
			sub = _tk_get_ptr(&rt->pg,sub);

		rt->sub = sub;
		ckey = KDATA(sub);

		if(prev)
			prev = (prev->next) ? GOOFF(rt->pg,prev->next) : 0;
		else if(sub->sub)
			prev = GOOFF(rt->pg,sub->sub);

		if(prev)
		{
			while(prev->offset < sub->length &&
				(*(ckey + (prev->offset>>3)) & (0x80 >> (prev->offset & 7))) == 0)
   			{
				if(prev->next == 0)
				{
					prev = 0;
					break;
				}
				prev = GOOFF(rt->pg,prev->next);
			}
		}

		clen = (prev) ? prev->offset : sub->length;
		rt->diff = offset;
		if(offset)
		{
			clen -= offset;
			ckey += offset >> 3;
			offset = 0;
		}
		clen >>= 3;
		
		while(clen-- != 0)
		{
			it->kused++;
			if(it->kused > it->ksize)
				_it_grow_kdata(it,rt);

			rt->diff += 8;
			if((*rt->path++ = *ckey++) == 0)
   				return;
  		}

		sub = prev;
		prev = 0;
	}
	while(sub);
}

static void _it_prev(it_ptr* it, struct _st_lkup_it_res* rt, uint offset)
{
	key* prev = rt->prev;
	key* sub  = rt->sub;

	it->kused = (uint)rt->path - (uint)it->kdata;

	do
	{
		cdat ckey;
		uint clen;

		if(sub->length == 0)
			sub = _tk_get_ptr(&rt->pg,sub);

		rt->sub = sub;
		ckey = KDATA(sub);

		if(prev)
			prev = (prev->next) ? GOOFF(rt->pg,prev->next) : 0;
		else if(sub->sub)
			prev = GOOFF(rt->pg,sub->sub);

		if(prev)
		{
			while(prev->offset < sub->length && 
				(*(ckey + (prev->offset>>3)) & (0x80 >> (prev->offset & 7))) != 0)
   			{
				if(prev->next == 0)
				{
					prev = 0;
					break;
				}
				prev = GOOFF(rt->pg,prev->next);
			}
		}

		clen = (prev) ? prev->offset : sub->length;
		rt->diff = offset;
		if(offset)
		{
			clen -= offset;
			ckey += offset >> 3;
			offset = 0;
		}
		clen >>= 3;
		
		while(clen-- != 0)
		{
			it->kused++;
			if(it->kused > it->ksize)
				_it_grow_kdata(it,rt);

			rt->diff += 8;
			if((*rt->path++ = *ckey++) == 0)
   				return;
  		}

		sub = prev;
		prev = 0;
	}
	while(sub);
}

uint it_next(st_ptr* pt, it_ptr* it)
{
	struct _st_lkup_it_res rt;
	uint offset = it->offset;
	rt.path   = it->kdata;
	rt.length = it->kused << 3;
	rt.pg     = it->pg;
	rt.sub    = GOKEY(it->pg,it->key);
	rt.prev   = 0;
	rt.diff   = offset;

	if(rt.length > 0)
	{
		key* sub = rt.sub;
		_it_lookup(&rt);

		if(rt.high == 0)
		{
			if(rt.length == 0)
				return 0;
		}
		else if(rt.length == 0)
		{
			rt.sub  = rt.high;
			rt.prev = rt.high_prev;
			rt.path = rt.high_path;
			rt.pg   = rt.high_pg;
		}

		if(rt.sub != sub) offset = 0;
	}
	else
		_it_get_prev(&rt,offset);

	_it_next(it,&rt,offset);

	if(pt)
	{
		pt->pg  = rt.pg;
		pt->key = (uint)rt.sub - (uint)&rt.pg->pg;
		pt->offset = rt.diff;
	}
	return (it->kused > 0);
}

uint it_next_eq(st_ptr* pt, it_ptr* it)
{
	struct _st_lkup_it_res rt;
	uint offset = it->offset;
	rt.path   = it->kdata;
	rt.length = it->kused << 3;
	rt.pg     = it->pg;
	rt.sub    = GOKEY(it->pg,it->key);
	rt.prev   = 0;
	rt.diff   = offset;

	if(rt.length > 0)
	{
		key* sub = rt.sub;
		_it_lookup(&rt);

		if(rt.length == 0)
		{
			if(pt)
			{
				pt->pg  = rt.pg;
				pt->key = (uint)rt.sub - (uint)&rt.pg->pg;
				pt->offset = rt.diff;
			}
			return 1;
		}

		if(rt.high == 0)
			return 0;

		rt.sub  = rt.high;
		rt.prev = rt.high_prev;
		rt.path = rt.high_path;
		rt.pg   = rt.high_pg;
		if(rt.sub != sub) offset = 0;
	}
	else
		_it_get_prev(&rt,offset);

	_it_next(it,&rt,offset);

	if(pt)
	{
		pt->pg  = rt.pg;
		pt->key = (uint)rt.sub - (uint)&rt.pg->pg;
		pt->offset = rt.diff;
	}
	return (it->kused > 0);
}

uint it_prev(st_ptr* pt, it_ptr* it)
{
	struct _st_lkup_it_res rt;
	uint offset = it->offset;
	rt.path   = it->kdata;
	rt.length = it->kused << 3;
	rt.pg     = it->pg;
	rt.sub    = GOKEY(it->pg,it->key);
	rt.prev   = 0;
	rt.diff   = offset;

	if(rt.length > 0)
	{
		key* sub = rt.sub;
		_it_lookup(&rt);

		if(rt.low == 0)
		{
			if(rt.length == 0)
				return 0;
		}
		else if(rt.length == 0)
		{
			rt.sub  = rt.low;
			rt.prev = rt.low_prev;
			rt.path = rt.low_path;
			rt.pg   = rt.low_pg;
		}

		if(rt.sub != sub) offset = 0;
	}
	else
		_it_get_prev(&rt,offset);

	_it_prev(it,&rt,offset);

	if(pt)
	{
		pt->pg  = rt.pg;
		pt->key = (uint)rt.sub - (uint)&rt.pg->pg;
		pt->offset = rt.diff;
	}
	return (it->kused > 0);
}

uint it_prev_eq(st_ptr* pt, it_ptr* it)
{
	struct _st_lkup_it_res rt;
	uint offset = it->offset;
	rt.path   = it->kdata;
	rt.length = it->kused << 3;
	rt.pg     = it->pg;
	rt.sub    = GOKEY(it->pg,it->key);
	rt.prev   = 0;
	rt.diff   = it->offset;

	if(rt.length > 0)
	{
		key* sub = rt.sub;
		_it_lookup(&rt);

		if(rt.length == 0)
		{
			if(pt)
			{
				pt->pg  = rt.pg;
				pt->key = (uint)rt.sub - (uint)&rt.pg->pg;
				pt->offset = rt.diff;
			}
			return 1;
		}

		if(rt.low == 0)
			return 0;

		rt.sub  = rt.low;
		rt.prev = rt.low_prev;
		rt.path = rt.low_path;
		rt.pg   = rt.low_pg;

		if(rt.sub != sub) offset = 0;
	}
	else
		_it_get_prev(&rt,offset);

	_it_prev(it,&rt,offset);

	if(pt)
	{
		pt->pg  = rt.pg;
		pt->key = (uint)rt.sub - (uint)&rt.pg->pg;
		pt->offset = rt.diff;
	}
	return (it->kused > 0);
}

void it_load(it_ptr* it, cdat path, uint length)
{
	if(it->ksize < length)
	{
		it->ksize = length;
		it->kdata = (uchar*)realloc(it->kdata,it->ksize);
		if(it->kdata == 0)
			unimplm();
	}

	memcpy(it->kdata,path,length);

	it->kused = length;
}

void it_create(it_ptr* it, st_ptr* pt)
{
	it->pg     = pt->pg;
	it->key    = pt->key;
	it->offset = pt->offset;

	it->kdata  = 0;
	it->ksize  = it->kused = 0;
}

void it_dispose(it_ptr* it)
{
	tk_mfree(it->kdata);
}

/**
* update/build increasing index
*/
uint it_new(task* t, it_ptr* it, st_ptr* pt)
{
	struct _st_lkup_it_res rt;
	rt.path   = it->kdata;
	rt.length = 0;
	rt.pg     = it->pg;
	rt.sub    = GOKEY(it->pg,it->key);
	rt.prev   = 0;
	rt.diff   = it->offset;
	it->kused = 0;

	_it_get_prev(&rt,it->offset);

	_it_prev(it,&rt,it->offset);	// get highest position

	if(it->kused == 0)	// init 1.index
	{
		if(it->ksize == 0)
			_it_grow_kdata(it,&rt);

		it->kdata[0] = it->kdata[1] = 1;
		it->kdata[2] = 0;
		it->kused = 3;
	}
	else if(it->kused < 3 || it->kused != it->kdata[0] + 2)
		return 1;	// can not be an index
	else
	{				// incr. index
		uint idx = it->kused - 2;
		
		while(idx != 0 && it->kdata[idx] == 0xFF)
		{
			it->kdata[idx] = 1;
			idx--;
		}

		if(idx == 0)
		{
			if(it->kdata[0] == 0xFF)
				return 1;

			if(it->kused == it->ksize)
				_it_grow_kdata(it,&rt);

			it->kdata[it->kused - 1] = 1;
			it->kdata[it->kused] = 0;
			it->kdata[0]++;
			it->kused++;
		}
		else
			it->kdata[idx]++;
	}

	pt->pg = it->pg;
	pt->key = it->key;
	pt->offset = it->offset;

	return !st_insert(t,pt,it->kdata,it->kused);
}

/* delete function - uses it_lookup */
static uint _st_splice_key(page_wrap* rm_pg, ushort remove)
{
	if(remove)
	{
		key* tmp = GOKEY(rm_pg,remove);
		key* nxt = (tmp->next)?GOKEY(rm_pg,tmp->next):0;
		tmp->next = 0;
		if(nxt)
			return (uint)nxt - (uint)&rm_pg->pg;
	}
	return 0;
}

static uint _st_remove_key(page_wrap* rm_pg, key* sub, key* prev)
{
	uint remove;
	if(prev)
	{
		remove = prev->next;
		prev->next = _st_splice_key(rm_pg,remove);
	}
	else
	{
		remove = sub->sub;
		sub->sub = _st_splice_key(rm_pg,remove);
	}

	return remove;
}

uint st_delete(task* t, st_ptr* pt, cdat path, uint length)
{
	struct _st_lkup_it_res rt;
	page_wrap* rm_pg;
	uint waste  = 0;
	uint remove = 0;

	rt.pg     = pt->pg;
	rt.sub    = GOKEY(pt->pg,pt->key);
	rt.diff   = pt->offset;

	if(length > 0)
	{
		rt.path   = (uchar*)path;
		rt.length = length << 3;

		_it_lookup(&rt);

		if(rt.length != 0)
			return 1;

		if(rt.prev)
		{
			waste  = rt.sub->length - rt.diff;
			remove = rt.prev->next;
			rm_pg  = rt.pg;

			rt.sub->length = (rt.diff > 0)? rt.diff : 1;
			rt.prev->next = 0;
		}
		else
		{
			if(rt.low && rt.high)
			{
				if(rt.low_path < rt.high_path)
					rt.low = 0;
				else
					rt.high = 0;
			}

			if(rt.low)
			{
				rm_pg = rt.low_pg;
				remove = _st_remove_key(rm_pg,rt.low,rt.low_prev);
			}
			else if(rt.high)
			{
				rm_pg = rt.high_pg;
				remove = _st_remove_key(rm_pg,rt.high,rt.high_prev);
			}
			else
			{
				waste = rt.sub->length - rt.diff;
				rt.sub->length = (rt.diff > 0)? rt.diff : 1;
				rm_pg = rt.pg;
			}
		}
	}
	else
	{
		waste = rt.sub->length - rt.diff;
		rt.sub->length = (rt.diff > 0)? rt.diff : 1;
		rm_pg = rt.pg;

		if(rt.sub->sub)
		{
			key* nxt = GOOFF(rt.pg,rt.sub->sub);
			rt.prev = 0;
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
	}

	if(rm_pg->page_adr)
	{
		rm_pg->pg.waste += waste >> 3;
		_tk_remove_tree(t,rm_pg,remove);
	}

	return 0;
}
