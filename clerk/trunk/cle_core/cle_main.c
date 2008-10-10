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

/*
	dummy main
*/

#include <stdio.h>
#include <stdlib.h>
#include "cle_clerk.h"
#include "cle_input.h"

void unimplm()
{
	puts("failed in unimpl in test_main.c");
	exit(-1);
}

uint page_size;
uint resize_count;
uint overflow_size;

cle_syshandler _runtime_handler;

int main(int argc, char* argv[])
{
	puts("DUMMY MAIN - THIS IS A LIBRARY. LINK TO A HOST APPLICATION AND RUN THAT.");
	exit(-1);
	return 0;
}