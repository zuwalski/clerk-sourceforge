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
#include "cle_input.h"

/*
** SYSTEM-HANDLERS FOR INPUT-SYSTEM
**
** type-system has these events:

	- new type (nt)
	- move type (mt)
	- delete type (dt)
	- list types (lt)

	- move field (mf)
	- delete field (df)
*/

static int _do_next(sys_handler_data* hd, st_ptr pt, uint depth)
{
	return 0;
}

static cle_syshandler handle_nt = {"nt",2,0,_do_next,0,0};
static cle_syshandler handle_mt = {"mt",2,0,_do_next,0,0};
static cle_syshandler handle_dt = {"dt",2,0,_do_next,0,0};
static cle_syshandler handle_lt = {"lt",2,0,_do_next,0,0};
static cle_syshandler handle_mf = {"mf",2,0,_do_next,0,0};
static cle_syshandler handle_df = {"df",2,0,_do_next,0,0};

void typ_setup()
{
	cle_add_sys_handler(&handle_nt);
	cle_add_sys_handler(&handle_mt);
	cle_add_sys_handler(&handle_dt);
	cle_add_sys_handler(&handle_lt);
	cle_add_sys_handler(&handle_mf);
	cle_add_sys_handler(&handle_df);
}
