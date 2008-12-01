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
	TEST-SUITE RUNNER
*/

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "test.h"
#include "../cle_core/cle_runtime.h"

static FILE* f;
static void print_struct(page_wrap* pg, const key* me, int ind)
{
	while(1){
		int i;
	
		const char* path = KDATA(me);
  		int l = me->length;
  		int o = me->offset;
		int meoff = (int)((char*)me - (char*)pg->pg);

		for(i = 0; i < ind; i++)
			fputs("..",f);

		if(l == 0)
  		{
			ptr* pt = (ptr*)me;

			if(pt->koffset == 0)
			{
				fprintf(f,"(%s%d)(EXT) page:%p (%d - n:%d) >>\n",(*path & (0x80 >> (o & 7)))?"+":"-",
					pt->offset,pt->pg,meoff,pt->next);
			}
			else
			{
				fprintf(f,"(%s%d)(INT) page:%p + %d (%d - n:%d) >>\n",(*path & (0x80 >> (o & 7)))?"+":"-",
					pt->offset,pt->pg,pt->koffset,meoff,pt->next);
			}
    	}
  		else
  		{
			int i;

			fprintf(f,"(%s%d/%d) %s (%d - s:%d n:%d) [",
				(o < l && *path & (0x80 >> (o & 7)))?"+":"-",o,l,"" /*path*/,meoff,me->sub,me->next);

			//printf("%s",path);
			for(i = 0; i < (l + 7) >> 3; i++)
			{
				printf(" %x",path[i]);
			}

			
	  		if(me->sub){
	  			fputs("] ->\n",f);
				print_struct(pg,GOOFF(pg,me->sub),ind+1);
			}
			else
	  			fputs("]\n",f);
		}
  			
		if(!me->next)
			break;
			
		//if(me == (key*)0x029b7edc)
		//{
		//	l = l;
		//}

		me = GOOFF(pg,me->next);
	}
}

void st_prt_page(st_ptr* pt)
{
	f = stdout;
	fprintf(f,"%p(%d/%d)\n",pt->pg->pg->id,pt->pg->pg->used,pt->pg->pg->waste);
	print_struct(pt->pg,GOKEY(pt->pg,pt->key),0);
}

int _tk_validate(page* pg, uint* kcount, ushort koff)
{
	while(koff != 0)
	{
		key* k = (key*)((char*)pg + koff);

		if(*kcount > (uint)(pg->used / 8))
		{
			return 1;
		}
		*kcount += 1;

		if(k->length == 0)
		{
		}
		else if(_tk_validate(pg,kcount,k->sub) != 0)
			return 1;

		koff = k->next;
	}

	return 0;
}

void _tk_print(page* pg)
{
	int koff = sizeof(page);

	printf("PAGE: %p/id:%p (%d,%d)\n",pg,pg->id,pg->used,pg->waste);

	while(koff < pg->used)
	{
		key* k = (key*)((char*)pg + koff);

		if(k->length == 0)
		{
			ptr* pt = (ptr*)k;
			if(pt->koffset == 0)
				printf("%d Eptr n:%d >> %p\n",koff,pt->next,pt->pg);
			else
				printf("%d Iptr n:%d >> %p\n",koff,pt->next,pt->pg);

			koff += sizeof(ptr);
		}
		else
		{
			printf("%d key l:%d o:%d s:%d n:%d\n",koff,k->length,k->offset,k->sub,k->next);
			koff += sizeof(key) + ((k->length + 7) >> 3);
		}

		koff += koff & 1;
	}
}

static int levels[100];
static task* t;
static int empty_keys;
static int offset_zero;
static int key_count;
static int ptr_count;

static void calc_dist(page_wrap* pg, key* me, key* parent, int level)
{
	if(level >= 100)
		return;

	while(1){
		int offset = me->offset;

		if(offset == 0)
		{
			offset_zero++;
		}

		if(((me->length + 7) >> 3) + sizeof(key) + (char*)me > (char*)pg->pg + pg->pg->used)
		{
//			printf("oops");
		}

		if(me->length == 1)
		{
			empty_keys++;
		}

		if(parent == 0 && me->offset != 0)
		{
		//	printf("oops");
		}

		if(parent != 0 && me->offset > parent->length)
		{
			printf("oops");
		}

		if(me->length == 0)
  		{
			ptr* pt = (ptr*)me;
			//st_ptr tmp;
			page_wrap* pw = pg;
			key* root = _tk_get_ptr(t, &pw, me);
			levels[level] += 1;
			ptr_count++;

			//printf("%p\n",pw);

			//if(pw == (page_wrap*)0x003b4c88)
			//{
			//	st_ptr tmp;
			//	tmp.key = sizeof(page);
			//	tmp.offset = 0;
			//	tmp.pg = pw;
			//	st_prt_page(&tmp);
			//}

			//tmp.key = (pt->koffset == 0)? sizeof(page) : pt->koffset;
			//tmp.offset = 0;
			//tmp.pg = pw;
			//puts("\n");
			//st_prt_page(&tmp);

			calc_dist(pw,root,0,level + 1);
    	}
  		else
		{
			key_count++;

			if(me->sub)
				calc_dist(pg,GOOFF(pg,me->sub),me,level);
		}
  			
		if(!me->next)
			break;
			
		me = GOOFF(pg,me->next);

		if(me->offset <= offset)
		{
			printf("oops");
		}
	}
}

void st_prt_distribution(st_ptr* pt, task* tsk)
{
	int i;

	t = tsk;

	for(i = 0; i < 100; i++)
		levels[i] = 0;

	empty_keys = 0;
	offset_zero = 0;
	key_count = 0;
	ptr_count = 0;

	//puts("\n");
	//st_prt_page(pt);
	//puts("\n");

	calc_dist(pt->pg,GOKEY(pt->pg,pt->key),0,0);

	for(i = 0; i < 100 && levels[i] != 0; i++)
		printf("L:%d -> %d\n",i + 1,levels[i]);

	printf("empty keys: %d\n",empty_keys);
	printf("zero offset: %d\n",offset_zero);
	printf("key count: %d\n",key_count);
	printf("ptr count: %d\n",ptr_count);
}

//////////////////////////////////////////////////////////////////////////
// DUMP CODE

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

static void _cle_read(task* t, st_ptr* root, uint indent)
{
	it_ptr it;
	st_ptr pt;
	uint elms = 0;

	it_create(t,&it,root);

	while(it_next(t,&pt,&it))
	{
		if(elms == 0)
		{
			puts("{");
			elms = 1;
		}
		_cle_indent(indent + 1,it.kdata,it.kused);
		_cle_read(t,&pt,indent + 1);
	}

	if(elms)
		_cle_indent(indent,"}\n",2);
	else
		puts("");

	it_dispose(t,&it);
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

void _rt_dump_function(task* t, st_ptr* root)
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

	if(!st_move(t,&tmpptr,"A",2))	// expr's dont have a.
		_cle_read(t,&tmpptr,0);

	if(st_move(t,&strings,"S",2))
	{
		err(__LINE__);
		return;
	}

	tmpptr = strings;
//	_cle_read(&tmpptr,0);

	tmpptr = *root;
	if(st_move(t,&tmpptr,"B",2))
	{
		err(__LINE__);
		return;
	}
	else
	{
		struct _body_ body;
		if(st_get(t,&tmpptr,(char*)&body,sizeof(struct _body_)) != -2)
		{
			err(__LINE__);
			return;
		}

		if(body.body != OP_BODY)
		{
			err(__LINE__);
			return;
		}

		bptr = bptr2 = (char*)tk_malloc(t,body.codesize - sizeof(struct _body_));
		if(st_get(t,&tmpptr,bptr,body.codesize - sizeof(struct _body_)) != -1)
		{
			tk_mfree(t,bptr);
			err(__LINE__);
			return;
		}

		len = body.codesize - sizeof(struct _body_);
		printf("\nCodesize %d, Params %d, Vars %d, Stacksize: %d, Firsthandler: %d\n",body.codesize,body.maxparams,body.maxvars,body.maxstack,body.firsthandler);
	}

	while(len > 0)
	{
		opc = *bptr;
		printf("%04d  ",(char*)bptr - (char*)bptr2);
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
			if(st_move(t,&tmpptr,bptr,sizeof(ushort)))
				err(__LINE__);
			else
			{
				char buffer[200];
				uint slen = st_get(t,&tmpptr,buffer,sizeof(buffer));
				printf("%-10s %.*s\n",_rt_opc_name(opc),slen - 2,buffer + 2);
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
			printf("%-10s %d %04d\n",_rt_opc_name(opc),tmpuchar,tmpushort + (char*)bptr - (char*)bptr2);
			break;

		case OP_BNZ:
		case OP_BZ:
		case OP_BR:
		case OP_DEBUG:
			// emit Is (branch forward)
			tmpushort = *((ushort*)bptr);
			bptr += sizeof(ushort);
			len -= sizeof(ushort);
			printf("%-10s %04d\n",_rt_opc_name(opc),tmpushort + (char*)bptr - (char*)bptr2);
			break;

		case OP_LOOP:
		case OP_CAV:
			// emit Is (branch back)
			tmpushort = *((ushort*)bptr);
			bptr += sizeof(ushort);
			len -= sizeof(ushort);
			printf("%-10s %04d\n",_rt_opc_name(opc),(char*)bptr - (char*)bptr2 - tmpushort);
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

	tk_mfree(t,bptr2);
	puts("\nEND_OF_FUNCTION\n");
	if(opc != OP_END)
		err(__LINE__);
}

int rt_do_read(task* t, st_ptr root)
{
	st_ptr rroot = root;
	char head[2];

	if(st_get(t,&root,head,sizeof(head)) <= 0 && head[0] == 0)
	{
		switch(head[1])
		{
		case 'E':
		case 'M':
			_rt_dump_function(t,&root);
			break;
		case 'I':
			{
				int tmp;
				if(st_get(t,&root,(char*)&tmp,sizeof(int)) == 0)
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
				int len = st_get(t,&root,buffer,sizeof(buffer));
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
		_cle_read(t,&rroot,0);

	return 0;
}
