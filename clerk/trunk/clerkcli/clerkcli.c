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

#include <stdio.h>
#include <stdlib.h>
#include "../cle_core/cle_clerk.h"
#include "../cle_core/backends/cle_backends.h"
#include "../cle_core/cle_stream.h"

void unimplm()
{
	puts("failed in unimpl in clerkcli.c");
	exit(-1);
}

// defs
static void _start(void* v)
{
}
static void _next(void* v)
{
	puts("");
}
static void _end(void* v,cdat c,uint u)
{
	if(u > 0)
		printf(" [end: %.*s]\n",u,c);
}
static void _pop(void* v)
{
	printf(" << ");
}
static void _push(void* v)
{
	printf(" >> ");
}
static uint _data(void* v,cdat c,uint u)
{
	printf("%.*s",u,c);
	return 0;
}
static void _submit(void* v,st_ptr* st)
{
	printf(" [submit] ");
}

static cle_pipe _pipe_stdout = {_start,_next,_end,_pop,_push,_data,_submit};

static cle_pagesource* psource;
static cle_psrc_data pdata;

static char* db_file = "cle.db";
static char* db_event = "main.start";

static int _open_db(const char* dbfile, const char* eventname)
{
	task* t;
	st_ptr config_root;
	_ipt* ipt;

	cle_pagesource* psource = &util_file_pager;
	cle_psrc_data pdata = util_create_filepager(dbfile);

	if(pdata == 0)
		return -1;

	//  new task
	t = tk_create_task(psource,pdata);

	st_empty(t,&config_root);

	dev_register_handlers(t,&config_root);
	admin_register_handlers(t,&config_root);

	ipt = cle_start(config_root,test_events[i],10, 0, 0, 0,&_pipe_stdout,0,t);

	tk_drop_task(t);
	tk_commit_task(t);

	return (t == 0);
}

static void _print_usage(const char* toolname)
{
	printf("%s [-Fdbfilename] [event]\n",toolname);
	puts("(c) Lars Szuwalski, 2001-2009");
}

static void _parse_args(int argc, char* argv[])
{
	int i;
	for(i = 1; i < argc; i++)
	{
		if(argv[i][0] == '-')	// option
		{
			switch(argv[i][1])
			{
			case 'f':
			case 'F':
				break;
			case 'e':
			case 'E':
				break;
			default:
				// wtf?
			}
		}
		else	// event
		{}
	}
}

int main(int argc, char* argv[])
{
	switch(argc)
	{
	case 0:
		_print_usage("cle");
		break;
	case 2:
		return _open_db("cle.db",argv[1]);
	case 3:
		return _open_db(argv[1],argv[2]);
	default:
		_print_usage(argv[0]);
	}
	return -1;
}