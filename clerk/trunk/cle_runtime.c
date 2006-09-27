/* Copyrigth(c) Lars Szuwalski, 2006 */

#include "cle_runtime.h"

const char* funspace = "funs";

static struct _rt_continuation
{
	st_ptr	*app,
			*root,
			*param;

	st_ptr* writer;
	st_ptr* stack;
	char *codemem,*pc;
	st_ptr strings;
	uint funidx, call_state, sp;
};


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
	case OP_NEW:
		return "OP_NEW";
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
	case OP_READER_OUT:
		return "OP_READER_OUT";
	case OP_READER_CLEAR:
		return "OP_READER_CLEAR";
	case OP_VAR_OUT:
		return "OP_VAR_OUT";
	case OP_POP:
		return "OP_POP";
	case OP_DEF_VAR:
		return "OP_DEF_VAR";
	case OP_DEF_VAR_REF:
		return "OP_DEF_VAR_REF";
	case OP_LOAD_VAR:
		return "OP_LOAD_VAR";
	case OP_VAR_REF:
		return "OP_VAR_REF";
	case OP_VAR_CLEAR:
		return "OP_VAR_CLEAR";
	case OP_VAR_POP:
		return "OP_VAR_POP";
	case OP_VAR_READ:
		return "OP_VAR_READ";
	case OP_VAR_WRITE:
		return "OP_VAR_WRITE";
	case OP_LOAD_PARAM:
		return "OP_LOAD_PARAM";
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

	bptr = bptr2 = st_get_all(root,&len);

	tmpushort = *((ushort*)(bptr + len - sizeof(ushort)));
	printf("\nCode (code size: %d, stack size: %d):\n",len,(int)tmpushort);

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
		case OP_APP_ROOT:
		case OP_READER_TO_WRITER:
		case OP_WRITER_TO_READER:
		case OP_NEW:
		case OP_READER_OUT:
		case OP_READER_CLEAR:
		case OP_VAR_CLEAR:
		case OP_VAR_OUT:
		case OP_VAR_POP:
		case OP_POP:
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
		case OP_SET_PARAM:
			tmpushort = *((ushort*)bptr);	// size
			len -= sizeof(ushort);
			bptr += sizeof(ushort);

			printf("%s %s\n",_rt_opc_name(opc),bptr);

			bptr += tmpushort;
			len -= tmpushort;
			break;
		case OP_FUNCTION_REF:
			tmpuint = *((uint*)bptr);
			len -= sizeof(uint);
			bptr += sizeof(uint);

			printf("%s %d\n",_rt_opc_name(opc),tmpuint);
			break;
		case OP_DEF_VAR:
		case OP_DEF_VAR_REF:
		case OP_LOAD_VAR:
		case OP_VAR_REF:
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

static struct _rt_continuation* _rt_create_continuation(task* t, st_ptr* app, st_ptr* root, st_ptr* fun, st_ptr* param, uchar entry_fun)
{
	struct _rt_continuation* cont;
	st_ptr tmpptr;
	uint   tmpuint;
	ushort tmpushort;
	char   head[HEAD_SIZE];

	cont = (struct _rt_continuation*)tk_malloc(sizeof(struct _rt_continuation));

	cont->app = app;
	cont->root = root;
	cont->param = param;

	tmpptr = *fun;
	if(st_get(&tmpptr,head,HEAD_SIZE) != -1 || head[0] != 0 || head[1] != 'F')
		return 0;

	if(st_get(&tmpptr,(char*)&cont->funidx,sizeof(uint)) > 0)
		return 0;

	tmpptr = *app;
	if(st_move(&tmpptr,funspace,FUNSPACE_SIZE))
		return 0;

	if(st_move(&tmpptr,(cdat)&cont->funidx,sizeof(uint)))
		return 0;

	cont->strings = tmpptr;
	st_move(&cont->strings,"S",HEAD_SIZE);	// root string-space

	st_move(&tmpptr,"B",HEAD_SIZE);	// body

	cont->codemem = st_get_all(&tmpptr,&tmpuint);	// load function-code
	cont->pc = cont->codemem = tk_realloc(cont->codemem,tmpuint);

	// entry-function MUST be public
	if(*cont->pc == OP_PUBLIC_FUN)
		cont->pc++;
	else if(entry_fun)
		return 0;

	// alloc stack-space for function
	tmpushort = *((ushort*)(cont->codemem + tmpuint - sizeof(ushort)));
	cont->stack = tk_malloc(tmpushort * sizeof(st_ptr));

	cont->sp = 0;

	return cont;
}

static void _rt_dispose_continuation(task* t, struct _rt_continuation* cont)
{
	struct _rt_continuation* tmp;

	if(cont == 0)
		return;

	tk_mfree(cont);
}

static int _rt_eval(task* t, st_ptr* dest, st_ptr* source)
{
	return 0;
}

int rt_do_call(task* t, st_ptr* app, st_ptr* root, st_ptr* fun, st_ptr* param)
{
	st_ptr* writer;
	st_ptr* stack;
	st_ptr strings,tmpptr;
	st_ptr outstack[100];
	char *pc,*codemem,*tmpcharptr;
	uint funidx,tmpuint,outsp = 0,sp = 1;
	ushort tmpushort;
	char head[HEAD_SIZE];

	tmpptr = *fun;
	if(st_get(&tmpptr,head,HEAD_SIZE) != -1 || head[0] != 0 || head[1] != 'F')
		return(__LINE__);

	if(st_get(&tmpptr,(char*)&funidx,sizeof(uint)) > 0)
		return(__LINE__);

	tmpptr = *app;
	if(st_move(&tmpptr,funspace,FUNSPACE_SIZE))
		return(__LINE__);

	if(st_move(&tmpptr,(cdat)&funidx,sizeof(uint)))
		return(__LINE__);

	strings = tmpptr;
	st_move(&strings,"S",HEAD_SIZE);	// root string-space

	st_move(&tmpptr,"B",HEAD_SIZE);	// body

	pc = codemem = st_get_all(&tmpptr,&tmpuint);	// load function-code
	// entry-function MUST be public
	if(*pc++ != OP_PUBLIC_FUN)
		return(__LINE__);

	// alloc stack-space for function
	tmpushort = *((ushort*)(codemem + tmpuint - sizeof(ushort)));
	stack = tk_malloc(tmpushort * sizeof(st_ptr));

	// setup default output
	stack[0].pg = 0;
	st_empty(t,&stack[1]);
	writer = &stack[1];

	// run code
	while(1)
	{
		switch(*pc++)
		{
		case OP_FUNCTION_END:
			// output sp_0
			return 0;
		case OP_NOOP:
			break;
		case OP_APP_ROOT:
			stack[++sp] = *app;
			break;
		case OP_WRITER_TO_READER:
			break;
		case OP_READER_TO_WRITER:	// save writer behind new target - update writer
			tmpptr = stack[sp];
			stack[sp].pg = (page_wrap*)writer;
			stack[++sp] = tmpptr;
			writer = &stack[sp];
			break;
		case OP_NEW:
			st_empty(t,&stack[++sp]);
			break;
		case OP_POP:
			sp--;
			break;
		case OP_VAR_OUT:
		case OP_READER_OUT:
			_rt_eval(t,writer,&stack[sp--]);
			break;
		case OP_VAR_CLEAR:
		case OP_READER_CLEAR:	// match OP_READER_TO_WRITER
			_rt_eval(t,writer,&stack[sp--]);
			writer = (st_ptr*)stack[sp--].pg;	// restore writer
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
			stack[++sp] = stack[sp];
		case OP_MOVE_WRITER:
			tmpushort = *((ushort*)pc);
			pc += sizeof(ushort);
			st_insert(t,&stack[sp],pc,tmpushort);
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
		case OP_SET_PARAM:
			tmpushort = *((ushort*)pc);
			pc += sizeof(ushort);

			pc += tmpushort;
			break;
		case OP_FUNCTION_REF:
			tmpuint = *((uint*)pc);
			pc += sizeof(uint);


			break;
		case OP_LOAD_PARAM:
			tmpushort = *((ushort*)pc);
			pc += sizeof(ushort);
			tmpptr = *param;
			if(st_move(&tmpptr,pc,tmpushort))
				st_empty(t,&tmpptr);
			stack[++sp] = tmpptr;
			pc += tmpushort;
			break;
		case OP_DEF_VAR:
			st_empty(t,&stack[++sp]);
			stack[++sp] = stack[sp];
			break;
		case OP_DEF_VAR_REF:
			st_empty(t,&stack[++sp]);
			stack[++sp].pg = (page_wrap*)writer;
			writer = &stack[sp - 1];
			stack[++sp] = *writer;
			break;
		case OP_VAR_POP:
			sp -= 2;
			break;
		case OP_VAR_REF:
			tmpushort = *((ushort*)pc);
			pc += sizeof(ushort);
			stack[++sp].pg = (page_wrap*)writer;
			stack[++sp] = stack[tmpushort];
			writer = &stack[tmpushort];
			break;
		case OP_LOAD_VAR:
			tmpushort = *((ushort*)pc);
			pc += sizeof(ushort);
			stack[++sp] = stack[tmpushort];
			break;
		case OP_VAR_READ:
			tmpushort = *((ushort*)pc);
			pc += sizeof(ushort);
			tmpcharptr = st_get_all(&stack[tmpushort],&tmpuint);
			if(st_move(&stack[sp],tmpcharptr,tmpuint))
				;
			tk_mfree(tmpcharptr);
			break;
		case OP_VAR_WRITE:
			tmpushort = *((ushort*)pc);
			pc += sizeof(ushort);
			tmpcharptr = st_get_all(&stack[tmpushort],&tmpuint);
			stack[++sp] = stack[sp];
			st_insert(t,&stack[sp],tmpcharptr,tmpuint);
			tk_mfree(tmpcharptr);
			break;
		default:
			return(__LINE__);
		}
	}
}
