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

void cle_panic(task* t)
{
	puts("failed in cle_panic in clerkcli.c");
	exit(-1);
}

void cle_notify_start(event_handler* handler)
{
	while(handler != 0)
	{
		handler->thehandler->input.start(handler);
		handler = handler->next;
	}
}

void cle_notify_next(event_handler* handler)
{
	while(handler != 0)
	{
		handler->thehandler->input.next(handler);
		handler = handler->next;
	}
}

void cle_notify_end(event_handler* handler, cdat msg, uint msglength)
{
	while(handler != 0)
	{
		handler->thehandler->input.end(handler,msg,msglength);
		handler = handler->next;
	}
}

// defs
static void _start(void* v)
{
}
static void _next(void* v)
{
	printf("\n");
}
static void _end(void* v,cdat c,uint u)
{
	if(u > 0)
	{
		*((int*)v) = 1;
		printf(" [end: %.*s]\n",u,c);
	}
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
	//uint i;
	//for(i = 0; i < u; i++)
	//{
	//	printf("%c",c[i]);
	//}
	printf("%.*s",u,c);
	return 0;
}
static void _submit(void* v,task* t,st_ptr* st)
{
	printf(" [submit] ");
}

static cle_pipe _pipe_stdout = {_start,_next,_end,_pop,_push,_data,_submit};

static int _print_usage(const char* toolname)
{
	printf("%s [-f<dbfilename>] [-e<event>] [params*]|-F [input-files*]|...\n",toolname);
	puts("License GPL 3.0");
	puts("(c) Lars Szuwalski, 2001-2010");
	return -1;
}

static int _parse_element(char** begin, char* from)
{
	while(1)
		switch(*from++)
		{
		case 0:
		case '#':
		case '\r':
		case '\n':
			return 0;
		case ' ':
		case '\t':
			break;
		default:
			*begin = from - 1;

			while(1)
				switch(*from++)
				{
				case 0:
				case '#':
				case '\r':
				case '\n':
				case ' ':
				case '\t':
					return (int)(from - *begin - 1);
				}
		}
}

static char* _read_block(_ipt* ipt, FILE* sfile, char* buffer, int buffer_length, char* begin)
{
	char* from = begin = begin + 2;

	while(1)
	{
		while(begin[0] != 0 && !(begin[0] == '<' && begin[1] == '!'))
			begin++;

		cle_data(ipt,from,begin - from);

		if(begin[0] != 0)
			return begin + 2;

		if(fgets(buffer,buffer_length,sfile) == 0)
			return 0;

		from = begin = buffer;
	}
}

static int _read_file(_ipt* ipt, char* filename)
{
	FILE* pfile = fopen(filename,"r");
	if(pfile == 0)
		return -1;

	while(feof(pfile) == 0 && ferror(pfile) == 0)
	{
		char buffer[1024];
		size_t rd = fread(buffer,1,sizeof(buffer),pfile);

		if(rd > 0)
			cle_data(ipt,buffer,(uint)rd);
	}

	if(ferror(pfile) != 0)
		return -1;

	return fclose(pfile);
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

	st_ptr user,userroles;
	user.pg = 0;
	userroles.pg = 0;

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
		st_ptr eventname;
		st_empty(t,&eventname);
		st_insert(t,&eventname,db_event,(uint)strlen(db_event));
		
		ipt = cle_start(t,config_root,eventname,user,userroles,&_pipe_stdout,0);

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
			char buffer[1024];
			failed = 0;
			ipt = 0;

			while(failed == 0 && fgets(buffer,sizeof(buffer),sfile))
			{
				st_ptr eventname;
				char* begin;
				int length;
				
				// event-name
				length = _parse_element(&begin,buffer);
				if(length == 0) continue;

				if(ipt != 0)
					cle_end(ipt,0,0);

				//printf("event: %.*s\n",length,begin);

				st_empty(t,&eventname);
				st_insert(t,&eventname,begin,length);

				// start event
				ipt = cle_start(t,config_root,eventname,user,userroles,&_pipe_stdout,&failed);
				if(ipt == 0) {failed = 1; break;}

				// parameters
				do
				{
					length = _parse_element(&begin,begin + length);
					if(length == 0) break;

					// !> ... <!
					if(begin[0] == '!' && begin[1] == '>')
					{
						begin = _read_block(ipt,sfile,buffer,sizeof(buffer),begin);
						if(begin == 0)
							break;

						length = 0;
					}
					// [filename]
					else if(begin[0] == '[' && begin[length - 1] == ']')
					{
						begin[length - 1] = '\0'; 
						failed = _read_file(ipt,begin + 1);
					}
					// simple param
					else
						cle_data(ipt,begin,length);

					cle_next(ipt);
				}
				while(failed == 0);
			}

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
