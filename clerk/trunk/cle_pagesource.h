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
#ifndef __CLE_PAGESOURCE_H__
#define __CLE_PAGESOURCE_H__

typedef void* cle_pageid;
typedef void* cle_psrc_data;

typedef struct page
{
	cle_pageid id;
	unsigned short size;
	unsigned short used;
	unsigned short waste;
	char data[];
} page;

typedef struct cle_pagesource
{
	page* (*root_page)(cle_psrc_data);
	page* (*new_page)(cle_psrc_data);
	page* (*read_page)(cle_psrc_data, cle_pageid);
	int (*write_page)(cle_psrc_data, cle_pageid, page*);
	int (*remove_page)(cle_psrc_data, cle_pageid);
	int (*unref_page)(cle_psrc_data, cle_pageid);
	int (*ref_page)(cle_psrc_data, cle_pageid);
	int (*page_error)(cle_psrc_data);
}
cle_pagesource;

#endif
