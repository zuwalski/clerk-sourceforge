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
#include <time.h>

/*
	IMPLEMENTS A CLERK-CLIENT BASED ON THE STANDARD TERMINAL
	SORT OF A READ-EVAL-PRINT-LOOP (REPL)
*/

#include "cle_input.h"

void unimplm()
{
	exit(-1);
}

uint page_size = 0;	// TEST
uint resize_count = 0;	// TEST
uint overflow_size = 0;	// TEST

// terminal output-functions
static int _def_start(void* d) 
{
	puts("start:");
	return 0;
}

static int _def_end(void* d,cdat code, uint len)
{
	if(len > 0)
		printf("\nend (%.*s):\n",len,code);
	else
		puts("end():");
	return 0;
}

static int _def_pop(void* d)
{
	puts("\n}");
	return 0;
}

static int _def_push(void* d)
{
	puts("\n{");
	return 0;
}

static int _def_data(void* d,cdat data, uint len)
{
	printf("%.*s",len,data);
	return 0;
}

static int _def_next(void* d)
{
	puts("\nnext:");
	return 0;
}

static cle_output _default_output = {_def_start,_def_end,_def_pop,_def_push,_def_data,_def_next};

static int trim(char* str)
{
	int l;
	if(str == 0)
		return 0;

	for(l = 0; str[l] != 0; l++)
	{
		if(str[l] == '\r' || str[l] == '\n')
		{
			str[l] = 0;
			break;
		}
	}

	return l;
}

int main(int argc, char *argv[])
{
	int i;
	// setup sys-handlers
	app_setup();
	typ_setup();
	cmp_setup();
	tst_setup();

	for(i = 1; i < argc; i++)
	{
		cle_input inpt;
		_ipt* ipt;
		FILE* testfile;

		// get application-name
		inpt.app_len = 0;
		inpt.appid = 0;
		// TESTING only internal handlers here...

		testfile = fopen(argv[i],"r");

		if(testfile)
		{
			char* str;
			char linebuffer[256];

			do
			{
				str = fgets(linebuffer,sizeof(linebuffer),testfile);
				if(str)
				{
					// get event-name 1.line
					inpt.evnt_len = trim(str);
					inpt.eventid = str;

					ipt = cle_start(&inpt,&_default_output,0);

					while(1)
					{
						str = fgets(linebuffer,sizeof(linebuffer),testfile);

						if(!str)
							break;
						
						if(str[0] == '@')
							cle_next(ipt);
						else if(str[0] == '!')
						{
							cle_next(ipt);
							break;
						}
						else 
						{
							int len = strlen(str);
							if(len > 0)
								cle_data(ipt,str,len);
						}
					}

					cle_end(ipt,0,0);
				}
			}
			while(str && str[0] == '!');

			fclose(testfile);
		}
		else
			printf("failed to open %s\n",argv[i]);
	}

	system("PAUSE");	
	return 0;
}
