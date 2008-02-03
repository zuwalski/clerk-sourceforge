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
// ============ TEST / OUTLINE =================
#include "cle_clerk.h"
#include "cle_struct.h"
#include "cle_input.h"
#include "cle_runtime.h"
#include "cle_test.h"
#include <stdio.h>

static FILE* f;
static void print_struct(page_wrap* pg, const key* me, int ind)
{
	while(1){
		int i;
	
		const char* path = KDATA(me);
  		int l = me->length;
  		int o = me->offset;
  		
		for(i = 0; i < ind; i++)
			fputs("  ",f);

		if(l == 0)
  		{
//  			ptr* pt = (ptr*)me;
//  			page* pg = pt->pg;
//  			fprintf(f,">> (%d) %p(%d/%d) %p >>\n",pt->offset,pg,pg->insert,pg->waste,me);
  			
//	  		if(pg->insert > sizeof(page))
//  				print_struct(FIRSTKEY(pg),ind);
//  			fprintf(f,"<< %p (%d) <<\n",pt->pg,pt->offset);
    	}
  		else
  		{

			fprintf(f,"%s (%s%d/%d) %p",path,
	  		(o < l && *path & (0x80 >> (o & 7)))?"+":"-",o,l,me);
			
	  		if(me->sub){
	  			fputs(" ->\n",f);
				print_struct(pg,GOOFF(pg,me->sub),ind+1);
			}
			else
	  			fputs("\n",f);
		}
  			
		if(!me->next)
			break;
			
		me = GOOFF(pg,me->next);
	}
}

void st_prt_page(st_ptr* pt)
{
	f = stdout;
	fprintf(f,"%p(%d/%d)\n",pt->pg,pt->pg->pg.used,pt->pg->pg.waste);
	print_struct(pt->pg,GOKEY(pt->pg,pt->key),0);
}


/*
	stack-strukture:
	var-space
	stack-space

	runtime:
	[0] <- assign to: st_ptr OR output-identifier
	[...] <- values
*/
// code-header

static void _cle_indent(uint indent, const char* str, uint length)
{
	while(indent-- > 0)
		printf("  ");
	printf("%.*s",length,str);
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
		_cle_indent(indent + 1,it.kdata,it.kused);
		_cle_read(&pt,indent + 1);
	}

	if(elms)
		_cle_indent(indent,"}\n",2);
	else
		puts("");
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
	case OP_SETP:
		return "OP_SETP";
	case OP_DOCALL:
		return "OP_DOCALL";
	case OP_DOCALL_N:
		return "OP_DOCALL_N";
	case OP_WIDX:
		return "OP_WIDX";
	case OP_WVAR0:
		return "OP_WVAR0";
	case OP_WVAR:
		return "OP_WVAR";
	case OP_DMVW:
		return "OP_DMVW";
	case OP_MVW:
		return "OP_MVW";
	case OP_OUT:
		return "OP_OUT";
	case OP_OUTL:
		return "OP_OUTL";
	case OP_OUTLT:
		return "OP_OUTLT";
	case OP_CONF:
		return "OP_CONF";
	case OP_RIDX:
		return "OP_RIDX";
	case OP_RVAR:
		return "OP_RVAR";
	case OP_LVAR:
		return "OP_LVAR";
	case OP_MV:
		return "OP_MV";
	case OP_END:
		return "OP_END";
	case OP_DEFP:
		return "OP_DEFP";
	case OP_BODY:
		return "OP_BODY";
	case OP_STR:
		return "OP_STR";
	case OP_CALL:
		return "OP_CALL";
	case OP_POP:
		return "OP_POP";
	case OP_POPW:
		return "OP_POPW";
	case OP_FUN:
		return "OP_FUN";
	case OP_FREE:
		return "OP_FREE";
	case OP_AVARS:
		return "OP_AVARS";
	case OP_OVARS:
		return "OP_OVARS";
	case OP_ADD:
		return "OP_ADD";
	case OP_SUB:
		return "OP_SUB";
	case OP_MUL:
		return "OP_MUL";
	case OP_DIV:
		return "OP_DIV";
	case OP_REM:
		return "OP_REM";
	case OP_IMM:
		return "OP_IMM";
	case OP_BNZ:
		return "OP_BNZ";
	case OP_BZ:
		return "OP_BZ";
	case OP_BR:
		return "OP_BR";
	case OP_NE:
		return "OP_NE";
	case OP_GE:
		return "OP_GE";
	case OP_GT:
		return "OP_GT";
	case OP_LE:
		return "OP_LE";
	case OP_LT:
		return "OP_LT";
	case OP_EQ:
		return "OP_EQ";
	case OP_LOOP:
		return "OP_LOOP";
	case OP_CAV:
		return "OP_CAV";
	case OP_NULL:
		return "OP_NULL";
	case OP_CLEAR:
		return "OP_CLEAR";
	case OP_AVAR:
		return "OP_AVAR";
	case OP_ERROR:
		return "OP_ERROR";
	case OP_CAT:
		return "OP_CAT";
	case OP_CMV:
		return "OP_CMV";
	case OP_FMV:
		return "OP_FMV";
	case OP_NOT:
		return "OP_NOT";
	case OP_DEBUG:
		return "OP_DEBUG";
	case OP_NEW:
		return "OP_NEW";

	default:
		return "OP_ILLEGAL";
	}
}

static void _rt_dump_function(st_ptr* root)
{
	st_ptr strings,tmpptr;
	char* bptr,*bptr2;
	int len,tmpint;
	uint opc = 0;
	ushort tmpushort;
	uchar tmpuchar;
	uchar tmpuchar2;

	tmpptr = *root;
	strings = *root;

	puts("BEGIN_FUNCTION/EXPRESSION\nAnnotations:");

	if(!st_move(&tmpptr,"A",2))	// expr's dont have a.
		_cle_read(&tmpptr,0);

	if(st_move(&strings,"S",2))
	{
		err(__LINE__);
		return;
	}

	tmpptr = strings;
//	_cle_read(&tmpptr,0);

	tmpptr = *root;
	if(st_move(&tmpptr,"B",2))
	{
		err(__LINE__);
		return;
	}
	else
	{
		struct _body_ body;
		if(st_get(&tmpptr,(char*)&body,sizeof(struct _body_)) != -2)
		{
			err(__LINE__);
			return;
		}

		if(body.body != OP_BODY)
		{
			err(__LINE__);
			return;
		}

		bptr = bptr2 = (char*)tk_malloc(body.codesize - sizeof(struct _body_));
		if(st_get(&tmpptr,bptr,body.codesize - sizeof(struct _body_)) != -1)
		{
			tk_mfree(bptr);
			err(__LINE__);
			return;
		}

		len = body.codesize - sizeof(struct _body_);
		printf("\nCodesize %d, Params %d, Vars %d, Stacksize: %d, Firsthandler: %d\n",body.codesize,body.maxparams,body.maxvars,body.maxstack,body.firsthandler);
	}

	while(len > 0)
	{
		opc = *bptr;
		printf("%04d  ",(uint)bptr - (uint)bptr2);
		len--;
		bptr++;

		switch(opc)
		{
		case OP_NOOP:
		case OP_DOCALL:
		case OP_DOCALL_N:
		case OP_POP:
		case OP_POPW:
		case OP_WIDX:
		case OP_OUT:
		case OP_OUTL:
		case OP_OUTLT:
		case OP_CONF:
		case OP_RIDX:
		case OP_FUN:
		case OP_ADD:
		case OP_SUB:
		case OP_MUL:
		case OP_DIV:
		case OP_REM:
		case OP_NE:
		case OP_GE:
		case OP_GT:
		case OP_LE:
		case OP_LT:
		case OP_EQ:
		case OP_NULL:
		case OP_CLEAR:
		case OP_CAT:
		case OP_NOT:
		case OP_END:
		case OP_CALL:
			// emit0
			printf("%s\n",_rt_opc_name(opc));
			break;
/*		case OP_END:
			// emit0
			puts("OP_END\nEND_OF_FUNCTION\n");
			if(len != 0)
				printf("!!! Remaining length: %d\n",len);
			tk_mfree(bptr2);
			return;
*/
		case OP_DMVW:
		case OP_MVW:
		case OP_MV:
		case OP_CMV:
		case OP_FMV:
		case OP_NEW:
			// emit s
			tmpushort = *((ushort*)bptr);
			bptr += sizeof(ushort);
			printf("%-10s (%d) %s\n",_rt_opc_name(opc),tmpushort,bptr);
			bptr += tmpushort;
			len -= tmpushort + sizeof(ushort);
			break;

		case OP_SETP:
		case OP_WVAR0:
		case OP_WVAR:
		case OP_RVAR:
		case OP_LVAR:
		case OP_AVAR:
			// emit Ic
			tmpuchar = *bptr++;
			printf("%-10s %d\n",_rt_opc_name(opc),tmpuchar);
			len--;
			break;

		case OP_AVARS:
		case OP_OVARS:
			// emit Ic
			tmpuchar = *bptr++;
			printf("%-10s %d {",_rt_opc_name(opc),tmpuchar);
			while(tmpuchar-- > 0)
			{
				printf("%d ",*bptr++);
				len--;
			}
			puts("}");
			len--;
			break;

		case OP_STR:
			// emit Is
			tmpptr = strings;
			if(st_move(&tmpptr,bptr,sizeof(ushort)))
				err(__LINE__);
			else
			{
				uint slen = 0;
				char* str = st_get_all(&tmpptr,&slen);
				printf("%-10s %.*s\n",_rt_opc_name(opc),slen > HEAD_SIZE? slen - HEAD_SIZE : 0,str + HEAD_SIZE);
				tk_mfree(str);
			}
			bptr += sizeof(ushort);
			len -= sizeof(ushort);
			break;

		case OP_DEFP:
			// emit Is2 (branch forward)
			tmpuchar = *bptr++;
			tmpushort = *((ushort*)bptr);
			bptr += sizeof(ushort);
			len -= sizeof(ushort) + 1;
			printf("%-10s %d %04d\n",_rt_opc_name(opc),tmpuchar,tmpushort + (uint)bptr - (uint)bptr2);
			break;

		case OP_BNZ:
		case OP_BZ:
		case OP_BR:
		case OP_DEBUG:
			// emit Is (branch forward)
			tmpushort = *((ushort*)bptr);
			bptr += sizeof(ushort);
			len -= sizeof(ushort);
			printf("%-10s %04d\n",_rt_opc_name(opc),tmpushort + (uint)bptr - (uint)bptr2);
			break;

		case OP_LOOP:
		case OP_CAV:
			// emit Is (branch back)
			tmpushort = *((ushort*)bptr);
			bptr += sizeof(ushort);
			len -= sizeof(ushort);
			printf("%-10s %04d\n",_rt_opc_name(opc),(uint)bptr - (uint)bptr2 - tmpushort);
			break;

		case OP_FREE:
			// emit Ic2 (byte,byte)
			tmpuchar = *bptr++;
			tmpuchar2 = *bptr++;
			len -= 2;
			printf("%-10s %d %d\n",_rt_opc_name(opc),tmpuchar,tmpuchar2);
			break;

		case OP_IMM:
			// emit II (imm int)
			tmpint = *((int*)bptr);
			bptr += sizeof(int);
			len -= sizeof(int);
			printf("%-10s %d\n",_rt_opc_name(opc),tmpint);
			break;

		default:
			printf("ERR OPC(%d)\n",opc);
//			err(__LINE__);
//			tk_mfree(bptr2);
//			return;
		}
	}

	tk_mfree(bptr2);
	puts("\nEND_OF_FUNCTION\n");
	if(opc != OP_END)
		err(__LINE__);
}

int rt_do_read(st_ptr root)
{
	st_ptr rroot = root;
	char head[HEAD_SIZE];

	if(st_get(&root,head,sizeof(head)) <= 0 && head[0] == 0)
	{
		switch(head[1])
		{
		case 'E':
		case 'F':
			_rt_dump_function(&root);
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
			printf("Can't read this type - use functions. Type: %c\n",head[1]);
		}
	}
	else
		_cle_read(&rroot,0);

	return 0;
}

/*
** SYSTEM-HANDLERS FOR INPUT-SYSTEM
**
** compiler needs to handle the following system-events:
** - set field (sf)
** eventid names type
** 1.next: path.typename
** 2.next: field.path
** 3.next: data [(... -> function, =... -> expr, 0-9 -> number else: " text]
** x.next error
*/

struct _field
{
	st_ptr ptr;
};

static int _tst_setup(sys_handler_data* hd)
{
	struct _field* param = (struct _field*)tk_malloc(sizeof(struct _field));
	hd->data = param;

	param->ptr = hd->instance;
	return 0;
}

static int _tst_end(sys_handler_data* hd, cdat code, uint length)
{
	tk_mfree(hd->data);
	return 0;
}

static char no_such_type[] = "no such type\n";
static char no_such_field[] = "no such field\n";

static int _tst_do_next(sys_handler_data* hd, st_ptr pt, uint depth)
{
	struct _field* param = (struct _field*)hd->data;
	char* data;
	uint length;
	int rcode = 0;

	if(depth != 0 || hd->next_call > 1)
		return -1;

	switch(hd->next_call)
	{
	case 0:		// type
		// the type MUST exist
		if(st_move(&param->ptr,HEAD_TYPE,HEAD_SIZE))
			return -1;

		data = st_get_all(&pt,&length);

		if(st_move(&param->ptr,data,length))
		{
			hd->response->data(hd->respdata,no_such_type,sizeof(no_such_type));
			rcode = -1;
		}

		tk_mfree(data);
		break;
	case 1:		// field
		// if field must exsist
		data = st_get_all(&pt,&length);

		if(st_move(&param->ptr,data,length))
		{
			hd->response->data(hd->respdata,no_such_field,sizeof(no_such_field));
			rcode = -1;
		}
		else
		{
			rcode = rt_do_read(param->ptr);
		}

		tk_mfree(data);
	}

	return rcode;
}

static cle_syshandler handle_dump = {"dump",4,_tst_setup,_tst_do_next,_tst_end,0};

void tst_setup()
{
	cle_add_sys_handler(&handle_dump);
}
