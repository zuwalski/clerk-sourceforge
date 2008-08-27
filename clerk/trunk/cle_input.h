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
#ifndef __CLE_INPUT_H__
#define __CLE_INPUT_H__

#include "cle_clerk.h"
#include "cle_pagesource.h"

/*
*	The main input-interface to the running system
*	Commands and external events are "pumped" in through this set of functions
*/

/* initializer: call this once(!) before anything else */
void cle_initialize_system();

/* event input functions */
typedef struct _ipt_internal _ipt;

_ipt* cle_start(cdat eventid, uint event_len, cdat userid, uint userid_len, 
				cle_output* response, void* responsedata, 
					cle_pagesource* app_source, cle_psrc_data app_source_data, 
						cle_pagesource* session_source, cle_psrc_data session_source_data);

int cle_data(_ipt* inpt, cdat data, uint length);
int cle_pop(_ipt* inpt);
int cle_push(_ipt* inpt);
int cle_next(_ipt* inpt);
int cle_end(_ipt* inpt, cdat code, uint length);

// system-event-handlers

typedef struct sys_handler_data
{
	cle_output* response;
	void* respdata;
	void* data;
	task* instance_tk;
	st_ptr instance;
	uint next_call;
}
sys_handler_data;

/* system extension-handlers */
typedef struct cle_syshandler
{
	int (*do_setup)(sys_handler_data*);
	int (*do_next)(sys_handler_data*,st_ptr,uint);
	int (*do_end)(sys_handler_data*,cdat,uint);
}
cle_syshandler;

/* single threaded calls only */
int cle_add_sys_handler(cdat eventmask, uint mask_length, cle_syshandler* handler);

#endif
