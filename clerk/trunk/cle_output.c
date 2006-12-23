/* 
   Copyright 2005-2006 Lars Szuwalski

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
#include "cle_output.h"
#include <stdio.h>

/*
 default implementation of the output-interface
 Just output to std-out

 typedef int (*event_start)(task*);
typedef int (*event_end)(task*);
typedef int (*event_pop)(task*);
typedef int (*event_push)(task*);
typedef int (*event_name)(task*,cdat,uint);
typedef int (*event_data)(task*,cdat,uint);
typedef int (*event_next)(task*);

typedef struct cle_output
{
	event_start start;
	event_end   end;
	event_pop   pop;
	event_push  push;
	event_name  name;
	event_data  data;
	event_next  next;
} cle_output;
*/

int _std_out_start(task* t)
{
	puts("Output start:");
	return 0;
}

int _std_out_end(task* t)
{
	puts("Output end:");
	return 0;
}

int _std_out_pop(task* t)
{
	puts("}");
	return 0;
}

int _std_out_push(task* t)
{
	puts("{");
	return 0;
}

int _std_out_name(task* t, cdat data, uint length)
{
	puts(data);
	return 0;
}

int _std_out_data(task* t, cdat data, uint length)
{
	printf("%s",data);
	return 0;
}

int _std_out_next(task* t)
{
	puts(" , ");
	return 0;
}

uint cle_out_initstdout(cle_output* out)
{
	out->data  = _std_out_data;
	out->end   = _std_out_end;
	out->name  = _std_out_name;
	out->next  = _std_out_next;
	out->pop   = _std_out_pop;
	out->push  = _std_out_push;
	out->start = _std_out_start;
	return 0;
}
