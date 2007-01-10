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
	case OP_OVAR:
		return "OP_OVAR";
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
		case OP_OVAR:
			// emit Ic
			tmpuchar = *bptr++;
			printf("%-10s %d\n",_rt_opc_name(opc),tmpuchar);
			len--;
			break;

		case OP_AVARS:
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
				printf("%-10s %s\n",_rt_opc_name(opc),str + HEAD_SIZE);
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
static uint _rt_invoke(struct _rt_invocation* inv, task* t, st_ptr* config);

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

static struct _rt_buffer
{
	uchar* data;
	uint   length;
};

static struct _rt_invocation
{
	struct _rt_invocation* parent;
	struct _rt_function* fun;
	union _rt_stack* vars;
	union _rt_stack* sp;
	uchar* ip;
	st_ptr context;
	uint   error;
};

static struct _rt_s_int
{
	int value;
};

static struct _rt_s_inv_ref
{
	struct _rt_invocation* inv;
};

static struct _rt_s_assign_ref
{
	union _rt_stack* var;
	long _fill;
};

static struct _rt_s_assign_many
{
	//struct _rt_s_assign_ref* ref;
	void*  _pfill;
	ushort _fill;
	uchar  _type;
	uchar  remaining_depth;
};

static struct _rt_list
{
	struct _rt_list* last;
	struct _rt_list* next;
};

static struct _rt_s_list
{
	struct _rt_list* list;
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
	STACK_VALUE,
	STACK_LIST,
	STACK_DIRECT_OUT
};

static union _rt_stack
{
	struct _rt_s_assign_many many;
	struct _rt_s_assign_ref ref;
	struct _rt_s_inv_ref inv;
	struct _rt_s_value value;
	struct _rt_s_list list;
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

#define _rt_get_type(sp) ((sp)->chk.is_ptr? STACK_PTR : (sp)->chk.type)

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

static void _rt_free_se(union _rt_stack* sp, uint count)
{
	// set all to type:stack_null
	memset(sp,0,sizeof(union _rt_stack) * count);
}

static struct _rt_invocation* _rt_release_invocation(struct _rt_invocation* inv)
{
	struct _rt_invocation* parent = inv->parent;
	
	// clean-up
	_rt_free_se(inv->vars,inv->fun->max_vars);

	_rt_release_fun(inv->fun);

	tk_mfree(inv);

	return parent;
}

static uint _rt_create_invocation(task* t, st_ptr* context, st_ptr root, struct _rt_invocation** ret_inv)
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
	inv->sp = inv->vars + fun->max_vars;
	inv->context = *context;
	inv->ip = fun->code;
	inv->parent = 0;
	inv->fun = fun;

	*ret_inv = inv;
	return 0;
}

static char _rt_op_error = OP_ERROR;

static uint _rt_sys_error(struct _rt_invocation* inv, const int line)
{
	inv->error = line;
	inv->ip = &_rt_op_error;
	return 1;
}

static uint _rt_err(struct _rt_invocation* inv, const int code)
{
	return code? _rt_sys_error(inv,code) : 0;
}

static void _rt_print_int_hex(int theint, char* output)
{
	int i = sizeof(int) * 2;
	output += i - 1;
	*(output + 1) = '\0';
	while(i-- != 0)
	{
		char d = theint & 0xF;
		*output = d + (d > 9? 'A' - 9: '0');
		theint >>= 4;
		output--;
	}
}

static int _rt_parse_int(char* data, uint length)
{
	int ret = 0;
	while(length-- != 0)
	{
		ret <<= 4;
		if(*data >= '0' && *data <= '9')
			ret += *data - '0';
		else if(*data >= 'A' && *data <= 'F')
			ret += *data - 'A';
		else
			return 0;

		data++;
	}

	return ret;
}

static void _rt_next_list(task* t, union _rt_stack* sp, union _rt_stack* nxt)
{
	struct _rt_list* last;
	union _rt_stack* elm;
	if(_rt_get_type(sp) != STACK_LIST)
	{
		last = (struct _rt_list*)tk_malloc(sizeof(struct _rt_list) + sizeof(union _rt_stack));
		last->last = last;
		last->next = 0;

		elm = (union _rt_stack*)(last + 1);
		// stack is a ref-to-list element
		*elm = *sp->ref.var;

		sp->ref.var->chk.is_ptr = 0;
		sp->ref.var->chk.type = STACK_LIST;
		sp->ref.var->list.list = last;

		*sp = *sp->ref.var;
	}

	last = (struct _rt_list*)tk_malloc(sizeof(struct _rt_list) + sizeof(union _rt_stack));
	last->last = last;
	last->next = 0;

	elm = (union _rt_stack*)(last + 1);
	*elm = *nxt;

	sp->list.list->last->next = last;
	sp->list.list->last = last;
}

static void _rt_new_value(task* t, union _rt_stack* sp)
{
	sp->chk.is_ptr = 0;
	sp->chk.type = STACK_VALUE;

	sp->value.value = (struct _rt_value*)tk_malloc(sizeof(struct _rt_value));
	memset(sp->value.value,0,sizeof(struct _rt_value));
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

static uint _rt_direct_tree(task* t, st_ptr* config, st_ptr root)
{
	it_ptr it;
	st_ptr pt;
	uint ret = 0;

	it_create(&it,&root);
	while(it_next(&pt,&it))
	{
		if(it.kdata[0])	// skip headers
		{
			ret = t->output->name(t,it.kdata,it.kused);
			if(ret) break;
			ret = t->output->push(t);
			if(ret) break;

			ret = _rt_direct_tree(t,config,pt);
			if(ret) break;
		}
	}

	it_dispose(&it);
	// header elements
	if(ret == 0)
	{
		char  head[HEAD_SIZE];
		int gret = st_get(&root,head,sizeof(head));

		while(gret <= 0 && head[0] == 0)	// while-> could be a list
		{
			switch(head[1])
			{
			case 'E':	// expr -> eval
				{
					struct _rt_invocation* ret_inv;
					_rt_create_invocation(t,0,root,&ret_inv);

					ret_inv->sp->chk.type = STACK_DIRECT_OUT;
					ret_inv->sp->chk.is_ptr = 0;

					_rt_invoke(ret_inv,t,config);

					_rt_release_invocation(ret_inv);
				}
				return 0;
			case 'S':	// string
				do
				{
					char data[512];
					gret = st_get(&root,data,sizeof(data));
					gret = t->output->data(t,data,gret < 0? sizeof(data) : gret);
					if(gret) return gret;
				}
				while(gret == -2);
				break;
			case 'I':{	// number/int
					int tmp;
					if(st_get(&root,(char*)&tmp,sizeof(int)) == 0)
					{
						char buffer[sizeof(int) * 2 + 1];
						_rt_print_int_hex(tmp,buffer);
						t->output->data(t,buffer,sizeof(buffer));
					}
				}
				break;
			case 'N':
				ret = t->output->next(t);
				if(ret) return ret;
				return _rt_direct_tree(t,config,root);
			default:
				return t->output->pop(t);	// ignore (functions)
			}

			gret = st_get(&root,head,sizeof(head));
		}
	}

	return ret? ret : t->output->pop(t);
}

static uint _rt_do_out_tree(task* t, st_ptr* config, union _rt_stack* from)
{
	switch(_rt_get_type(from))
	{
	case STACK_NULL:
		return 0;
	case STACK_PTR:
		return _rt_direct_tree(t,config,from->ptr);
	case STACK_INT:{
		char buffer[sizeof(int) * 2 + 1];
		uint ret;
		_rt_print_int_hex(from->sint.value,buffer);
		ret = t->output->data(t,buffer,sizeof(buffer));
		if(ret) return ret;
		return t->output->pop(t);
		}
	case STACK_VALUE:{
		uint ret = t->output->data(t,from->value.value->data,from->value.value->length);
		if(ret) return ret;
		return t->output->pop(t);
		}
	case STACK_LIST:{
		struct _rt_list* list = from->list.list;
		while(list)
		{
			uint ret = _rt_do_out_tree(t,config,(union _rt_stack*)(list + 1));
			if(ret) return ret;
			list = list->next;
		}
		return 0;
		}
	}

	return __LINE__;
}

static uint _rt_copy_tree(task* t, st_ptr* config, st_ptr* to, st_ptr* from)
{
	it_ptr it;
	st_ptr pt;
	uint ret = 0;

	it_create(&it,from);
	while(it_next(&pt,&it))
	{
		if(it.kdata[0])	// skip headers
		{
			st_ptr tto = *to;
			st_insert(t,&tto,it.kdata,it.kused);

			ret = _rt_copy_tree(t,config,&tto,&pt);
			if(ret) break;
		}
	}

	it_dispose(&it);
	// header elements
	if(ret == 0)
	{
		char  head[HEAD_SIZE];
		int gret = st_get(from,head,sizeof(head));

		while(gret <= 0 && head[0] == 0)	// while-> could be a list
		{
			switch(head[1])
			{
			case 'E':	// expr -> eval
				{
					struct _rt_invocation* ret_inv;
					_rt_create_invocation(t,0,*from,&ret_inv);

					_rt_make_assign_ref(ret_inv->sp,(union _rt_stack*)to);

					_rt_invoke(ret_inv,t,config);

					_rt_release_invocation(ret_inv);
				}
				return 0;
			case 'S':	// string
				st_update(t,to,HEAD_STR,HEAD_SIZE);
				do
				{
					char data[512];
					gret = st_get(from,data,sizeof(data));
					st_append(t,to,data,gret < 0? sizeof(data) : gret);
				}
				while(gret == -2);
				break;
			case 'I':{	// number/int
					int tmp;
					if(st_get(from,(char*)&tmp,sizeof(int)) == 0)
					{
						st_update(t,to,HEAD_INT,HEAD_SIZE);
						st_append(t,to,(cdat)&tmp,sizeof(int));
					}
				}
				break;
			case 'N':	// list-next
				st_update(t,to,HEAD_NEXT,HEAD_SIZE);
				return _rt_copy_tree(t,config,to,from);
			}

			gret = st_get(from,head,sizeof(head));
		}
	}
	return ret;
}

static uint _rt_do_copy(task* t, st_ptr* config, st_ptr* to, union _rt_stack* from)
{
	switch(_rt_get_type(from))
	{
	case STACK_NULL:
		return 0;
	case STACK_PTR:
		return _rt_copy_tree(t,config,to,&from->ptr);
	case STACK_INT:
		st_update(t,to,HEAD_INT,HEAD_SIZE);
		st_append(t,to,(cdat)&from->sint.value,sizeof(int));
		break;
	case STACK_VALUE:
		st_update(t,to,HEAD_STR,HEAD_SIZE);
		st_append(t,to,from->value.value->data,from->value.value->length);
		break;
	case STACK_LIST:{
		struct _rt_list* list = from->list.list;
		while(list)
		{
			uint ret = _rt_do_copy(t,config,to,(union _rt_stack*)(list + 1));
			if(ret) return ret;
			if(!st_is_empty(to))
				st_insert(t,to,HEAD_NEXT,HEAD_SIZE);
			list = list->next;
		}
		}
		break;
	default:
		return __LINE__;
	}
	return 0;
}

static uint _rt_do_concat(task* t, st_ptr* config, union _rt_stack* result, union _rt_stack* cat)
{
	result->value.value->data = (uchar*)tk_realloc(result->value.value->data,
		result->value.value->length + cat->value.value->length - 1);

	memcpy(result->value.value->data + result->value.value->length - 1,
		cat->value.value->data,cat->value.value->length - 1);

	return 0;
}

static uint _rt_do_out(task* t, st_ptr* config, union _rt_stack* to, union _rt_stack* from, const uchar last)
{
	int ret;
	switch(_rt_get_type(to))
	{
	case STACK_VALUE:
		return _rt_do_concat(t,config,to,from);
	case STACK_REF:
		return _rt_do_concat(t,config,to->ref.var,from);
	case STACK_MANY:
		ret = _rt_do_concat(t,config,to - to->many.remaining_depth,from);
		if(ret) return ret;
		to->many.remaining_depth--;
		if(to->many.remaining_depth == 1)
			_rt_make_assign_ref(to,to - 1);
		return 0;
	// BELOW: dont copy-copy
	case STACK_PTR:
		ret = _rt_load_value(t,config,from);
		if(ret) return ret;
		st_append(t,&to->ptr,from->value.value->data,from->value.value->length);
		if(last)
			st_append(t,&to->ptr,"\0",1);
		return 0;
	case STACK_DIRECT_OUT:
		ret = _rt_load_value(t,config,from);
		if(ret) return ret;
		if(last && to->chk.data)
		{
			ret = t->output->next(t);
			if(ret) return ret;
		}
		to->chk.data = 1;
		return t->output->data(t,from->value.value->data,from->value.value->length);
	}
	return __LINE__;
}


static void _rt_move_out(struct _rt_invocation** inv, task* t, union _rt_stack* sp, cdat path, uint length)
{
	st_ptr base = sp->ptr;
	if(sp->chk.is_ptr == 0 || st_move(&sp->ptr,path,length))
		_rt_make_null(sp);	// not found
	else	// check for header...
	{
		st_ptr ptr = sp->ptr;
		uchar header[HEAD_SIZE];
		if(st_get(&ptr,header,HEAD_SIZE) >= 0 || header[0] != 0)
			return;

		switch(header[1])
		{
		case 'E':{
			struct _rt_invocation* ret_inv;

			_rt_create_invocation(t,&base,ptr,&ret_inv);

			*ret_inv->sp = *(sp - 1);	// copy target
			ret_inv->parent = *inv;
			*inv = ret_inv;
			}
			break;
		case 'I':{
			int val;
			char buffer[sizeof(int)*2 + 1];
			st_get(&ptr,(char*)&val,sizeof(int));
			_rt_print_int_hex(val,buffer);

			}
			break;
		case 'S':
			break;
		}
	}
}

static void _rt_move(struct _rt_invocation** inv, task* t, union _rt_stack* sp, cdat path, uint length)
{
	st_ptr base = sp->ptr;
	if(sp->chk.is_ptr == 0 || st_move(&sp->ptr,path,length))
		_rt_make_null(sp);	// not found
	else	// check for header...
	{
		st_ptr ptr = sp->ptr;
		uchar header[HEAD_SIZE];
		if(st_get(&ptr,header,HEAD_SIZE) >= 0 || header[0] != 0)
			return;

		switch(header[1])
		{
		case 'E':{
			struct _rt_invocation* ret_inv;

			_rt_create_invocation(t,&base,ptr,&ret_inv);

			_rt_make_assign_ref(ret_inv->sp,sp);
			ret_inv->parent = *inv;
			*inv = ret_inv;
			}
			break;
		case 'I':
			_rt_make_int(sp,0);
			st_get(&ptr,(char*)&sp->sint.value,sizeof(int));
			break;
		case 'S':
			_rt_new_value(t,sp);
			sp->value.value->data = st_get_all(&ptr,&sp->value.value->length);
			break;
		default:
			_rt_make_null(sp);
		}
	}
}

static void _rt_move_str(struct _rt_invocation** inv, task* t, union _rt_stack* ptr, union _rt_stack* val)
{
	switch(_rt_get_type(val))
	{
	case STACK_VALUE:
		_rt_move(inv,t,ptr,val->value.value->data,val->value.value->length);
		break;
	case STACK_INT:{
		char buffer[sizeof(int)*2 + 1];
		_rt_print_int_hex(val->sint.value,buffer);
		_rt_move(inv,t,ptr,buffer,sizeof(int)*2 + 1);
		}
		break;
	default:
		_rt_sys_error(*inv,__LINE__);
	}
}

static uint _rt_compare(struct _rt_invocation* inv, union _rt_stack* sp)
{
	uint type = _rt_get_type(sp);
	if(_rt_get_type(sp - 1) != type)
		return _rt_sys_error(inv,__LINE__);

	switch(type)
	{
	case STACK_INT:
		return (sp->sint.value - (sp - 1)->sint.value);
	case STACK_VALUE:
		return memcmp(sp->value.value->data,(sp - 1)->value.value->data,
			sp->value.value->length > (sp - 1)->value.value->length? sp->value.value->length : (sp - 1)->value.value->length);
	}
	return _rt_sys_error(inv,__LINE__);
}

static uint _rt_equal(union _rt_stack* sp)
{
	uint type = _rt_get_type(sp);
	if(_rt_get_type(sp - 1) == type)
	switch(type)
	{
	case STACK_NULL:
		return 1;
	case STACK_INT:
		return (sp->sint.value == (sp - 1)->sint.value);
	case STACK_VALUE:
		return (sp->value.value == (sp - 1)->value.value || 0 == memcmp(sp->value.value->data,(sp - 1)->value.value->data,
			sp->value.value->length > (sp - 1)->value.value->length? sp->value.value->length : (sp - 1)->value.value->length));
	case STACK_PTR:
		if(sp->ptr.key == (sp - 1)->ptr.key &&
			sp->ptr.pg == (sp - 1)->ptr.pg &&
			sp->ptr.offset == (sp - 1)->ptr.offset)
			return 1;
		return (st_is_empty(&sp->ptr) && st_is_empty(&(sp - 1)->ptr));
	case STACK_LIST:
		return (sp->list.list == (sp - 1)->list.list);
	}
	return 0;
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
		if(_rt_get_type(sp - sp->many.remaining_depth) == STACK_NULL)
			st_empty(t,&(sp - sp->many.remaining_depth)->ptr);
		*tmpptr = (sp - sp->many.remaining_depth)->ptr;
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

static uint _rt_insert_str(task* t, st_ptr* tmpptr, union _rt_stack* ptr, union _rt_stack* val, const uchar push)
{
	switch(_rt_get_type(val))
	{
	case STACK_VALUE:
		return _rt_insert(t,tmpptr,ptr,val->value.value->data,val->value.value->length,push);
	case STACK_INT:{
		char buffer[sizeof(int)*2 + 1];
		_rt_print_int_hex(val->sint.value,buffer);
		return _rt_insert(t,tmpptr,ptr,buffer,sizeof(int)*2 + 1,push);
		}
	}
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
		_rt_make_null(++inv->sp);
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

	case OP_ADD:
		if(_rt_err(inv,_rt_get_type(inv->sp) != STACK_INT || _rt_get_type(inv->sp - 1) != STACK_INT))
			break;
		(inv->sp - 1)->sint.value += inv->sp->sint.value;
		inv->sp--;
		break;
	case OP_SUB:
		if(_rt_err(inv,_rt_get_type(inv->sp) != STACK_INT || _rt_get_type(inv->sp - 1) != STACK_INT))
			break;
		(inv->sp - 1)->sint.value -= inv->sp->sint.value;
		inv->sp--;
		break;
	case OP_MUL:
		if(_rt_err(inv,_rt_get_type(inv->sp) != STACK_INT || _rt_get_type(inv->sp - 1) != STACK_INT))
			break;
		(inv->sp - 1)->sint.value *= inv->sp->sint.value;
		inv->sp--;
		break;
	case OP_DIV:
		if(_rt_err(inv,_rt_get_type(inv->sp) != STACK_INT || _rt_get_type(inv->sp - 1) != STACK_INT))
			break;
		(inv->sp - 1)->sint.value /= inv->sp->sint.value;
		inv->sp--;
		break;
	case OP_REM:
		if(_rt_err(inv,_rt_get_type(inv->sp) != STACK_INT || _rt_get_type(inv->sp - 1) != STACK_INT))
			break;
		(inv->sp - 1)->sint.value %= inv->sp->sint.value;
		inv->sp--;
		break;

	case OP_EQ:
		_rt_make_int(inv->sp - 1,_rt_equal(inv->sp));
		inv->sp--;
		break;
	case OP_NE:
		_rt_make_int(inv->sp - 1,!_rt_equal(inv->sp));
		inv->sp--;
		break;
	case OP_GE:
		_rt_make_int(inv->sp - 1,_rt_compare(inv,inv->sp) >= 0);
		inv->sp--;
		break;
	case OP_GT:
		_rt_make_int(inv->sp - 1,_rt_compare(inv,inv->sp) > 0);
		inv->sp--;
		break;
	case OP_LE:
		_rt_make_int(inv->sp - 1,_rt_compare(inv,inv->sp) <= 0);
		inv->sp--;
		break;
	case OP_LT:
		_rt_make_int(inv->sp - 1,_rt_compare(inv,inv->sp) < 0);
		inv->sp--;
		break;

	case OP_END:
		inv = _rt_release_invocation(inv);
		if(inv == 0)
			return 0;
		break;
	case OP_CALL:
		{
			struct _rt_invocation* cl;
			tmpptr = inv->sp->ptr;
			tmpushort = *((ushort*)inv->ip);
			inv->ip += sizeof(ushort);

			if(tmpptr.key == 0 || st_move(&tmpptr,inv->ip,tmpushort))
				return __LINE__;
			inv->ip += tmpushort;

			tmpint = _rt_create_invocation(t,&inv->sp->ptr,tmpptr,&cl);
			cl->parent = inv;

			_rt_make_inv_ref(inv->sp,cl);
		}
		break;
	case OP_DOCALL:
		{
			struct _rt_invocation* cl = inv->sp->inv.inv;
			inv->sp--;
			*cl->sp = *inv->sp;	// copy target
			inv = cl;
		}
		break;
	case OP_DOCALL_N:	// nested call
		{
			struct _rt_invocation* cl = inv->sp->inv.inv;
			_rt_make_null(inv->sp);	// blank
			_rt_make_assign_ref(cl->sp,inv->sp);
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
		if(_rt_err(inv,_rt_insert(t,&tmpptr,inv->sp,inv->ip,tmpushort,1)))
			break;
		if(tmpptr.key)
		{
			st_insert(t,&tmpptr,inv->ip,tmpushort);
			inv->sp++;
			inv->sp->ptr = tmpptr;
		}
		inv->ip += tmpushort;
		break;
	case OP_MVW:{
		ushort len = *((ushort*)inv->ip);
		char* str;
		inv->ip += sizeof(ushort);
		str = inv->ip;
		inv->ip += len;
		switch(_rt_get_type(inv->sp))
		{
		case STACK_PTR:
			st_insert(t,&inv->sp->ptr,str,len);
			break;
		case STACK_DIRECT_OUT:
			// gen. name-event
			_rt_err(inv,t->output->name(t,str,len));
			break;
		default:
			_rt_sys_error(inv,__LINE__);
		}
		}
		break;
	case OP_WIDX:
		_rt_err(inv,_rt_insert_str(t,&tmpptr,inv->sp - 1,inv->sp,0));
		inv->sp--;
		break;
	case OP_WVAR:
		_rt_err(inv,_rt_insert_str(t,&tmpptr,inv->sp,inv->vars + *inv->ip++,1));
		if(tmpptr.key)
		{
			inv->sp++;
			inv->sp->ptr = tmpptr;
		}
		break;
	case OP_WVAR0:
		_rt_err(inv,_rt_insert_str(t,&tmpptr,inv->sp,inv->vars + *inv->ip++,1));
		break;

	case OP_MV:{
		uchar* str;
		ushort len = *((ushort*)inv->ip);
		inv->ip += sizeof(ushort);
		str = inv->ip;
		inv->ip += len;
		_rt_move(&inv,t,inv->sp,str,len);
		}
		break;
	case OP_RIDX:
		_rt_move_str(&inv,t,inv->sp - 1,inv->sp);
		inv->sp--;
		break;
	case OP_RVAR:
		_rt_move_str(&inv,t,inv->sp,inv->vars + *inv->ip++);
		break;

	case OP_LVAR:
		*(++inv->sp) = inv->vars[*inv->ip++];
		break;
	case OP_AVAR:
		_rt_make_assign_ref(++inv->sp,inv->vars + *inv->ip++);
		break;
	case OP_AVARS:
		tmpint = *inv->ip++;
		inv->ip += tmpint;
		inv->sp++;
		memset(inv->sp,0,tmpint * sizeof(union _rt_stack));
		inv->sp += tmpint;
		inv->sp->chk.is_ptr = 0;
		inv->sp->chk.type = STACK_MANY;
		inv->sp->many.remaining_depth = tmpint;
		break;
	case OP_CAV:
		// emitIs
		tmpushort = *((ushort*)inv->ip);
		inv->ip += sizeof(ushort);
		{
			uchar* op_avars = inv->ip - tmpushort + 1;
			tmpint = *op_avars;
			op_avars += tmpint;
			inv->sp--;
			while(tmpint-- != 0)
			{
				inv->vars[*op_avars--] = *inv->sp--;
			}
		}
		break;

	case OP_CAT:
		if(_rt_get_type(inv->sp) != STACK_VALUE)
			_rt_sys_error(inv,__LINE__);
		else if(inv->sp->value.value->ref)
		{
			struct _rt_value* val = inv->sp->value.value;
			_rt_new_value(t,inv->sp);
			inv->sp->value.value->data = tk_malloc(val->length);
			inv->sp->value.value->length = val->length;
			memcpy(inv->sp->value.value->data,val->data,val->length);
		}
		break;
	case OP_OVAR:
		// emit Ic
		_rt_err(inv,_rt_do_out(t,config,inv->sp,inv->vars + *inv->ip++,0));
		break;
	case OP_OUT:
		_rt_err(inv,_rt_do_out(t,config,inv->sp - 1,inv->sp,0));
		inv->sp--;
		break;
	case OP_OUTL: // [OUT Last]
		_rt_err(inv,_rt_do_out(t,config,inv->sp - 1,inv->sp,1));
		inv->sp--;
		break;
	case OP_OUTLT:	// non-string (concat) out-ing [OUT Last Tree]
		switch(_rt_get_type(inv->sp - 1))
		{
		case STACK_MANY: // set and roll
			if((inv->sp - 1)->many.remaining_depth > 1)
			{
				*(inv->sp - 1 - (inv->sp - 1)->many.remaining_depth)
					= *inv->sp;
				(inv->sp - 1)->many.remaining_depth--;
				break;
			}
			else	// last: change to STACK_REF and fall-thru
				_rt_make_assign_ref(inv->sp - 1,inv->sp - 2);
		case STACK_REF: // set ref
			if(_rt_get_type((inv->sp - 1)->ref.var) == STACK_NULL)
				*(inv->sp - 1)->ref.var = *inv->sp;	// first time just copy
			else
				_rt_next_list(t,inv->sp - 1,inv->sp);
			break;
		case STACK_PTR:	// copy-to
			if(_rt_err(inv,_rt_do_copy(t,config,&(inv->sp - 1)->ptr,inv->sp)))
				break;
			if(!st_is_empty(&(inv->sp - 1)->ptr))
				st_insert(t,&(inv->sp - 1)->ptr,HEAD_NEXT,HEAD_SIZE);
			break;
		case STACK_DIRECT_OUT: // output
			_rt_err(inv,_rt_do_out_tree(t,config,inv->sp));
			break;
		default:
			return __LINE__;
		}
		inv->sp--;
		break;
	case OP_CLEAR:
		st_delete(t,&inv->sp->ptr,0,0);	// all clear before assign
		break;

	case OP_STR:
		// emit Is
		tmpptr = inv->fun->strings;
		st_move(&tmpptr,inv->ip,sizeof(ushort));
		inv->ip += sizeof(ushort);
		st_get(&tmpptr,(char*)&tmpint,HEAD_SIZE);	// SKIP HEAD -> REMOVE!
		_rt_new_value(t,++inv->sp);
		inv->sp->value.value->data = st_get_all(&tmpptr,&inv->sp->value.value->length);
		break;

	case OP_DEFP:
		// emit Is2 (branch forward)
		tmpuchar  = *inv->ip++;
		tmpushort = *((ushort*)inv->ip);
		inv->ip += sizeof(ushort);

		if(_rt_get_type(inv->vars + tmpuchar) == STACK_NULL)
		{
			// setup assign to var[tmpuchar]
			_rt_make_assign_ref(++inv->sp,inv->vars + tmpuchar);
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
		inv->ip += tmpushort - sizeof(ushort);
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
		_rt_make_int(++inv->sp,*((int*)inv->ip));
		inv->ip += sizeof(int);
		break;
	case OP_ERROR:
		// emit S (Report error)
		printf("OP_ERROR: %d\n",inv->error);
		return __LINE__;
	default:
		return __LINE__;
	}
}

int rt_do_call(task* t, st_ptr* app, st_ptr* root, st_ptr* fun, st_ptr* param)
{
	struct _rt_invocation* inv;
	// create invocation
	int ret = _rt_create_invocation(t,root,*fun,&inv);
	if(ret)
	{
		_rt_release_invocation(inv);
		return ret;
	}

	// dump params
	//_cle_read(param,0);

	if(param && inv->fun->max_params != 0)
		// set param-tree
		inv->vars->ptr = *param;

	// mark for output-handler
	inv->sp->chk.type = STACK_DIRECT_OUT;
	inv->sp->chk.is_ptr = 0;
	inv->sp->chk.data = 0;

	t->output->start(t);	// begin output
	// .. and invoke
	ret = _rt_invoke(inv,t,app);

	if(ret)
		_rt_release_invocation(inv);
	else
		t->output->end(t);		// end output
	return ret;
}
