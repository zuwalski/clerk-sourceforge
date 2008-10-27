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
#include "cle_util.h"
#include <stdlib.h>

struct _mem_psrc_data
{
	page* root;
};

page _dummy_root = {ROOT_ID,MEM_PAGE_SIZE,sizeof(page) + 10,0,0,0,1,0,0,0};

static cle_pageid new_page(cle_psrc_data pd, page* data)
{
	page* pg = malloc(data->size);
	if(pg == 0)
		return 0;
	memcpy(pg,data,data->used);
	pg->id = pg;
	return (cle_pageid)pg;
}

static page* read_page(cle_psrc_data pd, cle_pageid id)
{
	return (id == ROOT_ID)? &_dummy_root : (page*)id;
}

static void write_page(cle_psrc_data pd, cle_pageid id, page* pg)
{
	if(id == ROOT_ID)
	{
		struct _mem_psrc_data* md = (struct _mem_psrc_data*)pd;
		if(md->root == 0)
		{
			md->root = (page*)new_page(pd,pg);
			return;
		}

		id = md->root;
	}

	memcpy(id,pg,pg->used);
}

static void remove_page(cle_psrc_data pd, cle_pageid id)
{
	if(id != ROOT_ID)
		free(id);
}

static void unref_page(cle_psrc_data pd, cle_pageid id)
{}

static int pager_simple(cle_psrc_data pd)
{
	return 0;
}

cle_pagesource util_memory_pager = 
{
	new_page,read_page,write_page,remove_page,unref_page,pager_simple,pager_simple,pager_simple
};

cle_psrc_data util_create_mempager()
{
	struct _mem_psrc_data* md = (struct _mem_psrc_data*)malloc(sizeof(struct _mem_psrc_data));
	md->root = 0;
	return (cle_psrc_data)md;
}
