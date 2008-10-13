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

#define DUMMY_ID ((cle_pageid)1)

page _dummy_root = {DUMMY_ID,MEM_PAGE_SIZE,sizeof(page) + 10,0,0,0,1,0,0,0};

static page* root_page(cle_psrc_data pd)
{
	struct _mem_psrc_data* md = (struct _mem_psrc_data*)pd;
	return (md->root == 0)? &_dummy_root : md->root;
}

static page* new_page(cle_psrc_data pd)
{
	page* pg = malloc(MEM_PAGE_SIZE);
	pg->id = pg;
	pg->size = MEM_PAGE_SIZE;
	pg->used = sizeof(page);
	pg->waste = 0;
	return pg;
}

static page* read_page(cle_psrc_data pd, cle_pageid id)
{
	return (id == 0)? &_dummy_root : (page*)id;
}

static int write_page(cle_psrc_data pd, cle_pageid id, page* pg)
{
	if(id == DUMMY_ID)
	{
		struct _mem_psrc_data* md = (struct _mem_psrc_data*)pd;
		md->root = id = new_page(pd);
	}

	memcpy(id,pg,pg->used);
	return 0;
}

static int remove_page(cle_psrc_data pd, cle_pageid id)
{
	if(id != DUMMY_ID)
		free(id);
	return 0;
}

static int unref_page(cle_psrc_data pd, cle_pageid id)
{
	return 0;
}

static int page_error(cle_psrc_data pd)
{
	return 0;
}

cle_pagesource util_memory_pager = 
{
	root_page,new_page,read_page,write_page,remove_page,unref_page,unref_page,page_error
};

cle_psrc_data util_create_mempager()
{
	struct _mem_psrc_data* md = (struct _mem_psrc_data*)malloc(sizeof(struct _mem_psrc_data));
	md->root = 0;
	return (cle_psrc_data)md;
}
