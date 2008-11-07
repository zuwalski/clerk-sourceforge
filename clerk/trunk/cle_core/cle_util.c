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

#include <stdio.h>
#include <io.h>
#include <fcntl.h>
#include <share.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef CLERK_SINGLE_THREAD

/*
	simple mem pager
*/
struct _mem_psrc_data
{
	page* root;
	int pagecount;
};

page _dummy_root = {ROOT_ID,MEM_PAGE_SIZE,sizeof(page) + 10,0,0,0,1,0,0,0};

static cle_pageid mem_new_page(cle_psrc_data pd, page* data)
{
	struct _mem_psrc_data* md = (struct _mem_psrc_data*)pd;
	page* pg = malloc(data->size);
	if(pg == 0)
		return 0;
	memcpy(pg,data,data->used);
	pg->id = pg;
	md->pagecount++;
	return (cle_pageid)pg;
}

static page* mem_read_page(cle_psrc_data pd, cle_pageid id)
{
	if(id == ROOT_ID)
	{
		struct _mem_psrc_data* md = (struct _mem_psrc_data*)pd;
		return (md->root == 0)? &_dummy_root : md->root;
	}

	return (page*)id;
}

static void mem_write_page(cle_psrc_data pd, cle_pageid id, page* pg)
{
	if(id == ROOT_ID)
	{
		struct _mem_psrc_data* md = (struct _mem_psrc_data*)pd;
		if(md->root == 0)
		{
			md->root = (page*)mem_new_page(pd,pg);
			md->root->id = ROOT_ID;
			return;
		}

		id = md->root;
	}

	memcpy(id,pg,pg->used);
}

static void mem_remove_page(cle_psrc_data pd, cle_pageid id)
{
	struct _mem_psrc_data* md = (struct _mem_psrc_data*)pd;
	if(id != ROOT_ID)
	{
		free(id);
		md->pagecount--;
	}
	else
	{
		free(md->root);
		md->root = 0;
		md->pagecount = 0;
	}	
}

static void mem_unref_page(cle_psrc_data pd, page* pg)
{}

static int mem_pager_simple(cle_psrc_data pd)
{
	return 0;
}

cle_pagesource util_memory_pager = 
{
	mem_new_page,mem_read_page,mem_write_page,mem_remove_page,mem_unref_page,mem_pager_simple,mem_pager_simple,mem_pager_simple,mem_pager_simple
};

cle_psrc_data util_create_mempager()
{
	struct _mem_psrc_data* md = (struct _mem_psrc_data*)malloc(sizeof(struct _mem_psrc_data));
	md->root = 0;
	md->pagecount = 0;
	return (cle_psrc_data)md;
}

int mempager_get_pagecount(cle_psrc_data pd)
{
	struct _mem_psrc_data* md = (struct _mem_psrc_data*)pd;
	return md->pagecount;
}

/*

	simple file pager

	Not really implemented - just showcase and timings
*/

struct _file_pager_header
{
	unsigned long magic;
	unsigned long pagesize;
	unsigned long pagecount;
	unsigned long firstfreepage;
	unsigned long version;
};

struct _file_psrc_data
{
	int fh;
	struct _file_pager_header header;
};

static cle_pageid file_new_page(cle_psrc_data pd, page* pg)
{
	struct _file_psrc_data* fd = (struct _file_psrc_data*)pd;

	fd->header.pagecount += 1;
	pg->id = (cle_pageid)(fd->header.pagecount * fd->header.pagesize + sizeof(struct _file_pager_header));

	if(_lseek(fd->fh,(long)pg->id,SEEK_SET) == -1)
		return 0;

	if(_write(fd->fh,pg,pg->used) == -1)
		return 0;

	return pg->id;
}

static page* file_read_page(cle_psrc_data pd, cle_pageid pid)
{
	struct _file_psrc_data* fd = (struct _file_psrc_data*)pd;
	page* pg;

	if(pid == ROOT_ID)
	{
		if(fd->header.pagecount == 0)
			return &_dummy_root;

		pid = (cle_pageid)sizeof(struct _file_pager_header);
	}

	if(_lseek(fd->fh,(long)pid,SEEK_SET) == -1)
		return 0;

	pg = (page*)malloc(fd->header.pagesize);

	if(_read(fd->fh,pg,fd->header.pagesize) == -1)
	{
		free(pg);
		return 0;
	}

	return pg;
}

static void file_write_page(cle_psrc_data pd, cle_pageid pid, page* pg)
{
	struct _file_psrc_data* fd = (struct _file_psrc_data*)pd;

	if(pid == ROOT_ID)
	{
		pg->id = ROOT_ID;
		pid = (cle_pageid)sizeof(struct _file_pager_header);
	}

	if(_lseek(fd->fh,(long)pid,SEEK_SET) == -1)
		return;

	_write(fd->fh,pg,pg->used);
}

static void file_remove_page(cle_psrc_data pd, cle_pageid pid)
{
	struct _file_psrc_data* fd = (struct _file_psrc_data*)pd;
}

static void file_unref_page(cle_psrc_data pd, page* pg)
{
	struct _file_psrc_data* fd = (struct _file_psrc_data*)pd;

	if(pg != &_dummy_root)
		free(pg);
}

static int file_pager_error(cle_psrc_data pd)
{
	struct _file_psrc_data* fd = (struct _file_psrc_data*)pd;
	return 0;
}

// not really implemented (just showcase)
static int file_pager_commit(cle_psrc_data pd)
{
	struct _file_psrc_data* fd = (struct _file_psrc_data*)pd;

	fd->header.version += 1;

	if(_lseek(fd->fh,0,SEEK_SET) == -1)
		return -1;

	if(_write(fd->fh,&fd->header,sizeof(struct _file_pager_header)) == -1)
		return -1;

	return _commit(fd->fh);
}

// not really implemented
static int file_pager_rollback(cle_psrc_data pd)
{
	return 0;
}

static int file_pager_close(cle_psrc_data pd)
{
	struct _file_psrc_data* fd = (struct _file_psrc_data*)pd;
	_close(fd->fh);
	free(fd);
	return 0;
}

cle_pagesource util_file_pager = 
{
	file_new_page,file_read_page,file_write_page,file_remove_page,file_unref_page,file_pager_error,file_pager_commit,file_pager_rollback,file_pager_close
};


cle_psrc_data util_create_filepager(const char* filename)
{
	struct _file_psrc_data* fd = (struct _file_psrc_data*)malloc(sizeof(struct _file_psrc_data));

	errno_t err = _sopen_s(&fd->fh,filename,_O_BINARY|_O_CREAT|_O_RANDOM|_O_RDWR, _SH_DENYWR, _S_IREAD|_S_IWRITE);

	if(err != 0)
	{
		free(fd);
		return 0;
	}

	// read header 
	if(_read(fd->fh,&fd->header,sizeof(struct _file_pager_header)) != sizeof(struct _file_pager_header))
	{
		// create
		fd->header.magic = PAGER_MAGIC;
		fd->header.pagesize = MEM_PAGE_SIZE;
		fd->header.firstfreepage = 0;
		fd->header.pagecount = 0;
		fd->header.version = 0;

		if(_write(fd->fh,&fd->header,sizeof(struct _file_pager_header)) == -1)
		{
			file_pager_close(fd);
			return 0;
		}
	}
	else
	{
		// open
		if(fd->header.magic != PAGER_MAGIC)	// not a dbfile
		{
			file_pager_close(fd);
			return 0;
		}
	}

	return fd;
}

int filepager_get_pagecount(cle_psrc_data pd)
{
	struct _file_psrc_data* fd = (struct _file_psrc_data*)pd;
	return fd->header.pagecount;
}

#endif	// #ifdef CLERK_SINGLE_THREAD

