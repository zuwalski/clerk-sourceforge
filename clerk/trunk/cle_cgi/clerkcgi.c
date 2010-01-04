/* 
Clerk application and storage engine.
Copyright (C) 2009  Lars Szuwalski

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

////// master fail
void unimplm()
{
	puts("failed in unimpl in clerkcgi.c");
	exit(-1);
}

/////// schedule
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

///// query cgi-headers
static const char _cgi_headers_name[] = "cgi\0header";
static cle_syshandler _cgi_headers;

///// cgi main
int main(int argc, char* argv[])
{
	//getenv("var");
	return 0;
}
