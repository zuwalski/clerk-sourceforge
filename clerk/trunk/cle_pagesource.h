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

typedef void* cle_pagedata;
typedef void* cle_pageid;
typedef void* cle_psrc_data;

typedef struct cle_pagesource
{
	cle_pagedata (*root_page)(cle_psrc_data);
	cle_pagedata (*new_page)(cle_psrc_data);
	cle_pagedata (*read_page)(cle_psrc_data, cle_pageid);
	cle_pagedata (*writable_page)(cle_psrc_data, cle_pagedata);
	int (*write_page)(cle_psrc_data, cle_pagedata);
	int (*release_page)(cle_psrc_data, cle_pageid);
}
cle_pagesource;

#endif
