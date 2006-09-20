/* Copyrigth(c) Lars Szuwalski, 2006 */

#include "cle_runtime.h"

const char* funspace = "funs";

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
	case OP_STR:
		return "OP_STR";
	case OP_PUBLIC_FUN:
		return "OP_PUBLIC_FUN";
	case OP_FUNCTION_END:
		return "OP_FUNCTION_END";
	case OP_FUNCTION_REF:
		return "OP_FUNCTION_REF";
	case OP_FUNCTION_REF_DONE:
		return "OP_FUNCTION_REF_DONE";
	case OP_CALL:
		return "OP_CALL";
	case OP_PARAMS:
		return "OP_PARAMS";
//	case OP_NEW:
//		return "OP_NEW";
	case OP_APP_ROOT:
		return "OP_APP_ROOT";
	case OP_MOVE_WRITER:
		return "OP_MOVE_WRITER";
	case OP_MOVE_READER:
		return "OP_MOVE_READER";
	case OP_DUB_MOVE_WRITER:
		return "OP_DUB_MOVE_WRITER";
	case OP_DUB_MOVE_READER:
		return "OP_DUB_MOVE_READER";
	case OP_MOVE_READER_FUN:
		return "OP_MOVE_READER_FUN";
	case OP_READER_TO_WRITER:
		return "OP_READER_TO_WRITER";
	case OP_WRITER_TO_READER:
		return "OP_WRITER_TO_READER";
//	case OP_WRITER_POP:
//		return "OP_WRITER_POP";
	case OP_READER_POP:
		return "OP_READER_POP";
//	case OP_STR_POP:
//		return "OP_STR_POP";
	case OP_POP_READER:
		return "OP_POP_READER";
	case OP_POP_WRITER:
		return "OP_POP_WRITER";
	case OP_DEF_VAR:
		return "OP_DEF_VAR";
	case OP_VAR_FREE:
		return "OP_VAR_FREE";
	case OP_LOAD_VAR:
		return "OP_LOAD_VAR";
	case OP_VAR_READ:
		return "OP_VAR_READ";
	case OP_VAR_WRITE:
		return "OP_VAR_WRITE";
	case OP_LOAD_PARAM:
		return "OP_LOAD_PARAM";
	case OP_PARAM_READ:
		return "OP_PARAM_READ";
	case OP_PARAM_WRITE:
		return "OP_PARAM_WRITE";
	case OP_SET_PARAM:
		return "OP_SET_PARAM";
	default:
		return "OP_ILLEGAL";
	}
}

static void _rt_dump_function(st_ptr app, st_ptr* root)
{
	st_ptr strings,tmpptr;
	char* bptr,*bptr2;
	uint funidx,tmpuint;
	int len;
	ushort tmpushort;

	if(st_get(root,(char*)&funidx,sizeof(uint)) > 0)
	{
		err(__LINE__);
		return;
	}

	root = &app;

	if(st_move(root,funspace,FUNSPACE_SIZE))
	{
		err(__LINE__);
		return;
	}

	if(st_move(root,(cdat)&funidx,sizeof(uint)))
	{
		err(__LINE__);
		return;
	}

	tmpptr = *root;

	if(st_move(root,"A",2))
	{
		err(__LINE__);
		return;
	}

	puts("Function:");
	_cle_read(root,0);

	root = &tmpptr;
	strings = tmpptr;

	if(st_move(root,"B",2))
	{
		err(__LINE__);
		return;
	}

	if(st_move(&strings,"S",2))
	{
		err(__LINE__);
		return;
	}

	puts("\nCode:");
	bptr = bptr2 = st_get_all(root,&len);

	while(len > 0)
	{
		uint opc = *bptr++;
		len--;

		switch(opc)
		{
		case OP_FUNCTION_END:
			printf("%s\nFunction done\n",_rt_opc_name(opc));
			tk_mfree(bptr2);
			return;
		case OP_NOOP:
		case OP_PUBLIC_FUN:
		case OP_PARAMS:
//		case OP_NEW:
		case OP_APP_ROOT:
		case OP_READER_TO_WRITER:
		case OP_WRITER_TO_READER:
//		case OP_WRITER_POP:
		case OP_READER_POP:
//		case OP_STR_POP:
		case OP_POP_READER:
		case OP_POP_WRITER:
		case OP_FUNCTION_REF_DONE:
			printf("%s\n",_rt_opc_name(opc));
			break;
		case OP_STR:
			tmpushort = *((ushort*)bptr);
			len -= sizeof(ushort);
			bptr += sizeof(ushort);

			tmpptr = strings;
			if(st_move(&tmpptr,(cdat)&tmpushort,sizeof(ushort)))
				err(__LINE__);
			else
			{
				uint slen;
				char* str = st_get_all(&tmpptr,&slen);

				printf("%s (%s)\n",_rt_opc_name(opc),str);
				tk_mfree(str);
			}
			break;
		case OP_CALL:
		case OP_MOVE_WRITER:
		case OP_MOVE_READER:
		case OP_DUB_MOVE_READER:
		case OP_DUB_MOVE_WRITER:
		case OP_MOVE_READER_FUN:
		case OP_LOAD_PARAM:
		case OP_PARAM_READ:
		case OP_PARAM_WRITE:
		case OP_SET_PARAM:
			tmpuint = *((uint*)bptr);	// size
			len -= sizeof(uint);
			bptr += sizeof(uint);

			printf("%s %s\n",_rt_opc_name(opc),bptr);

			bptr += tmpuint;
			len -= tmpuint;
			break;
		case OP_FUNCTION_REF:
			tmpuint = *((uint*)bptr);
			len -= sizeof(uint);
			bptr += sizeof(uint);

			printf("%s %d\n",_rt_opc_name(opc),tmpuint);
			break;
		case OP_DEF_VAR:
		case OP_VAR_FREE:
		case OP_LOAD_VAR:
		case OP_VAR_READ:
		case OP_VAR_WRITE:
			tmpushort = *((ushort*)bptr);
			len -= sizeof(ushort);
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

	if(st_get(&root,head,sizeof(head)) < 0 && head[0] == 0)
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
	st_ptr* writer = 0;
	st_ptr stack[100];
	st_ptr strings,tmpptr;
	char* pc,*codemem;
	uint funidx,tmpuint;
	int sp = -1;
	ushort tmpushort;

	tmpptr = *fun;
	if(st_get(&tmpptr,(char*)&funidx,sizeof(uint)) > 0)
		return(__LINE__);

	tmpptr = *app;
	if(st_move(&tmpptr,funspace,FUNSPACE_SIZE))
		return(__LINE__);

	if(st_move(&tmpptr,(cdat)&funidx,sizeof(uint)))
		return(__LINE__);

	strings = tmpptr;
	if(st_move(&strings,"S",2))	// root string-space
		return(__LINE__);

	if(st_move(&tmpptr,"B",2))	// body
		return(__LINE__);

	pc = codemem = st_get_all(&tmpptr,&tmpuint);	// load function-code
	// entry-function MUST be public
	if(*pc++ != OP_PUBLIC_FUN)
		return(__LINE__);

	// run code
	while(1)
	{
		switch(*pc++)
		{
		case OP_FUNCTION_END:
			if(sp > 0)
			{
				writer = (st_ptr*)stack[sp--].pg;
				if(writer == 0)
					rt_do_read(0,app,stack[sp]);	// output
				else
					;	// copy stack[sp] -> writer
			}
			tk_mfree(codemem);
			return 0;
		case OP_NOOP:
			break;
		case OP_APP_ROOT:
			stack[++sp] = *app;
			break;
		case OP_READER_TO_WRITER:	// swap sp/w
			writer = &stack[sp];
			break;
		case OP_WRITER_TO_READER:	// function start with "new" -> clear by function-end
			sp++;
			stack[++sp].pg = (page_wrap*)writer;
			if(writer == 0)
			{
				st_empty(t,&stack[sp - 1]);	// create "receiver"
				writer = &stack[sp - 1];
			}
			break;
		case OP_POP_WRITER:
			writer = (st_ptr*)stack[sp--].pg;
			break;
		case OP_POP_READER:
			sp--;
			break;
		case OP_READER_POP:	// copy/output to writer
			if(writer)	// copy / lazy
			{
			}
			else
				rt_do_read(0,app,stack[sp]);	// output
			sp--;
			break;
		case OP_PARAMS:
			st_empty(t,&stack[++sp]);
			break;
		case OP_FUNCTION_REF_DONE:
			break;
		case OP_STR:
			tmpushort = *((ushort*)pc);
			pc += sizeof(ushort);
			stack[++sp] = strings;
			if(st_move(&stack[sp],(cdat)&tmpushort,sizeof(ushort)))
				return(__LINE__);
			break;
		case OP_DUB_MOVE_WRITER:
			stack[++sp] = *writer;
		case OP_MOVE_WRITER:
			tmpushort = *((ushort*)pc);
			pc += sizeof(ushort);
			st_insert(t,writer,pc,tmpushort);
			pc += tmpushort;
			break;
		case OP_DUB_MOVE_READER:
			stack[++sp] = stack[sp];
		case OP_MOVE_READER:
			tmpushort = *((ushort*)pc);
			pc += sizeof(ushort);
			if(st_move(&stack[sp],pc,tmpushort))
				;
			pc += tmpushort;
			break;
		case OP_MOVE_READER_FUN:
			stack[++sp] = *root;
			tmpushort = *((ushort*)pc);
			pc += sizeof(ushort);
			if(st_move(&stack[sp],pc,tmpushort))
				;
			pc += tmpushort;
			break;
		case OP_CALL:
		case OP_LOAD_PARAM:
		case OP_PARAM_READ:
		case OP_PARAM_WRITE:
		case OP_SET_PARAM:
			tmpushort = *((ushort*)pc);
			pc += sizeof(ushort);

			pc += tmpushort;
			break;
		case OP_FUNCTION_REF:
			tmpuint = *((uint*)pc);
			pc += sizeof(uint);
			break;
		case OP_DEF_VAR:
		case OP_VAR_FREE:
		case OP_LOAD_VAR:
		case OP_VAR_READ:
		case OP_VAR_WRITE:
			tmpushort = *((ushort*)pc);
			pc += sizeof(ushort);
			break;
		//case OP_PUBLIC_FUN:
		default:
			tk_mfree(codemem);
			return(__LINE__);
		}
	}
}
