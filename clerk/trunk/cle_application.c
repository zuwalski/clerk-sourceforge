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

struct _sp_parameters
{
	st_ptr app;
	cdat propertyname;
	char* value[4];
	uint vallen[4];
	uint prolen;
};

/*
** SYSTEM-HANDLERS FOR INPUT-SYSTEM

	handler must handle these events:

	- new application (na) [appname]
	- rename app (ra) [appname] [new appname]
	- delete app (da) [appname]
	- list apps (la)

	- set app property (sp) [appname] [property] [value]
**
*/

// KEYWORDS
static char kw_event[] = "event";
static char kw_single[] = "single";
static char kw_session[] = "session";
static char kw_request[] = "request";
static char kw_const[] = "const";
static char kw_extends[] = "extends";
static char kw_import[] = "import";

// new app
static int _do_na(sys_handler_data* hd, st_ptr pt, uint depth)
{
	int rcode = 0;

	if(hd->next_call == 0 && depth == 0)
	{
		uint length;
		char* appname = st_get_all(&pt,&length);

		if(length > 0)
		{
			st_ptr newapp = hd->instance;
			st_insert(hd->t,&newapp,HEAD_APPS,HEAD_SIZE);

			if(!st_insert(hd->t,&newapp,appname,length))
				rcode = -2;
		}

		tk_mfree(appname);
	}
	else rcode = -1;

	return rcode;
}

// delete app
static int _do_da(sys_handler_data* hd, st_ptr pt, uint depth)
{
	int rcode = 0;

	if(hd->next_call == 0 && depth == 0)
	{
		uint length;
		char* appname = st_get_all(&pt,&length);

		if(length > 0)
		{
			st_ptr delapp = hd->instance;
			if(!st_move(&delapp,HEAD_APPS,HEAD_SIZE))
				st_delete(hd->t,&delapp,appname,length);
		}

		tk_mfree(appname);
	}
	else rcode = -1;

	return rcode;
}

// list apps (end-handler)
static int _do_la(sys_handler_data* hd, cdat code, uint length)
{
	if(length == 0)	// no error
	{
		st_ptr apps = hd->instance;

		hd->response->start(hd->respdata);

		if(!st_move(&apps,HEAD_APPS,HEAD_SIZE))
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

// set app property
static int do_setup_sp(sys_handler_data* hd)
{
	struct _sp_parameters* param = (struct _sp_parameters*)tk_malloc(sizeof(struct _sp_parameters));
	hd->data = param;

	param->value[0] = 0;
	param->value[1] = 0;
	param->value[2] = 0;
	param->value[3] = 0;
	return 0;
}

static int _do_next_sp(sys_handler_data* hd, st_ptr pt, uint depth)
{
	struct _sp_parameters* param = (struct _sp_parameters*)hd->data;
	char* value;
	uint length,rcode = 0;

	if(depth != 0 || hd->next_call > 5)
		return -1;

	value = st_get_all(&pt,&length);

	switch(hd->next_call)
	{
	case 0:
		param->app = hd->instance;

		rcode = -1;
		if(!st_move(&param->app,HEAD_APPS,HEAD_SIZE))
		{
			if(!st_move(&param->app,value,length))
				rcode = 0;
		}
		break;
	case 1:
		param->propertyname = value;
		param->prolen       = length;
		break;
	default:
		param->value[hd->next_call - 2]  = value;
		param->vallen[hd->next_call - 2] = length;
	}

	tk_mfree(value);
	return rcode;
}

int do_end_sp(sys_handler_data* hd, cdat code, uint length)
{
	struct _sp_parameters* param = (struct _sp_parameters*)hd->data;
	uint rcode = 0;
	if(length == 0)	// no error
	{
		st_ptr pt = param->app;
		// set app property
		if(param->prolen == sizeof(kw_event) && memcmp(kw_event,param->propertyname,sizeof(kw_event)))
		{
			// EVENT
			if(hd->next_call == 5)
			{
				st_insert(hd->t,&pt,HEAD_EVENT,HEAD_SIZE);

				st_insert(hd->t,&pt,param->value[0],param->vallen[0]);	// event-mask

				st_insert(hd->t,&pt,param->value[1],param->vallen[1]);	// single|session|request

				st_insert(hd->t,&pt,param->value[2],param->vallen[2]);	// typename

				st_insert(hd->t,&pt,param->value[3],param->vallen[3]);	// role-list
			}
			else rcode = -1;
		}
		else if(param->prolen == sizeof(kw_extends) && memcmp(kw_extends,param->propertyname,sizeof(kw_extends)))
		{
			// EXTENDS
			st_insert(hd->t,&pt,HEAD_EXTENDS,HEAD_SIZE);

			st_insert(hd->t,&pt,param->value[0],param->vallen[0]);	// typename
		}
		else if(param->prolen == sizeof(kw_const) && memcmp(kw_const,param->propertyname,sizeof(kw_const)))
		{
			// CONST
			st_insert(hd->t,&pt,kw_const,sizeof(kw_const));
		}
		else if(param->prolen == sizeof(kw_import) && memcmp(kw_import,param->propertyname,sizeof(kw_import)))
		{
			// IMPORT
			st_insert(hd->t,&pt,HEAD_IMPORT,HEAD_SIZE);
		}
		else
		{
			// NAMED APPLICATION-OBJECT
			st_insert(hd->t,&pt,param->propertyname,param->prolen);

			st_insert(hd->t,&pt,param->value[0],param->vallen[0]);	// single|session|request

			st_insert(hd->t,&pt,param->value[1],param->vallen[1]);	// typename
		}
	}

	tk_mfree(param->value[0]);
	tk_mfree(param->value[1]);
	tk_mfree(param->value[2]);
	tk_mfree(param->value[3]);

	tk_mfree(hd->data);
	return rcode;
}

static cle_syshandler handle_na = {"na",2,0,_do_na,0,0};
static cle_syshandler handle_ra = {"ra",2,0,0,0,0};
static cle_syshandler handle_da = {"da",2,0,_do_da,0,0};
static cle_syshandler handle_la = {"la",2,0,0,_do_la,0};
static cle_syshandler handle_sp = {"sp",2,do_setup_sp,_do_next_sp,do_end_sp,0};

void app_setup()
{
	cle_add_sys_handler(&handle_na);
	cle_add_sys_handler(&handle_ra);
	cle_add_sys_handler(&handle_da);
	cle_add_sys_handler(&handle_la);
	cle_add_sys_handler(&handle_sp);
}
