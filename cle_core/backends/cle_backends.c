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
#include "cle_backends.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/*
 
 simple mem pager

 */
struct _mem_psrc_data {
	page* root;
	page* free;
	int pagecount;
};

//page _dummy_root = {ROOT_ID,MEM_PAGE_SIZE,sizeof(page) + 10,0,0,0,1,0,0,0};
struct _dummy_rt {
	page pg;
	short s[6];
} _dummy_root = { { &_dummy_root, 0, MEM_PAGE_SIZE, sizeof(page) + 10, 0 }, { 0, 1, 0, 0, 0, 0 } };

static cle_pageid mem_new_page(cle_psrc_data pd, page* data) {
	struct _mem_psrc_data* md = (struct _mem_psrc_data*) pd;
	page* pg = md->free;

	if (pg == 0) {
		pg = malloc(data->size);
		if (pg == 0)
			return 0;
	} else
		md->free = pg->parent;

	if (data->used > data->size) {
		printf("not good\n");
	}

	memcpy(pg, data, data->used);
	pg->id = pg;
	pg->parent = 0;
	md->pagecount++;
	return (cle_pageid) pg;
}

static page* mem_read_page(cle_psrc_data pd, cle_pageid id) {
	return (page*) id;
}

static page* mem_root_page(cle_psrc_data pd) {
	struct _mem_psrc_data* md = (struct _mem_psrc_data*) pd;
	return md->root;
}

static void mem_write_page(cle_psrc_data pd, cle_pageid id, page* pg) {
	page* npg;
	if (pg->used > pg->size) {
		printf("not good");
	}
	if (id == &_dummy_root) {
		struct _mem_psrc_data* md = (struct _mem_psrc_data*) pd;

		md->root = (page*) mem_new_page(pd, pg);
		return;
	} else {
		npg = (page*) id;
		memcpy(npg, pg, pg->used);
	}
	npg->id = id;
	npg->parent = 0;
}

static void mem_remove_page(cle_psrc_data pd, cle_pageid id) {
	struct _mem_psrc_data* md = (struct _mem_psrc_data*) pd;
	page* pg;

	if (id != &_dummy_root) {
		pg = (page*) id;
	} else {
		if (md->root == &_dummy_root)
			return;

		md->root = &_dummy_root;
		pg = md->root;
	}

	pg->parent = md->free;
	md->free = pg;
	md->pagecount--;
}

static void mem_unref_page(cle_psrc_data pd, page* pg) {
}

static int mem_pager_simple(cle_psrc_data pd) {
	return 0;
}

static cle_psrc_data mem_pager_clone(cle_psrc_data dat) {
	return dat;
}

cle_pagesource util_memory_pager = { mem_new_page, mem_read_page, mem_root_page, mem_write_page, mem_remove_page,
		mem_unref_page, mem_pager_simple, mem_pager_simple, mem_pager_simple, mem_pager_simple, mem_pager_clone };

cle_psrc_data util_create_mempager() {
	struct _mem_psrc_data* md = (struct _mem_psrc_data*) malloc(sizeof(struct _mem_psrc_data));
	md->root = &_dummy_root;
	md->free = 0;
	md->pagecount = 0;
	return (cle_psrc_data) md;
}

int mempager_get_pagecount(cle_psrc_data pd) {
	struct _mem_psrc_data* md = (struct _mem_psrc_data*) pd;
	return md->pagecount;
}
