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

/*
	stack-strukture:
	var-space
	stack-space

	runtime:
	[0] <- assign to: st_ptr OR output-identifier
	[...] <- values
*/
// code-header
static struct _body_
{
	char body;
	uchar maxparams;
	uchar maxvars;
	uchar maxstack;
	ushort codesize;
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
	case OP_SETP:
		return "OP_SETP";
	case OP_DOCALL_N:
		return "OP_DOCALL_N";
	case OP_DOCALL:
		return "OP_DOCALL";
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
	case OP_BODY:
		return "OP_BODY";
	case OP_STR:
		return "OP_STR";
	case OP_CALL:
		return "OP_CALL";
	case OP_POP:
		return "OP_POP";
	case OP_FUN:
		return "OP_FUN";
	case OP_FREE:
		return "OP_FREE";
	case OP_AVARS:
		return "OP_AVARS";
	case OP_OVARS:
		return "OP_OVARS";
	case OP_CAT:
		return "OP_CAT";
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
	case OP_LNUM:
		return "OP_LNUM";

	default:
		return "OP_ILLEGAL";
	}
}

static void _rt_dump_function(st_ptr app, st_ptr* root)
{
	st_ptr strings,tmpptr;
	char* bptr,*bptr2;
	int len,tmpint;
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
		printf("\nCodesize %d, Params %d, Vars %d, Stacksize: %d\n",body.codesize,body.maxparams,body.maxvars,body.maxstack);
	}

	while(len > 0)
	{
		uint opc = *bptr;
		printf("%04d  ",(uint)bptr - (uint)bptr2);
		len--;
		bptr++;

		switch(opc)
		{
		case OP_NOOP:
		case OP_DOCALL_N:
		case OP_DOCALL:
		case OP_POP:
		case OP_WIDX:
		case OP_OUT:
		case OP_OUTL:
		case OP_CONF:
		case OP_RIDX:
		case OP_FUN:
		case OP_CAT:
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
		case OP_CAV:
		case OP_LNUM:
			// emit0
			printf("%s\n",_rt_opc_name(opc));
			break;
		case OP_END:
			// emit0
			puts("OP_END\nEND_OF_FUNCTION\n");
			if(len != 0)
				printf("!!! Remaining length: %d\n",len);
			tk_mfree(bptr2);
			return;

		case OP_CALL:
		case OP_DMVW:
		case OP_MVW:
		case OP_MV:
		case OP_CMV:
		case OP_FMV:
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
		case OP_CVAR:
		case OP_LVAR:
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
				printf("%-10s (%d) %s\n",_rt_opc_name(opc),tmpushort,str + HEAD_SIZE);
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

		case OP_BZ:
		case OP_BR:
			// emit Is (branch forward)
			tmpushort = *((ushort*)bptr);
			bptr += sizeof(ushort);
			len -= sizeof(ushort);
			printf("%-10s %04d\n",_rt_opc_name(opc),tmpushort + (uint)bptr - (uint)bptr2);
			break;

		case OP_LOOP:
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
			err(__LINE__);
			tk_mfree(bptr2);
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

	if(st_get(&root,head,sizeof(head)) <= 0 && head[0] == 0)
	{
		switch(head[1])
		{
		case 'E':
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
			printf("Can't read this type - use functions. Type: %c\n",head[1]);
		}
	}
	else
		_cle_read(&rroot,0);

	return 0;
}

// -------------- RUNTIME -------------------------------------------------------

union _rt_stack;

struct _rt_function
{
	uchar* code;
	st_ptr root;
	st_ptr strings;
	ulong  ref_count;
	ushort max_stack;
	uchar  max_params;
	uchar  max_vars;
};

// !! SIZE_OF_CALL must give size of this
// struct in terms of _rt_stack-elements
struct _rt_invocation
{
	struct _rt_invocation* parent;
	struct _rt_invocation* pipe_fwrd;
	struct _rt_function* fun;
	union _rt_stack* vars;
	union _rt_stack* sp;
	st_ptr context;
	uchar* ip;
};

struct _rt_s_int
{
	int value;
	ulong  _fill;
};

struct _rt_s_assign0
{
	uchar* ip_list;
	ushort zero;
	uchar  length;
	uchar  current;
};

struct _rt_s_assign1
{
	struct _rt_invocation* inv;
	ushort zero;
	uchar  id;
	uchar  depth;
};

struct _rt_s_assign_one
{
	union _rt_stack* var;
	ulong _fill;
};

struct _rt_s_check
{
	void*  part_pointer;
	ushort is_ptr;		// place-of-key (if == 0 => special type: see _rt_stack)
	uchar  type;		// if not key: What type
	uchar  data;		// other stuff
};

enum _rt_s_types
{
	STACK_NULL = 0,
	STACK_PTR,
	STACK_INT,
	STACK_ASG,
	STACK_ASG_ONE
};

union _rt_stack
{
	struct _rt_s_check chk;
	struct _rt_s_assign1 asgn1;
	struct _rt_s_assign0 _asgn0;
	struct _rt_s_assign_one asgn_one;
	struct _rt_s_int sint;
	st_ptr ptr;
};

static uint _rt_get_type(union _rt_stack* sp)
{
	if(sp->chk.is_ptr)
		return STACK_PTR;
	return sp->chk.type;
}

static uint _rt_load_function(st_ptr root, struct _rt_function* fun)
{
	char head[HEAD_SIZE];

	if(st_get(&root,head,sizeof(head)) > 0)
		return __LINE__;

	if(head[0] != 0 && (head[1] != 'F' || head[1] != 'E'))
		return __LINE__;

	fun->ref_count = 1;
	fun->root = root;
	fun->strings = root;

	if(st_move(&fun->strings,"S",2))
		return __LINE__;

	if(st_move(&root,"B",2))
		return __LINE__;
	else
	{
		struct _body_ body;
		if(st_get(&root,(char*)&body,sizeof(struct _body_)) != -2)
			return __LINE__;

		if(body.body != OP_BODY)
			return __LINE__;

		fun->code = (char*)tk_malloc(body.codesize - sizeof(struct _body_));
		if(st_get(&root,fun->code,body.codesize - sizeof(struct _body_)) != -1)
		{
			tk_mfree(fun->code);
			return __LINE__;
		}

		fun->max_params = body.maxparams;
		fun->max_vars   = body.maxvars;
		fun->max_stack  = body.maxstack;
	}
	return 0;
}

static void _rt_unref_function(struct _rt_function* fun)
{
	fun->ref_count--;
}

static struct _rt_invocation* _rt_free_invocation(struct _rt_invocation* inv)
{
	tk_mfree(inv->vars);

	_rt_unref_function(inv->fun);

	return inv->parent;
}

static uint _rt_load_value(union _rt_stack* sp)
{
}

static void _rt_free_se(union _rt_stack* sp, uint count)
{
	// set all to type:stack_null
	memset(sp,0,sizeof(union _rt_stack) * count);
}

static uint _rt_compare(union _rt_stack* sp)
{
	union _rt_stack* sp2 = sp - 1;
	int type = _rt_get_type(sp);

	if(type != _rt_get_type(sp2))
		return -2;	// must be same type

	switch(type)
	{
	case STACK_NULL:
		return 0;	// same allways
	case STACK_INT:
		if(sp->sint.value == sp2->sint.value) return 0;
		return (sp->sint.value > sp2->sint.value)? 1 : -1;
	case STACK_PTR:
		if(sp->ptr.pg == sp2->ptr.pg &&
			sp->ptr.key == sp2->ptr.key &&
			sp->ptr.offset == sp2->ptr.offset)
			return 0;
		// compare content
		break;
	}
	return -3;	// uncomparable types
}

static void _rt_make_int(union _rt_stack* sp, int value)
{
	sp->chk.is_ptr = 0;
	sp->chk.type = STACK_INT;
	sp->sint.value = value;
}

static void _rt_make_assign_one(union _rt_stack* sp, union _rt_stack* var)
{
	sp->chk.is_ptr = 0;
	sp->chk.type = STACK_ASG_ONE;
	sp->asgn_one.var = var;
}

static void _rt_make_null(union _rt_stack* sp)
{
	memset(sp,0,sizeof(union _rt_stack));
}

static uint _rt_invoke(struct _rt_invocation* inv, task* t, st_ptr* config)
{
	int tmpint;
	ushort tmpushort;
	uchar  tmpuchar,tmpuchar2;
	// main interpreter-loop. The "CPU"
	while(1)
	switch(*inv->ip++)
	{
	case OP_NOOP:
		break;
	case OP_POP:
		inv->sp--;
		break;
	case OP_CONF:
		inv->sp++;
		inv->sp->ptr = *config;
		break;
	case OP_FUN:
		inv->sp++;
		inv->sp->ptr = inv->context;
		break;
	case OP_LNUM:
		if(_rt_get_type(inv->sp) != STACK_INT)
		{
			tmpint = _rt_load_value(inv->sp);
			if(tmpint) return tmpint;
			if(_rt_get_type(inv->sp) != STACK_INT) return __LINE__;
		}
		break;
	case OP_ADD:
		(inv->sp - 1)->sint.value += inv->sp->sint.value;
		inv->sp--;
		break;
	case OP_SUB:
		(inv->sp - 1)->sint.value -= inv->sp->sint.value;
		inv->sp--;
		break;
	case OP_MUL:
		(inv->sp - 1)->sint.value *= inv->sp->sint.value;
		inv->sp--;
		break;
	case OP_DIV:
		(inv->sp - 1)->sint.value /= inv->sp->sint.value;
		inv->sp--;
		break;
	case OP_REM:
		(inv->sp - 1)->sint.value %= inv->sp->sint.value;
		inv->sp--;
		break;
	case OP_EQ:
		tmpint = _rt_compare(inv->sp);
		inv->sp--;
		_rt_make_int(inv->sp,tmpint == 0);
		break;
	case OP_NE:
		tmpint = _rt_compare(inv->sp);
		inv->sp--;
		_rt_make_int(inv->sp,tmpint != 0);
		break;
	case OP_GE:
		tmpint = _rt_compare(inv->sp);
		inv->sp--;
		if(tmpint < -1) return __LINE__;
		_rt_make_int(inv->sp,tmpint >= 0);
		break;
	case OP_GT:
		tmpint = _rt_compare(inv->sp);
		inv->sp--;
		if(tmpint < -1) return __LINE__;
		_rt_make_int(inv->sp,tmpint > 0);
		break;
	case OP_LE:
		tmpint = _rt_compare(inv->sp);
		inv->sp--;
		if(tmpint < -1) return __LINE__;
		_rt_make_int(inv->sp,tmpint <= 0);
		break;
	case OP_LT:
		tmpint = _rt_compare(inv->sp);
		inv->sp--;
		if(tmpint < -1) return __LINE__;
		_rt_make_int(inv->sp,tmpint < 0);
		break;

	case OP_END:
		// clean-up
		{
			union _rt_stack tmp = *inv->sp;
			_rt_free_se(inv->vars,inv->fun->max_vars);

			inv = _rt_free_invocation(inv);
			if(inv == 0)
				return 0;

			// return to parent
			inv->sp -= SIZE_OF_CALL;	// pop inv-element
			if(nest_call)
			{
				inv->sp++;
				*inv->sp = tmp;
			}
		}
		break;
	case OP_CALL:
		tmpushort = *((ushort*)inv->ip);
		inv->ip += sizeof(ushort);
		{
			struct _rt_invocation* cl = (struct _rt_invocation*)inv->sp;
			st_ptr tmpptr = inv->sp->ptr;
			cl->context = tmpptr;

			if(st_move(&tmpptr,inv->ip,tmpushort))
				return __LINE__;

			cl->fun = (struct _rt_function*)tk_malloc(sizeof(struct _rt_function));

			tmpint = _rt_load_function(tmpptr,cl->fun);
			if(tmpint)
			{
				tk_mfree(cl->fun);
				return tmpint;
			}

			cl->vars = tk_malloc((cl->fun->max_vars + cl->fun->max_stack) * sizeof(union _rt_stack));
			memset(cl->vars,0,cl->fun->max_vars * sizeof(union _rt_stack));

			cl->sp = cl->vars + cl->fun->max_vars;
			cl->ip = cl->fun->code;
			cl->parent = inv;
			cl->pipe_fwrd = 0;
			inv->sp += SIZE_OF_CALL - 1;
		}
		break;
	case OP_DOCALL_N:
		{
			struct _rt_invocation* cl = (struct _rt_invocation*)(inv->sp - SIZE_OF_CALL);
			st_empty(t,&cl->sp->ptr);
			inv->pipe_fwrd = cl;
			inv = cl;
		}
		break;
	case OP_DOCALL:
		{
			struct _rt_invocation* cl = (struct _rt_invocation*)(inv->sp - SIZE_OF_CALL);
			cl->sp = (inv->sp - SIZE_OF_CALL - 1);
			inv->pipe_fwrd = cl;
			inv = cl;
		}
		break;

	case OP_DMVW:
		inv->sp++;
		inv->sp = (inv->sp - 1);
	case OP_MVW:
		tmpushort = *((ushort*)inv->ip);
		inv->ip += sizeof(ushort);
		st_insert(t,&inv->sp->ptr,inv->ip,tmpushort);
		inv->ip += tmpushort;
		break;
	case OP_CMV:
		inv->sp++;
		inv->sp->ptr = *config;
		tmpushort = *((ushort*)inv->ip);
		inv->ip += sizeof(ushort);
		if(st_move(&inv->sp->ptr,inv->ip,tmpushort))
			_rt_make_null(inv->sp);
		inv->ip += tmpushort;
		break;
	case OP_FMV:
		inv->sp++;
		inv->sp->ptr = inv->context;
	case OP_MV:
		tmpushort = *((ushort*)inv->ip);
		inv->ip += sizeof(ushort);
		if(st_move(&inv->sp->ptr,inv->ip,tmpushort))
			_rt_make_null(inv->sp);
		inv->ip += tmpushort;
		break;

	case OP_SETP:
		{
			struct _rt_invocation* cl = (struct _rt_invocation*)(inv->sp - SIZE_OF_CALL);
			tmpuchar = *inv->ip++;
			if(tmpuchar < cl->fun->max_params)
				cl->vars[tmpuchar] = *inv->sp;
			else
				return __LINE__;
			inv->sp--;
		}
		break;

	case OP_RIDX:
	case OP_WIDX:
	case OP_OUT:
	case OP_OUTL:
		break;
	case OP_CAT:
		break;
	case OP_CAV:
		// emit0
		break;

	case OP_WVAR:
	case OP_WVAR0:
	case OP_RVAR:
	case OP_CVAR:
		break;
	case OP_LVAR:
		inv->sp++;
		*inv->sp = inv->vars[*inv->ip++];
		break;

	case OP_AVARS:
	case OP_OVARS:
		// emit Ic
		//tmpuchar = *bptr++;
		//printf("%-10s %d {",_rt_opc_name(opc),tmpuchar);
		//while(tmpuchar-- > 0)
		//{
		//	printf("%d ",*bptr++);
		//	len--;
		//}
		//puts("}");
		//len--;
		break;

	case OP_STR:
		// emit Is
		inv->sp++;
		inv->sp->ptr = inv->fun->strings;
		if(st_move(&inv->sp->ptr,inv->ip,sizeof(ushort)))
			return __LINE__;
		inv->ip += sizeof(ushort);
		break;

	case OP_DEFP:
		// emit Is2 (branch forward)
		tmpuchar  = *inv->ip++;
		tmpushort = *((ushort*)inv->ip);
		inv->ip += sizeof(ushort);

		if(_rt_get_type(inv->sp) == STACK_NULL)
		{
			// setup assign to var[tmpuchar]
			inv->sp++;
			_rt_make_assign_one(inv->sp,inv->vars + tmpuchar);
		}
		else
			inv->ip += tmpushort;
		break;

	case OP_BZ:
		// emit Is (branch forward conditional)
		tmpushort = *((ushort*)inv->ip);
		inv->ip += sizeof(ushort);
		tmpuchar = _rt_get_type(inv->sp);

		if(tmpuchar == STACK_NULL ||
			(tmpuchar == STACK_INT && inv->sp->sint.value == 0) ||
			(tmpuchar == STACK_PTR && st_is_empty(&inv->sp->ptr)))
			inv->ip += tmpushort;
		break;
	case OP_BR:
		// emit Is (branch forward)
		tmpushort = *((ushort*)inv->ip);
		inv->ip += sizeof(ushort);
		inv->ip += tmpushort;
		break;

	case OP_LOOP:
		// emit Is (branch back)
		tmpushort = *((ushort*)inv->ip);
		inv->ip += sizeof(ushort);
		inv->ip -= tmpushort;
		break;

	case OP_FREE:
		// emit Ic2 (byte,byte)
		tmpuchar = *inv->ip++;
		tmpuchar2 = *inv->ip++;
		_rt_free_se(inv->vars + tmpuchar2,tmpuchar);
		break;

	case OP_IMM:
		// emit II (imm int)
		inv->sp++;
		_rt_make_int(inv->sp,*((int*)inv->ip));
		inv->ip += sizeof(int);
		break;
	default:
		return __LINE__;
	}
}

int rt_do_call(task* t, st_ptr* app, st_ptr* root, st_ptr* fun, st_ptr* param)
{
	struct _rt_invocation inv;

	return 0;
}
