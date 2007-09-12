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
#ifndef __CLE_INPUT_H__
#define __CLE_INPUT_H__

#include "cle_clerk.h"

/* event-description struct */
typedef struct cle_input
{
	void* internaldata;
	cdat instance;
	uint inst_len;
	cdat appid;
	uint app_len;
	cdat eventid;
	uint evnt_len;
	cdat sessionid;
	uint ses_len;
}
cle_input;

/* event input functions */
typedef struct _ipt_internal _ipt;

int cle_data(_ipt* inpt, cdat data, uint length);
int cle_pop(_ipt* inpt);
int cle_push(_ipt* inpt);
int cle_end(_ipt* inpt, cdat code, uint length);
_ipt* cle_start(cle_input* inpt, cle_output* response, void* responsedata);
int cle_next(_ipt* inpt);

typedef struct sys_handler_data
{
	cle_output* response;
	void* respdata;
	void* data;
	task* t;
	st_ptr instance;
	uint next_call;
}
sys_handler_data;

/* system extension-handlers */
typedef struct cle_syshandler
{
	const char* handlerid;
	uint id_length;

	int (*do_setup)(sys_handler_data*);
	int (*do_next)(sys_handler_data*,st_ptr,uint);
	int (*do_end)(sys_handler_data*,cdat,uint);

	struct cle_syshandler* next;
}
cle_syshandler;

int cle_add_sys_handler(cle_syshandler* handler);

#endif
