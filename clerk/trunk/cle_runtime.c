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
#include "cle_runtime.h"

static void _cle_indent(uint indent, const char* str)
{
	while(indent-- > 0)
		printf("  ");
	printf(str);
}

static void _cle_read(st_ptr* root, uint indent)
{
	it_ptr it;
	st_ptr pt;
	uint elms = 0;

	it_create(&it,root);

	while(it_next(&pt,&it))
	{
		if(elms == 0)
		{
			puts("{");
			elms = 1;
		}
		_cle_indent(indent + 1,it.kdata);
		_cle_read(&pt,indent + 1);
	}

	if(elms)
		_cle_indent(indent,"}\n");
	else
		puts(";");
}

static void err(int line)
{
	printf("DUMP err %d line\n",line);
}

static const char* _rt_opc_name(uint opc)
{
	switch(opc)
	{
	case OP_NOOP:
		return "OP_NOOP";
	case OP_DEF:
		return "OP_DEF";
	case OP_SETP:
		return "OP_SETP";
	case OP_DOCALL_N:
		return "OP_DOCALL_N";
	case OP_DOCALL:
		return "OP_DOCALL";
	case OP_ASGN:
		return "OP_ASGN";
	case OP_ASGNRET:
		return "OP_ASGNRET";
	case OP_WIDX:
		return "OP_WIDX";
	case OP_WVAR:
		return "OP_WVAR";
	case OP_DMVW:
		return "OP_DMVW";
	case OP_MVW:
		return "OP_MVW";
	case OP_APP:
		return "OP_APP";
	case OP_OUTS:
		return "OP_OUTS";
	case OP_CONF:
		return "OP_CONF";
	case OP_RIDX:
		return "OP_RIDX";
	case OP_RVAR:
		return "OP_RVAR";
	case OP_CVAR:
		return "OP_CVAR";
	case OP_LVAR:
		return "OP_LVAR";
	case OP_MV:
		return "OP_MV";
	case OP_CMV:
		return "OP_CMV";
	case OP_FMV:
		return "OP_FMV";
	case OP_END:
		return "OP_END";
	case OP_DEFP:
		return "OP_DEFP";
	case OP_CHKP:
		return "OP_CHKP";
	case OP_BODY:
		return "OP_BODY";
	case OP_STR:
		return "OP_STR";
	case OP_CALL:
		return "OP_CALL";
	case OP_POP:
		return "OP_POP";
	default:
		return "OP_ILLEGAL";
	}
}

static struct _body_
{
	char body;
	ushort codesize;
	ushort stacksize;
	ushort maxparams;
};

static void _rt_dump_function(st_ptr app, st_ptr* root)
{
	st_ptr strings,tmpptr;
	char* bptr,*bptr2;
	uint len;
	ushort tmpushort;
	ushort tmpushort2;
	ushort tmpushort3;
	uchar tmpuchar;

	tmpptr = *root;
	strings = *root;

	if(st_move(&tmpptr,"A",2))
	{
		err(__LINE__);
		return;
	}

	puts("Function:");
	_cle_read(&tmpptr,0);

	if(st_move(&strings,"S",2))
	{
		err(__LINE__);
		return;
	}

	tmpptr = *root;
	if(st_move(&tmpptr,"B",2))
	{
		err(__LINE__);
		return;
	}
	else
	{
		struct _body_ body;
		if(st_get(&tmpptr,(char*)&body,7) != -1)
		{
			err(__LINE__);
			return;
		}

		if(body.body != OP_BODY)
		{
			err(__LINE__);
			return;
		}

		bptr = bptr2 = (char*)tk_malloc(body.codesize - 7);
		if(st_get(&tmpptr,bptr,body.codesize - 7) != 0)
		{
			tk_mfree(bptr);
			err(__LINE__);
			return;
		}

		len = body.codesize - 10;
		printf("\nCodesize %d, Stacksize: %d, Params %d\n",body.codesize,body.stacksize,body.maxparams);
	}

	while(len > 0)
	{
		uint opc = *bptr++;
		len--;

		switch(opc)
		{
		case OP_NOOP:
		case OP_DOCALL_N:
		case OP_DOCALL:
		case OP_ASGN:
		case OP_ASGNRET:
		case OP_POP:
		case OP_WIDX:
		case OP_APP:
		case OP_OUTS:
		case OP_CONF:
		case OP_RIDX:
		case OP_END:
		case OP_CHKP:
		case OP_DEF:
			// emit0
			printf("%s\n",_rt_opc_name(opc));
			break;

		case OP_CALL:
		case OP_DMVW:
		case OP_MVW:
		case OP_MV:
		case OP_CMV:
		case OP_FMV:
			// emit s
			tmpushort = *((ushort*)bptr);
			bptr += sizeof(ushort);
			printf("%s (%d) %s\n",_rt_opc_name(opc),tmpushort,bptr);
			bptr += tmpushort;
			break;

		case OP_SETP:
			// emit Ic
			tmpuchar = *bptr++;
			printf("%s %d\n",_rt_opc_name(opc),tmpuchar);
			break;

		case OP_STR:
			// emit Is
			tmpushort = *((ushort*)bptr);
			bptr += sizeof(ushort);
			tmpptr = strings;
			if(st_move(&tmpptr,(cdat)&tmpushort,sizeof(ushort)))
				err(__LINE__);
			else
			{
				uint slen = 0;
				char* str = st_get_all(&tmpptr,&slen);
				printf("%s (%d) %s\n",_rt_opc_name(opc),tmpushort,str);
			}
			break;
		case OP_WVAR:
		case OP_RVAR:
		case OP_CVAR:
		case OP_LVAR:
		case OP_DEFP:
			// emit Is
			tmpushort = *((ushort*)bptr);
			bptr += sizeof(ushort);
			printf("%s %d\n",_rt_opc_name(opc),tmpushort);
			break;

		default:
			err(__LINE__);
			return;
		}
	}

	tk_mfree(bptr2);
	err(__LINE__);
}

int rt_do_read(st_ptr* out, st_ptr* app, st_ptr root)
{
	st_ptr rroot = root;
	char head[HEAD_SIZE];

	if(0 && st_get(&root,head,sizeof(head)) <= 0 && head[0] == 0)
	{
		switch(head[1])
		{
		case 'F':
			_rt_dump_function(*app,&root);
			break;
		case 'I':
			{
				int tmp;
				if(st_get(&root,(char*)&tmp,sizeof(int)) == 0)
				{
					printf("INT(%d)\n",tmp);
				}
				else
					printf("Illegal int\n");
			}
			break;
		case 'S':
			{
				char buffer[256];
				int len = st_get(&root,buffer,sizeof(buffer));
				if(len < 0) buffer[255] = 0;
				else
					buffer[255 - len] = '\0';
				printf("STR(%s%s)\n",buffer,(len < 0)?"...":"");
			}
			break;
		default:
			printf("Illegal header %c\n",head[1]);
		}
	}
	else
		_cle_read(&rroot,0);

	return 0;
}

int rt_do_call(task* t, st_ptr* app, st_ptr* root, st_ptr* fun, st_ptr* param)
{
	return 0;
}