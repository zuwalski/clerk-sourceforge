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
	puts("start:\n");
	return 0;
}

static int _def_end(void* d,cdat code, uint len)
{
	printf("\nend (%s)\n",code);
	return 0;
}

static int _def_pop(void* d)
{
	puts("}");
	return 0;
}

static int _def_push(void* d)
{
	puts("{");
	return 0;
}

static int _def_data(void* d,cdat data, uint len)
{
	printf("%s",data);
	return 0;
}

static int _def_next(void* d)
{
	puts("next:\n");
	return 0;
}

static cle_output _default_output = {_def_start,_def_end,_def_pop,_def_push,_def_data,_def_next};

int main(int argc, char *argv[])
{
	// setup sys-handlers
	app_setup();
	typ_setup();
	cmp_setup();

	while(1)
	{
		cle_input inpt;
		_ipt* ipt;
		char linebuffer[256];

		// get application-name
		inpt.app_len = 0;
		inpt.appid = 0;
		// TESTING only internal handlers here...

		// get event-name
		inpt.evnt_len = 0;
		inpt.eventid = 0;
		
		ipt = cle_start(&inpt,&_default_output,0);

		fgets(linebuffer,sizeof(linebuffer),stdin);

		cle_end(ipt,0,0);
	}

	system("PAUSE");	
	return 0;
}
