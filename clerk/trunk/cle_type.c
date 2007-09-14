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

struct _parameters
{
	char* value[3];
	uint vallen[3];
};

/*
** SYSTEM-HANDLERS FOR INPUT-SYSTEM
**
** type-system has these events:

	- new type (nt) [typename] ?[extending-typename]
	- move type (mt) [typename] [new typename]
	- delete type (dt) [typename]
	- list types (lt)

	- move field (mf) [typename] [fieldname] [new fieldname]
	- delete field (df) [typename] [fieldname]
*/

static int _do_setup(sys_handler_data* hd)
{
	struct _parameters* param = (struct _parameters*)tk_malloc(sizeof(struct _parameters));
	hd->data = param;

	param->value[0] = 0;
	param->value[1] = 0;
	param->value[2] = 0;
	return 0;
}

static int _do_next(sys_handler_data* hd, st_ptr pt, uint depth)
{
	struct _parameters* param = (struct _parameters*)hd->data;
	uint length;

	if(hd->next_call > 2)
		return -1;

	param->value[hd->next_call]  = st_get_all(&pt,&param->vallen[hd->next_call]);
	return 0;
}

static void _do_clean(struct _parameters* param)
{
	tk_mfree(param->value[0]);
	tk_mfree(param->value[1]);
	tk_mfree(param->value[2]);
	tk_mfree(param);
}

// new type (end handler)
static int _do_nt(sys_handler_data* hd, cdat code, uint length)
{
	struct _parameters* param = (struct _parameters*)hd->data;
	int rcode = 0;

	if(hd->next_call == 0 || hd->next_call > 2)
		rcode = -1;
	else
	{
		st_ptr tps = hd->instance;
		// create type
		st_insert(hd->t,&tps,HEAD_TYPE,HEAD_SIZE);

		if(!st_insert(hd->t,&tps,param->value[0],param->vallen[0]))
			rcode = -2;	// already exsists
		// extends typename
		else if(hd->next_call == 2)
		{
			st_insert(hd->t,&tps,HEAD_EXTENDS,HEAD_SIZE);

			st_insert(hd->t,&tps,param->value[1],param->vallen[1]);
		}
	}

	_do_clean(param);
	return rcode;
}

// move type (end handler)
static int _do_mt(sys_handler_data* hd, cdat code, uint length)
{
	struct _parameters* param = (struct _parameters*)hd->data;

	_do_clean(param);
	return -1;			// NOT IMPL.
}

// move field (end handler)
static int _do_mf(sys_handler_data* hd, cdat code, uint length)
{
	struct _parameters* param = (struct _parameters*)hd->data;

	_do_clean(param);
	return -1;			// NOT IMPL.
}

// delete field (end handler)
static int _do_df(sys_handler_data* hd, cdat code, uint length)
{
	struct _parameters* param = (struct _parameters*)hd->data;
	int rcode = 0;

	if(hd->next_call == 2)
	{
		st_ptr tp = hd->instance;

		if(st_move(&tp,HEAD_TYPE,HEAD_SIZE))
			rcode = -2;
		else if(st_move(&tp,param->value[0],param->vallen[0]))
			rcode = -3;
		else
		{
			st_delete(hd->t,&tp,param->value[1],param->vallen[1]);
		}
	}
	else
		rcode = -1;

	_do_clean(param);
	return rcode;
}

// delete type
static int _do_dt(sys_handler_data* hd, st_ptr pt, uint depth)
{
	int rcode = 0;

	if(hd->next_call == 0 && depth == 0)
	{
		uint length;
		char* tpname = st_get_all(&pt,&length);

		if(length > 0)
		{
			st_ptr deltp = hd->instance;
			if(!st_move(&deltp,HEAD_TYPE,HEAD_SIZE))
				st_delete(hd->t,&deltp,tpname,length);
		}

		tk_mfree(tpname);
	}
	else rcode = -1;

	return rcode;
}

// list types (end-handler)
static int _do_lt(sys_handler_data* hd, cdat code, uint length)
{
	if(length == 0)	// no error
	{
		st_ptr apps = hd->instance;

		hd->response->start(hd->respdata);

		if(!st_move(&apps,HEAD_TYPE,HEAD_SIZE))
		{
			it_ptr it;
			it_create(&it,&apps);

			while(it_next(0,&it))
			{
				hd->response->data(hd->respdata,it.kdata,it.kused);
				hd->response->next(hd->respdata);
			}

			it_dispose(&it);
		}

		hd->response->end(hd->respdata,0,0);
	}

	return 0;
}

static cle_syshandler handle_nt = {"nt",2,_do_setup,_do_next,_do_nt,0};
static cle_syshandler handle_mt = {"mt",2,_do_setup,_do_next,_do_mt,0};
static cle_syshandler handle_dt = {"dt",2,0,_do_dt,0,0};
static cle_syshandler handle_lt = {"lt",2,0,0,_do_lt,0};
static cle_syshandler handle_mf = {"mf",2,_do_setup,_do_next,_do_mf,0};
static cle_syshandler handle_df = {"df",2,_do_setup,_do_next,_do_df,0};

void typ_setup()
{
	cle_add_sys_handler(&handle_nt);
	cle_add_sys_handler(&handle_mt);
	cle_add_sys_handler(&handle_dt);
	cle_add_sys_handler(&handle_lt);
	cle_add_sys_handler(&handle_mf);
	cle_add_sys_handler(&handle_df);
}
