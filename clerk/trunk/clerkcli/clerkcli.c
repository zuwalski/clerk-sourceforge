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
#include <string.h>
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
	printf("}");
}
static void _push(void* v)
{
	printf("}");
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

static int _print_usage(const char* toolname)
{
	printf("%s [-f<dbfilename>] [-e<event>] [params*]|-F [input-files*]|...\n",toolname);
	puts("License GPL 3.0");
	puts("(c) Lars Szuwalski, 2001-2009");
	return -1;
}

int main(int argc, char* argv[])
{
	char* db_file = "cle.cle";
	char* db_event = "cli.start";
	char* script = 0;

	cle_pagesource* psource = &util_file_pager;
	cle_psrc_data pdata;
	st_ptr config_root;
	_ipt* ipt;
	task* t;
	int i,nodev = 0,noadm = 0,failed = 1;

	// options
	for(i = 1; i < argc; i++)
	{
		if(argv[i][0] == '-')
		{
			switch(argv[i][1])
			{
			case 'f':
				if(i + 1 < argc)
					db_file = argv[++i];
				else return _print_usage(argv[0]);
				break;
			case 'e':
				if(i + 1 < argc)
					db_event = argv[++i];
				else return _print_usage(argv[0]);
				break;
			case 'd':
				nodev = 1;
				break;
			case 'a':
				noadm = 1;
				break;
			case 'h':
			case '?':
				_print_usage(argv[0]);
				return 0;
			case 's':
				if(i + 1 < argc)
					script = argv[++i];
				else return _print_usage(argv[0]);
				break;
			default:
				return _print_usage(argv[0]);
			}
		}
		else
			break;
	}

	// open db-file
	pdata = util_create_filepager(db_file);
	if(pdata == 0)
	{
		fprintf(stderr,"failed to open file: %s\n",db_file);
		return -1;
	}

	//  new task
	t = tk_create_task(psource,pdata);

	st_empty(t,&config_root);

	if(nodev == 0)
		dev_register_handlers(t,&config_root);

	if(noadm == 0)
		admin_register_handlers(t,&config_root);

	if(script == 0)
	{
		ipt = cle_start(config_root,db_event,(uint)strlen(db_event) + 1, 0, 0, 0,&_pipe_stdout,0,t);

		if(ipt != 0)
		{
			failed = 0;
			// event-stream
			for(; failed == 0 && i < argc; i++)
			{
				cle_data(ipt,argv[i],(uint)strlen(argv[i]));
				cle_next(ipt);
			}

			cle_end(ipt,0,0);
		}
	}
	else
	{
		FILE* sfile = fopen(script,"r");
		if(sfile == 0)
			fprintf(stderr,"failed to open script: %s\n",script);
		else
		{
			int state = 0, data = 0;
			char buffer[1024];
			failed = 0;
			/*
			while(feof(sfile) == 0 && ferror(sfile) == 0)
			{
				size_t rd = fread(buffer,1,sizeof(buffer),sfile);

				for(i = 0; i < rd; i++)
				{
					switch(buffer[i])
					{
					case '<':
					case '>':
					case '!':
					case ' ':
					case '\t':
						if(state == 1)
						break;
					case '\n':
					case '\r':
					default:
					}
				}
			}
			*/


			while(fgets(buffer,sizeof(buffer),sfile))
			{
				if(strcmp(buffer,"!>\n") == 0)
				{
					if(state & 6)
						state = 1;
					else
					{
						failed = 1;
						break;
					}
				}
				else if(strcmp(buffer,"<!\n") == 0)
				{
					if(state != 1)
					{
						failed = 1;
						break;
					}
					cle_next(ipt);
					state = 2;
				}
				else
				{
					if(state == 1)
					{
						cle_data(ipt,buffer,strnlen(buffer,sizeof(buffer) - 1) + 1);
					}
					else
					{
						if(state != 0)
							cle_end(ipt,0,0);

						ipt = cle_start(config_root,buffer,(uint)strnlen(buffer,sizeof(buffer) - 1) + 1, 0, 0, 0,&_pipe_stdout,0,t);
						if(ipt == 0)
						{
							failed = 1;
							break;
						}
						state = 4;
					}
				}
			}

			if(state & 6 == 0)
				failed = 1;

			cle_end(ipt,0,0);

			fclose(sfile);
		}
	}

	if(failed == 0)
	{
		failed = tk_commit_task(t);
		if(failed != 0)
			fprintf(stderr,"failed to commit event: %s\n",db_event);
	}
	else
	{
		tk_drop_task(t);
		fprintf(stderr,"failed to fire event: %s\n",db_event);
	}

	return failed;
}
