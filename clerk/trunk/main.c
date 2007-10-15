/* 
   Copyright 2005-2007 Lars Szuwalski

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
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
