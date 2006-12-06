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
#include "cle_struct.h"

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
	case OP_CALL_N:
		return "OP_CALL_N";
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
	case OP_NULL:
		return "OP_NULL";

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
		case OP_DOCALL:
		case OP_POP:
		case OP_POPW:
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
		case OP_LNUM:
		case OP_NULL:
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
		case OP_CALL_N:
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
		case OP_CAV:
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

static union _rt_stack;

static struct _rt_function
{
	uchar* code;
	st_ptr root;
	st_ptr strings;
	ulong  ref_count;
	ushort max_stack;
	uchar  max_params;
	uchar  max_vars;
};

static struct _rt_invocation
{
	struct _rt_invocation* parent;
	struct _rt_invocation* pipe_fwrd;
	struct _rt_function* fun;
	union _rt_stack* vars;
	union _rt_stack* sp;
	st_ptr context;
	uchar* ip;
};

static struct _rt_s_int
{
	int value;
	ulong  _fill;
};

static struct _rt_s_inv_ref
{
	struct _rt_invocation* inv;
	ulong  _fill;
};

static struct _rt_s_assign_ref
{
	union _rt_stack* var;
	ulong _fill;
};

static struct _rt_s_assign_many
{
	struct _rt_s_assign_ref* ref;
	ushort _fill;
	uchar  _type;
	uchar  remaining_depth;
};

static struct _rt_value
{
	uchar* data;
	uint   length;
	uint   ref;
};

static struct _rt_s_value
{
	struct _rt_value* value;
	ulong _fill;
};

static struct _rt_s_check
{
	void*  part_pointer;
	ushort is_ptr;		// place-of-key (if == 0 => special type: see _rt_stack)
	uchar  type;		// if not key: What type
	uchar  data;		// other stuff
};

static enum _rt_s_types
{
	STACK_NULL = 0,
	STACK_PTR,
	STACK_INT,
	STACK_REF,
	STACK_INV,
	STACK_MANY,
	STACK_DIRECT_OUT,
	STACK_STR_INT
};

static union _rt_stack
{
	struct _rt_s_assign_many many;
	struct _rt_s_assign_ref ref;
	struct _rt_s_inv_ref inv;
	struct _rt_s_value value;
	struct _rt_s_check chk;
	struct _rt_s_int sint;
	st_ptr ptr;
};

static void _rt_make_int(union _rt_stack* sp, int value)
{
	sp->chk.is_ptr = 0;
	sp->chk.type = STACK_INT;
	sp->sint.value = value;
}

static void _rt_make_assign_ref(union _rt_stack* sp, union _rt_stack* var)
{
	sp->ref.var = var;
	sp->chk.is_ptr = 0;
	sp->chk.type = STACK_REF;
}

static void _rt_make_inv_ref(union _rt_stack* sp, struct _rt_invocation* inv)
{
	sp->inv.inv = inv;
	sp->chk.is_ptr = 0;
	sp->chk.type = STACK_INV;
}

static void _rt_make_null(union _rt_stack* sp)
{
	memset(sp,0,sizeof(union _rt_stack));
}

static uint _rt_get_type(union _rt_stack* sp)
{
	if(sp->chk.is_ptr)
		return STACK_PTR;
	return sp->chk.type;
}

static uint _rt_load_function(task* t, st_ptr root, struct _rt_function** ret_fun)
{
	struct _rt_function* fun;
	st_ptr strings;
	char head[HEAD_SIZE];
	*ret_fun = 0;

	// TODO: read functions-from cache....
	if(st_get(&root,head,sizeof(head)) > 0)
		return __LINE__;

	if(head[0] != 0 && (head[1] != 'F' || head[1] != 'E'))
		return __LINE__;

	strings = root;
	if(st_move(&strings,"S",2))
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

		fun = (struct _rt_function*)tk_malloc(
			sizeof(struct _rt_function) + body.codesize - sizeof(struct _body_));

		fun->ref_count = 0;
		*ret_fun = fun;

		fun->code = (char*)fun + sizeof(struct _rt_function);
		if(st_get(&root,fun->code,body.codesize - sizeof(struct _body_)) != -1)
			return __LINE__;

		fun->max_params = body.maxparams;
		fun->max_vars   = body.maxvars;
		fun->max_stack  = body.maxstack;
		fun->strings    = strings;
		fun->root       = root;
	}
	return 0;
}

static void _rt_release_fun(struct _rt_function* fun)
{
	if(fun == 0) return;

	if(fun->ref_count == 0)
		tk_mfree(fun);
	else
		fun->ref_count--;
}

static uint _rt_create_invocation(task* t, st_ptr context, st_ptr root, struct _rt_invocation** ret_inv)
{
	struct _rt_invocation* inv;
	struct _rt_function* fun;
	uint ret = _rt_load_function(t,root,&fun);
	if(ret)
	{
		_rt_release_fun(fun);
		return ret;
	}
	
	// setup-invocation
	inv = (struct _rt_invocation*)
		tk_malloc(sizeof(struct _rt_invocation) + (fun->max_vars + fun->max_stack) * sizeof(union _rt_stack));

	inv->vars = (union _rt_stack*)((char*)inv + sizeof(struct _rt_invocation));

	memset(inv->vars,0,fun->max_vars * sizeof(union _rt_stack));
	inv->context = context;
	inv->sp = inv->vars + inv->fun->max_vars;
	inv->ip = fun->code;
	inv->parent = 0;
	inv->pipe_fwrd = 0;
	return 0;
}

static struct _rt_invocation* _rt_free_invocation(struct _rt_invocation* inv)
{
	struct _rt_invocation* parent = inv->parent;
	
	_rt_release_fun(inv->fun);

	tk_mfree(inv);

	return parent;
}

static uint _rt_load_value(task* t, union _rt_stack* sp, uint stype)
{
	st_ptr* ptr;
	char head[HEAD_SIZE];

	ptr = &sp->ptr;
	if(st_get(ptr,head,sizeof(head)) <= 0 && head[0] == 0)
	{
		switch(head[1])
		{
		case 'E':
			{
				struct _rt_invocation* ret_inv;
				_rt_create_invocation(t,*ptr,*ptr,&ret_inv);
			}
			break;
		case 'I':
			{
				int tmp;
				if(st_get(ptr,(char*)&tmp,sizeof(int)) == 0)
				{
				}
				else
					return __LINE__;
			}
			break;
		case 'S':
			{
				uint len;
				char* data = st_get_all(ptr,&len);
			}
			break;
		default:
			return __LINE__;
		}
	}

	return 0;
}

static void _rt_free_value(union _rt_stack* sp)
{
	if(sp->value.value->ref == 0)
	{
		tk_mfree(sp->value.value);
	}
	else
		sp->value.value->ref--;
}

static void _rt_free_se(union _rt_stack* sp, uint count)
{
	// set all to type:stack_null
	memset(sp,0,sizeof(union _rt_stack) * count);
}

static uint _rt_compare(task* t, union _rt_stack* sp)
{
	union _rt_stack* sp2 = sp - 1;
	uint type = _rt_get_type(sp);

	if(type != _rt_get_type(sp2))
		return -2;	// must be same type

	switch(type)
	{
	case STACK_NULL:
		return 0;	// same always
	case STACK_INT:
		if(sp->sint.value == sp2->sint.value) return 0;
		return (sp->sint.value > sp2->sint.value)? 1 : -1;
	case STACK_PTR:
		if(sp->ptr.pg == sp2->ptr.pg &&
			sp->ptr.key == sp2->ptr.key &&
			sp->ptr.offset == sp2->ptr.offset)
			return 0;
		// compare content
		if(_rt_load_value(t,sp,STACK_STR_INT))
		{
			_rt_free_value(sp);
			return -4;
		}
		if(_rt_load_value(t,sp2,STACK_STR_INT))
		{
			_rt_free_value(sp);
			_rt_free_value(sp2);
			return -5;
		}

		type = (sp->value.value->length > sp2->value.value->length)?
			sp2->value.value->length : sp->value.value->length;
		type = memcmp(sp->value.value->data,sp2->value.value->data,type);

		_rt_free_value(sp);
		_rt_free_value(sp2);
		if(type == 0) return 0;
		if(type < 0) return -1;
		return 1;
	}
	return -3;	// uncomparable types
}

static uint _rt_insert(task* t, st_ptr* tmpptr, union _rt_stack* sp, cdat name, const uint length, const uchar push)
{
	int tmpint;
	switch(_rt_get_type(sp))
	{
	case STACK_PTR:
		*tmpptr = sp->ptr;
		st_insert(t,tmpptr,name,length);
		break;
	case STACK_REF:
		if(_rt_get_type(sp->ref.var) == STACK_NULL)
			st_empty(t,&sp->ref.var->ptr);
		*tmpptr = sp->ref.var->ptr;
		st_insert(t,tmpptr,name,length);
		break;
	case STACK_MANY:
		if(_rt_get_type(sp->many.ref->var) == STACK_NULL)
			st_empty(t,&sp->many.ref->var->ptr);
		*tmpptr = sp->many.ref->var->ptr;
		st_insert(t,tmpptr,name,length);
		break;
	case STACK_DIRECT_OUT:
		// gen. name-event
		tmpint = t->output->name(t,name,length);
		if(tmpint) return tmpint;
		if(push)
		{
			tmpint = t->output->push(t);
			if(tmpint) return tmpint;
		}
		tmpptr->key = 0;
		break;
	default:
		return __LINE__;
	}
	return 0;
}

static uint _rt_do_out(task* t, union _rt_stack* to, union _rt_stack* from)
{
	st_ptr* ptr;
	uint ret = _rt_load_value(t,from,STACK_STR_INT);

	switch(_rt_get_type(to))
	{
	case STACK_PTR:
		ptr = &to->ptr;
		break;
	case STACK_REF:
		if(_rt_get_type(to->ref.var) == STACK_NULL)
			st_empty(t,&to->ref.var->ptr);
		ptr = &to->ref.var->ptr;
		break;
	case STACK_MANY:
		if(_rt_get_type(to->many.ref->var) == STACK_NULL)
			st_empty(t,&to->many.ref->var->ptr);
		ptr = &to->many.ref->var->ptr;
		break;
	case STACK_DIRECT_OUT:
		return t->output->data(t,from->value.value->data,from->value.value->length);
	default:
		return __LINE__;
	}

	st_append(t,ptr,from->value.value->data,from->value.value->length);
	return 0;
}

static uint _rt_do_out_tree(task* t, st_ptr* root)
{
	it_ptr it;
	st_ptr pt;
	uint  ret;

	it_create(&it,root);
	while(it_next(&pt,&it))
	{
		ret = t->output->name(t,it.kdata,it.kused);
		if(ret) break;
		ret = t->output->push(t);
		if(ret) break;

		ret = _rt_do_out_tree(t,&pt);
		if(ret) break;
	}

	it_dispose(&it);
	return ret? ret : t->output->pop(t);
}

static uint _rt_do_copy(task* t, st_ptr* to, st_ptr* root)
{
	it_ptr it;
	st_ptr pt;
	uint ret = 0;

	it_create(&it,root);
	while(it_next(&pt,&it))
	{
		st_ptr tto = *to;
		st_insert(t,&tto,it.kdata,it.kused);

		ret = _rt_do_copy(t,&tto,&pt);
		if(ret) break;
	}

	it_dispose(&it);
	return ret;
}

static uint _rt_do_concat(task* t, struct _rt_value* result, union _rt_stack* cat)
{
	return __LINE__;
}

static uint _rt_invoke(struct _rt_invocation* inv, task* t, st_ptr* config)
{
	st_ptr tmpptr;
	int tmpint;
	ushort tmpushort;
	uchar  tmpuchar,tmpuchar2;
	// main interpreter-loop. The "CPU"
	while(1)
	switch(*inv->ip++)
	{
	case OP_NOOP:
		break;
	case OP_NULL:
		inv->sp++;
		_rt_make_null(inv->sp);
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
		// force to STACK_INT
		switch(_rt_get_type(inv->sp))
		{
		case STACK_INT:
			break;
		case STACK_PTR:
			{
				char head[HEAD_SIZE];
				st_ptr* ptr = &inv->sp->ptr;

				if(st_get(ptr,head,sizeof(head)) > 0 || head[0] != 0)
					return __LINE__;

				switch(head[1])
				{
				case 'E':
					{
						struct _rt_invocation* ret_inv;
						_rt_create_invocation(t,*ptr,*ptr,&ret_inv);
					}
					break;
				case 'I':
					{
						int tmp;
						if(st_get(ptr,(char*)&tmp,sizeof(int)) != 0)
							return __LINE__;

						_rt_make_int(inv->sp,tmp);
					}
					break;
				default:
					return __LINE__;
				}
			}
			break;
		default:
			return __LINE__;
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
		tmpint = _rt_compare(t,inv->sp);
		inv->sp--;
		_rt_make_int(inv->sp,tmpint == 0);
		break;
	case OP_NE:
		tmpint = _rt_compare(t,inv->sp);
		inv->sp--;
		_rt_make_int(inv->sp,tmpint != 0);
		break;
	case OP_GE:
		tmpint = _rt_compare(t,inv->sp);
		inv->sp--;
		if(tmpint < -1) return __LINE__;
		_rt_make_int(inv->sp,tmpint >= 0);
		break;
	case OP_GT:
		tmpint = _rt_compare(t,inv->sp);
		inv->sp--;
		if(tmpint < -1) return __LINE__;
		_rt_make_int(inv->sp,tmpint > 0);
		break;
	case OP_LE:
		tmpint = _rt_compare(t,inv->sp);
		inv->sp--;
		if(tmpint < -1) return __LINE__;
		_rt_make_int(inv->sp,tmpint <= 0);
		break;
	case OP_LT:
		tmpint = _rt_compare(t,inv->sp);
		inv->sp--;
		if(tmpint < -1) return __LINE__;
		_rt_make_int(inv->sp,tmpint < 0);
		break;

	case OP_END:
		// clean-up
		_rt_free_se(inv->vars,inv->fun->max_vars);

		// return to parent
		inv = _rt_free_invocation(inv);
		if(inv == 0)
			return 0;

		inv->pipe_fwrd = 0;
		break;
	case OP_CALL_N:
		*(inv->sp + 2) = *inv->sp;	// dub
		_rt_make_null(inv->sp++);	// blank
		_rt_make_assign_ref(inv->sp,inv->sp - 1);
		inv->sp++;
	case OP_CALL:
		{
			struct _rt_invocation* cl;
			tmpptr = inv->sp->ptr;
			tmpushort = *((ushort*)inv->ip);
			inv->ip += sizeof(ushort);

			if(tmpptr.key == 0 || st_move(&tmpptr,inv->ip,tmpushort))
				return __LINE__;

			tmpint = _rt_create_invocation(t,inv->sp->ptr,tmpptr,&cl);
			cl->parent = inv;
			*cl->sp = *(inv->sp - 1);	// copy assign-to

			_rt_make_inv_ref(inv->sp,cl);
		}
		break;
	case OP_DOCALL:
		{
			struct _rt_invocation* cl = inv->sp->inv.inv;
			inv->pipe_fwrd = cl;
			inv->sp--;
			inv = cl;
		}
		break;
	case OP_SETP:
		{
			struct _rt_invocation* cl = (inv->sp - 1)->inv.inv;
			tmpuchar = *inv->ip++;
			if(tmpuchar < cl->fun->max_params)
				cl->vars[tmpuchar] = *inv->sp;
			else
				return __LINE__;
			inv->sp--;
		}
		break;

	case OP_POPW:
		if(_rt_get_type(inv->sp) == STACK_DIRECT_OUT)
		{
			// gen. pop-event.
			tmpint = t->output->pop(t);
			if(tmpint) return tmpint;
		}
		else
			inv->sp--;
		break;
	case OP_DMVW:
		tmpushort = *((ushort*)inv->ip);
		inv->ip += sizeof(ushort);
		tmpint = _rt_insert(t,&tmpptr,inv->sp,inv->ip,tmpushort,1);
		if(tmpint) return tmpint;
		if(tmpptr.key)
		{
			st_insert(t,&tmpptr,inv->ip,tmpushort);
			inv->sp++;
			inv->sp->ptr = tmpptr;
		}
		inv->ip += tmpushort;
		break;
	case OP_MVW:
		tmpushort = *((ushort*)inv->ip);
		inv->ip += sizeof(ushort);
		switch(_rt_get_type(inv->sp))
		{
		case STACK_PTR:
			st_insert(t,&inv->sp->ptr,inv->ip,tmpushort);
			break;
		case STACK_DIRECT_OUT:
			// gen. name-event
			tmpint = t->output->name(t,inv->ip,tmpushort);
			if(tmpint) return tmpint;
			break;
		default:
			return __LINE__;
		}
		inv->ip += tmpushort;
		break;
	case OP_WIDX:
		tmpint = _rt_load_value(t,inv->sp,STACK_STR_INT);
		if(tmpint) return tmpint;
		tmpint = _rt_insert(t,&tmpptr,inv->sp - 1,inv->sp->value.value->data,
			inv->sp->value.value->length,0);
		if(tmpint) return tmpint;
		_rt_free_value(inv->sp);
		inv->sp--;
		break;
	case OP_WVAR:
		tmpuchar = *inv->ip++;
		tmpint = _rt_load_value(t,inv->vars + tmpuchar,STACK_STR_INT);
		if(tmpint) return tmpint;
		tmpint = _rt_insert(t,&tmpptr,inv->sp,(inv->vars + tmpuchar)->value.value->data,
			(inv->vars + tmpuchar)->value.value->length,1);
		if(tmpint) return tmpint;
		if(tmpptr.key)
		{
			inv->sp++;
			inv->sp->ptr = tmpptr;
		}
		_rt_free_value(inv->vars + tmpuchar);
		break;
	case OP_WVAR0:
		tmpuchar = *inv->ip++;
		tmpint = _rt_load_value(t,inv->vars + tmpuchar,STACK_STR_INT);
		if(tmpint) return tmpint;
		tmpint = _rt_insert(t,&tmpptr,inv->sp,(inv->vars + tmpuchar)->value.value->data,
			(inv->vars + tmpuchar)->value.value->length,0);
		if(tmpint) return tmpint;
		_rt_free_value(inv->vars + tmpuchar);
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
		tmpushort = *((ushort*)inv->ip);
		inv->ip += sizeof(ushort);
		if(st_move(&inv->sp->ptr,inv->ip,tmpushort))
			_rt_make_null(inv->sp);
		inv->ip += tmpushort;
		break;
	case OP_MV:
		tmpushort = *((ushort*)inv->ip);
		inv->ip += sizeof(ushort);
		if(inv->sp->chk.is_ptr == 0 || st_move(&inv->sp->ptr,inv->ip,tmpushort))
			_rt_make_null(inv->sp);
		inv->ip += tmpushort;
		break;
	case OP_RIDX:
		tmpint = _rt_load_value(t,inv->sp,STACK_STR_INT);
		if(tmpint) return tmpint;
		if(st_move(&(inv->sp - 1)->ptr,inv->sp->value.value->data,inv->sp->value.value->length))
			_rt_make_null(inv->sp - 1);
		_rt_free_value(inv->sp);
		inv->sp--;
		break;
	case OP_CVAR:
		inv->sp++;
		inv->sp->ptr = *config;
	case OP_RVAR:
		tmpuchar = *inv->ip++;
		tmpint = _rt_load_value(t,inv->vars + tmpuchar,STACK_STR_INT);
		if(tmpint) return tmpint;
		if(st_move(&inv->sp->ptr,inv->sp->value.value->data,inv->sp->value.value->length))
			_rt_make_null(inv->sp);
		_rt_free_value(inv->vars + tmpuchar);
		break;

	case OP_LVAR:
		inv->sp++;
		*inv->sp = inv->vars[*inv->ip++];
		break;
	case OP_AVARS:
		tmpint = tmpuchar = *inv->ip++;
		{
			uchar* list = inv->ip + tmpint - 1;
			while(tmpint-- > 0)
			{
				inv->sp++;
				_rt_make_assign_ref(inv->sp,inv->vars + *list--);
			}
		}
		inv->sp++;
		inv->sp->chk.is_ptr = 0;
		inv->sp->chk.type = STACK_MANY;
		inv->sp->many.remaining_depth = tmpuchar;
		inv->sp->many.ref = &(inv->sp - 1)->ref;
		inv->ip += tmpuchar;
		break;
	case OP_CAV:
		// emitIc
		tmpuchar = *inv->ip++;
		inv->sp -= tmpuchar;
		break;

	case OP_OUTL:
		switch(_rt_get_type(inv->sp - 1))
		{
		case STACK_PTR:	// copy-to
			tmpint = _rt_do_copy(t,&(inv->sp - 1)->ptr,&inv->sp->ptr);
			if(tmpint) return tmpint;
			break;
		case STACK_REF: // set ref
//			??? -nexting
			*(inv->sp - 1)->ref.var = *inv->sp;
			break;
		case STACK_MANY: // set and roll
			if((inv->sp - 1)->many.remaining_depth > 0)
			{
				*(inv->sp - 1)->many.ref->var = *inv->sp;
				(inv->sp - 1)->many.ref--;
				(inv->sp - 1)->many.remaining_depth--;
			}
			else
			{
//				??? -nexting
			}
			break;
		case STACK_DIRECT_OUT: // output
			tmpint = _rt_do_out_tree(t,&inv->sp->ptr);
			if(tmpint) return tmpint;
			break;
		default:
			return __LINE__;
		}
		inv->sp--;
		break;

	case OP_CAT:
		if(_rt_get_type(inv->sp - 1) != STACK_STR_INT)
		{
			tmpint = _rt_load_value(t,inv->sp - 1,STACK_STR_INT);
			if(tmpint) return tmpint;
		}
		tmpint = _rt_do_concat(t,(inv->sp - 1)->value.value,inv->sp);
		if(tmpint) return tmpint;
		inv->sp--;
		break;
	case OP_OUT:
		tmpint = _rt_do_out(t,inv->sp - 1,inv->sp);
		if(tmpint) return tmpint;
		inv->sp--;
		break;
	case OP_OVARS:
		// emit Ic
		tmpint = *inv->ip++;
		{
			uchar* list = inv->ip + tmpint - 1;
			while(tmpint-- > 0)
			{
				uint ret = _rt_do_out(t,inv->sp,inv->vars + *list--);
				if(ret) return ret;
			}
		}
		break;

	case OP_STR:
		// emit Is
		inv->sp++;
		inv->sp->ptr = inv->fun->strings;
		st_move(&inv->sp->ptr,inv->ip,sizeof(ushort));
		inv->ip += sizeof(ushort);
		break;

	case OP_DEFP:
		// emit Is2 (branch forward)
		tmpuchar  = *inv->ip++;
		tmpushort = *((ushort*)inv->ip);
		inv->ip += sizeof(ushort);

		if(_rt_get_type(inv->vars + tmpuchar) == STACK_NULL)
		{
			// setup assign to var[tmpuchar]
			inv->sp++;
			_rt_make_assign_ref(inv->sp,inv->vars + tmpuchar);
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
		inv->sp--;
		break;
	case OP_BR:
		// emit Is (branch forward)
		tmpushort = *((ushort*)inv->ip);
		inv->ip += tmpushort + sizeof(ushort);
		break;

	case OP_LOOP:
		// emit Is (branch back)
		tmpushort = *((ushort*)inv->ip);
		inv->ip -= tmpushort - sizeof(ushort);
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
	struct _rt_invocation* inv;
	// create invocation
	int ret = _rt_create_invocation(t,*root,*fun,&inv);

	if(ret == 0 && inv->fun->max_params != 1)
		ret = __LINE__;

	if(ret)
	{
		_rt_free_invocation(inv);
		return ret;
	}

	// set param-tree
	inv->vars->ptr = *param;
	// mark for output-handler
	inv->sp->chk.type = STACK_DIRECT_OUT;
	inv->sp->chk.is_ptr = 0;
	inv->sp++;

	t->output->start(t);	// begin output
	// .. and invoke
	ret = _rt_invoke(inv,t,app);
	t->output->end(t);		// end output

	_rt_free_invocation(inv);
	return ret;
}
