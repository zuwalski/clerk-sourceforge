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
#include "cle_runtime.h"
#include "cle_stream.h"
#include "cle_instance.h"
#include <stdio.h>

typedef double rt_number;

static const char number_format[] = "%.14g";

static const char to_str_expr[] = "str\0E";

struct _rt_callframe;

struct _rt_code
{
	struct _rt_code* next;
	char* code;
	struct _rt_callframe* free;
	st_ptr home;
	st_ptr strings;
	struct _body_ body;
};

// stack-element types
enum {
	STACK_NULL = 0,
	STACK_NUM,
	STACK_OBJ,
	STACK_OUTPUT,
	STACK_REF,
	STACK_CODE,
	STACK_PTR,
	// readonly ptr
	STACK_RO_PTR,
	STACK_PROP
};

struct _rt_stack
{
	union 
	{
		rt_number num;
		struct _rt_stack* var;
		struct
		{
			st_ptr single_ptr;
			st_ptr single_ptr_w;
		};
		struct
		{
			st_ptr obj;
			st_ptr ptr;
		};
		struct
		{
			st_ptr prop_obj;
			ulong prop_id;
		};
		struct
		{
			st_ptr code_obj;
			struct _rt_code* code;
		};
		struct
		{
			cle_pipe* out;
			void* outdata;
		};
	};
	uchar type;
	uchar flags;	// JIT-flags
};

struct _rt_callframe
{
	struct _rt_callframe* parent;
	struct _rt_code* code;
	const char* pc;
	struct _rt_stack* vars;
	struct _rt_stack* sp;
	st_ptr object;
	char is_expr;
};

struct _rt_invocation
{
	struct _rt_callframe* top;
	struct _rt_code* code_cache;
	task* t;
	event_handler* hdl;
	int params_before_run;
};

const static char exec_error = OP_ERROR;

static void _rt_error(struct _rt_invocation* inv, uint code)
{
	char buffer[64];
	int len = sprintf(buffer,"runtime:failed(%d)",code);
	cle_stream_fail(inv->hdl,buffer,len);
	inv->top->pc = &exec_error;
}

static struct _rt_code* _rt_load_code(struct _rt_invocation* inv, st_ptr code)
{
	struct _rt_code* ret = inv->code_cache;
	st_ptr pt;
	struct _body_ body;

	// lookup in cache
	while(ret != 0)
	{
		if(ret->home.pg == code.pg && ret->home.key == code.key)
			return ret;

		ret = ret->next;
	}

	// not found -> load it and add it
	pt = code;
	if(st_move(inv->t,&pt,"B",2) != 0)
		return 0;

	if(st_get(inv->t,&pt,(char*)&body,sizeof(struct _body_)) != -2)
		return 0;

	ret = (struct _rt_code*)tk_alloc(inv->t,sizeof(struct _rt_code) + body.codesize - sizeof(struct _body_));

	// push
	ret->next = inv->code_cache;
	inv->code_cache = ret;

	ret->home = code;
	ret->body = body;

	ret->code = (char*)ret + sizeof(struct _rt_code);
	if(st_get(inv->t,&pt,ret->code,ret->body.codesize - sizeof(struct _body_)) != -1)
		return 0;

	// get strings
	ret->strings = code;
	st_move(inv->t,&ret->strings,"S",2);

	// zero invocation-cache
	ret->free = 0;

	return ret;
}

static struct _rt_callframe* _rt_newcall(struct _rt_invocation* inv, struct _rt_code* code, st_ptr* object, int is_expr)
{
	struct _rt_callframe* cf;

	if(code->free != 0)
	{
		cf = code->free;
		code->free = cf->parent;
	}
	else
		cf = (struct _rt_callframe*)tk_alloc(inv->t,sizeof(struct _rt_callframe)
			+ (code->body.maxvars + code->body.maxstack) * sizeof(struct _rt_stack));

	// push
	cf->parent = inv->top;
	inv->top = cf;

	cf->pc = code->code;
	cf->code = code;
	cf->object = *object;

	cf->vars = (struct _rt_stack*)((char*)cf + sizeof(struct _rt_callframe));

	// clear all vars
	memset(cf->vars,0,code->body.maxvars * sizeof(struct _rt_stack));
	cf->sp = cf->vars + code->body.maxvars + code->body.maxstack;

	cf->is_expr = is_expr;
	return cf;
}

static void _rt_get(struct _rt_invocation* inv, struct _rt_stack** sp)
{
	struct _rt_stack top = **sp;
	char buffer[HEAD_SIZE];

	// look for header
	if(st_get(inv->t,&top.ptr,buffer,HEAD_SIZE) != -2 || buffer[0] != 0)
	{
		(*sp)->type = STACK_OBJ;
		return;
	}

	(*sp)->type = STACK_NULL;

	switch(buffer[1])
	{
	case 'M':	// method
		(*sp)->code = _rt_load_code(inv,top.ptr);
		(*sp)->code_obj = top.obj;
		(*sp)->type = STACK_CODE;
		break;
	case 'E':	// expr
		inv->top = _rt_newcall(inv,_rt_load_code(inv,top.ptr),&top.obj,1);

		(*sp)->type = STACK_NULL;
		inv->top->sp--;
		inv->top->sp->type = STACK_REF;
		inv->top->sp->var = *sp;
		*sp = inv->top->sp;
		break;
	case 'R':	// ref (oid)
		(*sp)->obj = inv->hdl->instance;
		if(st_move(inv->t,&(*sp)->obj,HEAD_OID,HEAD_SIZE) != 0)
			return;
		// copy_move sp-obj , top.ptr
		if(st_move_st(inv->t,&(*sp)->obj,&top.ptr) != 0)
			return;
		(*sp)->ptr = (*sp)->obj;
		(*sp)->type = STACK_OBJ;
		break;
	case 'y':	// property
		// geting from within objectcontext or external?
		if(top.obj.pg != inv->top->object.pg || top.obj.key != inv->top->object.key)
			return;
		if(st_get(inv->t,&top.ptr,(char*)&(*sp)->prop_id,PROPERTY_SIZE) != -1)
			return;
		(*sp)->prop_obj = top.obj;
		(*sp)->type = STACK_PROP;
	}
}

static uint _rt_find_prop_value(struct _rt_invocation* inv, struct _rt_stack* sp)
{
	st_ptr obj = sp->single_ptr = sp->prop_obj;
	sp->type = inv->top->is_expr? STACK_RO_PTR : STACK_PTR;

	while(st_move(inv->t,&sp->single_ptr,HEAD_PROPERTY,HEAD_SIZE) != 0 ||
		st_move(inv->t,&sp->single_ptr,(cdat)&sp->prop_id,PROPERTY_SIZE) != 0)
	{
		// no value in object - look in parent object (if any)
		if(st_move(inv->t,&obj,HEAD_EXTENDS,HEAD_SIZE) != 0)
			return 1;

		sp->single_ptr = inv->hdl->instance;
		if(st_move(inv->t,&sp->single_ptr,HEAD_OID,HEAD_SIZE) != 0)
			return 1;
		if(st_move_st(inv->t,&sp->single_ptr,&obj))
			return 1;

		obj = sp->single_ptr;
		sp->type = STACK_RO_PTR;
	}

	return 0;
}

static uint _rt_move(struct _rt_invocation* inv, struct _rt_stack** sp, cdat mv, uint length)
{
	switch((*sp)->type)
	{
	case STACK_OBJ:
		// object-root?
		if((*sp)->obj.pg == (*sp)->ptr.pg && (*sp)->obj.key == (*sp)->ptr.key)
		{
			if(cle_get_property_host(inv->t,inv->hdl->instance,&(*sp)->ptr,mv,length) < 0)
				return 1;
		}
		else if(st_move(inv->t,&(*sp)->ptr,mv,length) != 0)
			return 1;
		_rt_get(inv,sp);
		break;
	case STACK_PROP:
		if(_rt_find_prop_value(inv,(*sp)))
			return 1;

		if(st_move(inv->t,&(*sp)->single_ptr,mv,length) != 0)
		{
			// last chance: maybe field contains a ref .. try move in ref'ed obj.
			st_ptr ref = (*sp)->single_ptr;
			if(st_move(inv->t,&ref,HEAD_REF,HEAD_SIZE) != 0)
				return 1;

			(*sp)->obj = inv->hdl->instance;
			if(st_move(inv->t,&(*sp)->obj,HEAD_OID,HEAD_SIZE) != 0 ||
				st_move_st(inv->t,&(*sp)->obj,&ref) != 0)
				return 1;

			(*sp)->ptr = (*sp)->obj;
			if(cle_get_property_host(inv->t,inv->hdl->instance,&(*sp)->ptr,mv,length) < 0)
				return 1;
			_rt_get(inv,sp);
		}
		break;
	case STACK_PTR:
	case STACK_RO_PTR:
		return (st_move(inv->t,&(*sp)->single_ptr,mv,length) != 0);
	default:
		return 1;
	}

	return 0;
}

static uint _rt_move_st(struct _rt_invocation* inv, struct _rt_stack** sp, st_ptr* mv)
{
	switch((*sp)->type)
	{
	case STACK_OBJ:
		// object-root?
		if((*sp)->obj.pg == (*sp)->ptr.pg && (*sp)->obj.key == (*sp)->ptr.key)
		{
			if(cle_get_property_host_st(inv->t,inv->hdl->instance,&(*sp)->ptr,*mv) < 0)
				return 1;
		}
		else if(st_move_st(inv->t,&(*sp)->ptr,mv) != 0)
			return 1;
		_rt_get(inv,sp);
		break;
	case STACK_PROP:
		if(_rt_find_prop_value(inv,(*sp)))
			return 1;

		if(st_move_st(inv->t,&(*sp)->single_ptr,mv) != 0)
		{
			// last chance: maybe field contains a ref .. try move in ref'ed obj.
			st_ptr ref = (*sp)->single_ptr;
			if(st_move(inv->t,&ref,HEAD_REF,HEAD_SIZE) != 0)
				return 1;

			(*sp)->obj = inv->hdl->instance;
			if(st_move(inv->t,&(*sp)->obj,HEAD_OID,HEAD_SIZE) != 0 ||
				st_move_st(inv->t,&(*sp)->obj,&ref) != 0)
				return 1;

			(*sp)->ptr = (*sp)->obj;
			if(cle_get_property_host_st(inv->t,inv->hdl->instance,&(*sp)->ptr,*mv) < 0)
				return 1;
			_rt_get(inv,sp);
		}
		break;
	case STACK_PTR:
	case STACK_RO_PTR:
		return (st_move_st(inv->t,&(*sp)->single_ptr,mv) != 0);
	default:
		return 1;
	}

	return 0;
}

static struct _rt_stack* _rt_eval_expr(struct _rt_invocation* inv, struct _rt_stack* target, struct _rt_stack* expr)
{
	inv->top = _rt_newcall(inv,_rt_load_code(inv,expr->ptr),&expr->obj,1);
	inv->top->sp--;
	*inv->top->sp = *target;
	return inv->top->sp;
}

static uint _rt_string_out(struct _rt_invocation* inv, struct _rt_stack* to, struct _rt_stack* from)
{
	switch(to->type)
	{
	case STACK_OUTPUT:
		return st_map(inv->t,&from->single_ptr,to->out->data,to->outdata);
	case STACK_PTR:
		return st_insert_st(inv->t,&to->single_ptr_w,&from->single_ptr);
	default:
		return 1;
	}
}

static void _rt_num_out(struct _rt_invocation* inv, struct _rt_stack* to, rt_number num)
{
	char buffer[32];
	int len = sprintf(buffer,number_format,num);
	switch(to->type)
	{
	case STACK_OUTPUT:
		to->out->data(to->outdata,buffer,len);
		break;
	case STACK_PTR:
		st_insert(inv->t,&to->single_ptr_w,buffer,len);
	}
}

static uint _rt_out(struct _rt_invocation* inv, struct _rt_stack** sp, struct _rt_stack* to, struct _rt_stack* from)
{
	switch(from->type)
	{
	case STACK_PROP:
		if(_rt_find_prop_value(inv,from))
			return 1;
	case STACK_RO_PTR:
	case STACK_PTR:
		return _rt_string_out(inv,to,from);
	case STACK_NUM:
		_rt_num_out(inv,to,from->num);
		break;
	case STACK_OBJ:
		if(st_move(inv->t,&from->ptr,to_str_expr,sizeof(to_str_expr) - 1) == 0)
			*sp = _rt_eval_expr(inv,to,from);
		break;	// no "str" expr? output nothing
	case STACK_CODE:
		// write out path/event to method/handler
		if(st_move(inv->t,&from->single_ptr,"p",1) == 0)
			return _rt_string_out(inv,to,from);
	}
	return 0;
}

static uint _rt_ref_out(struct _rt_invocation* inv, struct _rt_stack** sp, struct _rt_stack* to)
{
	st_ptr pt;
	st_empty(inv->t,&pt);

	if(to->var->type != STACK_NULL)
	{
		struct _rt_stack target;
		target.type = STACK_PTR;
		target.single_ptr_w = pt;

		if(_rt_out(inv,sp,&target,to->var))
			return 1;
	}

	to->var->single_ptr_w = to->var->single_ptr = pt;
	to->var->type = STACK_PTR;
	*to = *to->var;
	return 0;
}

static uint _rt_call(struct _rt_invocation* inv, struct _rt_stack* sp, int params)
{
	int i;
	if(sp[params].type != STACK_CODE)
		return __LINE__;

	inv->top = _rt_newcall(inv,sp[params].code,&sp[params].code_obj,inv->top->is_expr);

	if(inv->top->code->body.maxparams < params)
		return __LINE__;

	for(i = params - 1; i >= 0; i--)
		inv->top->vars[params - 1 - i] = sp[i];

	return 0;
}

// make sure its a number (load it)
static uint _rt_num(struct _rt_invocation* inv, struct _rt_stack* sp)
{
	st_ptr* loadfrom;

	if(sp->type == STACK_NUM)
		return 0;

	if(sp->type != STACK_PROP)
		return 1;

	if(_rt_find_prop_value(inv,sp))
		return 1;

	// is there a number here?
	if(st_move(inv->t,&sp->single_ptr,HEAD_NUM,HEAD_SIZE) != 0)
		return 1;

	// .. load it
	if(st_get(inv->t,&sp->single_ptr,(char*)&sp->num,sizeof(rt_number)) != -1)
		return 1;

	sp->type = STACK_NUM;
	return 0;
}

static int _rt_equal(struct _rt_invocation* inv, struct _rt_stack* op1, struct _rt_stack* op2)
{
	if(op1->type == STACK_NUM)
	{
		if(_rt_num(inv,op2))
			return 0;
	}
	else if(op2->type == STACK_NUM)
	{
		if(_rt_num(inv,op1))
			return 0;
	}
	// not a number
	else
	{
		return (op1->type == op2->type);
	}

	return op1->num == op2->num;
}

static int _rt_compare(struct _rt_invocation* inv, struct _rt_stack* op1, struct _rt_stack* op2)
{
	if(op1->type == STACK_NUM)
	{
		if(_rt_num(inv,op2))
			return 0;
	}
	else if(op2->type == STACK_NUM)
	{
		if(_rt_num(inv,op1))
			return 0;
	}
	// not a number
	else
	{
		op2->type = STACK_NULL;
		return 0;
	}

	return op1->num - op2->num;
}

static void _rt_run(struct _rt_invocation* inv)
{
	struct _rt_stack* sp = inv->top->sp;
	int tmp;
	while(1)
	{
		switch(*inv->top->pc++)
		{
		case OP_NOOP:
			break;
		case OP_DEBUG:
			inv->top->pc += sizeof(ushort);
			break;
		case OP_POP:
			sp++;
			break;
		case OP_ADD:
			if(_rt_num(inv,sp) || _rt_num(inv,sp + 1))
				_rt_error(inv,__LINE__);
			sp[1].num += sp[0].num;
			sp++;
			break;
		case OP_SUB:
			if(_rt_num(inv,sp) || _rt_num(inv,sp + 1))
				_rt_error(inv,__LINE__);
			sp[1].num -= sp[0].num;
			sp++;
			break;
		case OP_MUL:
			if(_rt_num(inv,sp) || _rt_num(inv,sp + 1))
				_rt_error(inv,__LINE__);
			sp[1].num *= sp[0].num;
			sp++;
			break;
		case OP_DIV:
			if(_rt_num(inv,sp) || _rt_num(inv,sp + 1) || sp->num == 0)
				_rt_error(inv,__LINE__);
			sp[1].num /= sp[0].num;
			sp++;
			break;
		case OP_NOT:
			sp->num = ((sp[0].type == STACK_NULL) ||
				(sp[0].type == STACK_NUM && sp[0].num == 0) ||
				((sp[0].type == STACK_PTR || sp[0].type == STACK_RO_PTR) && st_is_empty(&sp[0].single_ptr)));
			sp->type = STACK_NUM;
			break;
		case OP_NEG:
			if(_rt_num(inv,sp))
				_rt_error(inv,__LINE__);
			sp->num *= -1;
			break;

		case OP_EQ:
			sp[1].num = _rt_equal(inv,sp,sp + 1);
			sp[1].type = STACK_NUM;
			sp++;
			break;
		case OP_NE:
			sp[1].num = _rt_equal(inv,sp,sp + 1) == 0;
			sp[1].type = STACK_NUM;
			sp++;
			break;
		case OP_GE:
			sp[1].num = _rt_compare(inv,sp,sp + 1) >= 0;
			sp++;
			break;
		case OP_GT:
			sp[1].num = _rt_compare(inv,sp,sp + 1) > 0;
			sp++;
			break;
		case OP_LE:
			sp[1].num = _rt_compare(inv,sp,sp + 1) <= 0;
			sp++;
			break;
		case OP_LT:
			sp[1].num = _rt_compare(inv,sp,sp + 1) < 0;
			sp++;
			break;

		case OP_BNZ:
			// emit Is (branch forward conditional)
			tmp = *((ushort*)inv->top->pc);
			inv->top->pc += sizeof(ushort);
			if((sp[0].type != STACK_NULL) && (
				(sp[0].type == STACK_NUM && sp[0].num != 0) ||
				((sp[0].type == STACK_PTR || sp[0].type == STACK_RO_PTR) && st_is_empty(&sp[0].single_ptr) == 0)))
				inv->top->pc += tmp;
			sp++;
		case OP_BZ:
			// emit Is (branch forward conditional)
			tmp = *((ushort*)inv->top->pc);
			inv->top->pc += sizeof(ushort);
			if((sp[0].type == STACK_NULL) ||
				(sp[0].type == STACK_NUM && sp[0].num == 0) ||
				((sp[0].type == STACK_PTR || sp[0].type == STACK_RO_PTR) && st_is_empty(&sp[0].single_ptr)))
				inv->top->pc += tmp;
			sp++;
			break;
		case OP_BR:
			// emit Is (branch forward)
			tmp = *((ushort*)inv->top->pc);
			inv->top->pc += tmp + sizeof(ushort);
			break;

		case OP_LOOP:	// JIT-HOOK 
			// emit Is (branch back)
			tmp = *((ushort*)inv->top->pc);
			inv->top->pc -= tmp + sizeof(ushort);
			break;

		case OP_FREE:
			// emit Ic2 (byte,byte)
			tmp = *inv->top->pc++;
			while(tmp-- > 0)
			{
				inv->top->vars[tmp + *inv->top->pc].type = STACK_NULL;
			}
			inv->top->pc++;
			break;

		case OP_NULL:
			sp--;
			sp->type = STACK_NULL;
			break;
		case OP_IMM:
			// emit II (imm short)
			sp--;
			sp->type = STACK_NUM;
			sp->num = *((short*)inv->top->pc);
			inv->top->pc += sizeof(short);
			break;
		case OP_STR:
			// emit Is
			sp--;
			sp->type = STACK_RO_PTR;
			sp->single_ptr = inv->top->code->strings;
			st_move(inv->t,&sp->single_ptr,inv->top->pc,sizeof(ushort));
			inv->top->pc += sizeof(ushort);
			break;
		case OP_OBJ:
			sp--;
			sp->type = STACK_OBJ;
			sp->ptr = sp->obj = inv->top->object;
			break;

		case OP_NEW:
			tmp = *((ushort*)inv->top->pc);
			inv->top->pc += sizeof(ushort);

			inv->top->pc += tmp;
			break;

		case OP_OMV:
			tmp = *((ushort*)inv->top->pc);
			inv->top->pc += sizeof(ushort);
			sp--;
			sp->ptr = sp->obj = inv->top->object;
			if(cle_get_property_host(inv->t,inv->hdl->instance,&sp->ptr,inv->top->pc,tmp) < 0)
			{
				sp->type = STACK_NULL;
				inv->top->pc += tmp;
			}
			else
			{
				inv->top->pc += tmp;
				_rt_get(inv,&sp);
			}
			break;
		case OP_MV:
			tmp = *((ushort*)inv->top->pc);
			inv->top->pc += sizeof(ushort);
			if(_rt_move(inv,&sp,inv->top->pc,tmp))
				sp->type = STACK_NULL;
			inv->top->pc += tmp;
			break;
		case OP_RIDX:
			if(sp->type == STACK_NUM)
			{
				char buffer[sizeof(rt_number) + HEAD_SIZE];
				buffer[0] = 0;
				buffer[1] = 'N';
				memcpy(buffer + 2,&sp->num,sizeof(rt_number));
				sp++;
				if(_rt_move(inv,&sp,buffer,sizeof(buffer)))
					sp->type = STACK_NULL;
			}
			else if(sp->type == STACK_PTR ||
				sp->type == STACK_RO_PTR)
			{
				st_ptr mv = sp->single_ptr;
				sp++;
				if(_rt_move_st(inv,&sp,&mv))
					sp->type = STACK_NULL;
			}
			break;
		case OP_LVAR:
			sp--;
			*sp = inv->top->vars[*inv->top->pc++];
			break;

		// writer
		case OP_POPW:
			if(sp->type == STACK_OUTPUT)
				sp->out->pop(sp->outdata);
			else
				sp++;
			break;
		case OP_DMVW:
			tmp = *((ushort*)inv->top->pc);
			inv->top->pc += sizeof(ushort);
			switch(sp->type)
			{
			case STACK_REF:
				if(sp->var->type == STACK_NULL)
				{
					st_empty(inv->t,&sp->var->single_ptr_w);
					sp->var->single_ptr = sp->var->single_ptr_w;
				}
				else if(sp->var->type != STACK_PTR)
				{
					_rt_error(inv,__LINE__);
					return;
				}
				sp->single_ptr_w = sp->var->single_ptr;
				sp->single_ptr = sp->single_ptr_w;
				sp->type = STACK_PTR;
			case STACK_PTR:
				sp--;
				sp[0] = sp[1];
				st_insert(inv->t,&sp->single_ptr_w,inv->top->pc,tmp);
				break;
			case STACK_OUTPUT:
				sp->out->push(sp->outdata);
				sp->out->data(sp->outdata,inv->top->pc,tmp);
			}
			inv->top->pc += tmp;
			break;
		case OP_MVW:
			tmp = *((ushort*)inv->top->pc);
			inv->top->pc += sizeof(ushort);
			switch(sp->type)
			{
			case STACK_PTR:
				st_insert(inv->t,&sp->single_ptr_w,inv->top->pc,tmp);
				break;
			case STACK_OUTPUT:
				sp->out->data(sp->outdata,inv->top->pc,tmp);
			}
			inv->top->pc += tmp;
			break;
		case OP_WIDX:	// replace by OP_OUT ?
			if(_rt_out(inv,&sp,sp,sp + 1))
				_rt_error(inv,__LINE__);
			sp++;
			break;

		// receive input
		case OP_RECV:
			return;

		case OP_SET:
			if(inv->top->is_expr != 0)
				_rt_error(inv,__LINE__);
			else if(sp[1].type != STACK_PROP)
				_rt_error(inv,__LINE__);
			else
			{
				st_insert(inv->t,&sp[1].prop_obj,HEAD_PROPERTY,HEAD_SIZE);
				if(st_insert(inv->t,&sp[1].prop_obj,(cdat)&sp[1].prop_id,PROPERTY_SIZE) == 0)
					st_delete(inv->t,&sp[1].prop_obj,0,0);

				switch(sp->type)
				{
				case STACK_PROP:
					if(_rt_find_prop_value(inv,sp) == 0)
					{}
					break;
				case STACK_RO_PTR:
					// link
					st_link(inv->t,&sp[1].single_ptr_w,&sp->single_ptr);
					break;
				case STACK_PTR:
					// copy 
					st_copy_st(inv->t,&sp[1].single_ptr_w,&sp->single_ptr);
					break;
				case STACK_NUM:
					// bin-num
					st_insert(inv->t,&sp[1].single_ptr_w,HEAD_NUM,HEAD_SIZE);
					st_insert(inv->t,&sp[1].single_ptr_w,(cdat)&sp->num,sizeof(rt_number));
					break;
				case STACK_OBJ:
					// obj-ref
					st_insert(inv->t,&sp[1].single_ptr_w,HEAD_REF,HEAD_SIZE);
					break;
				case STACK_CODE:
					// write out path/event to method/handler
					if(st_move(inv->t,&sp->single_ptr,"p",1) == 0)
						_rt_string_out(inv,sp + 1,sp);
				}
			}
			break;
		case OP_MERGE:
			switch(sp->type)
			{
			case STACK_PROP:
				if(inv->top->is_expr != 0)
					_rt_error(inv,__LINE__);
				else
				{
					st_insert(inv->t,&sp->prop_obj,HEAD_PROPERTY,HEAD_SIZE);
					st_insert(inv->t,&sp->prop_obj,(cdat)&sp->prop_id,PROPERTY_SIZE);
					sp->single_ptr_w = sp->single_ptr = sp->prop_obj;
					sp->type = STACK_PTR;
				}
				break;
			case STACK_REF:
				if(sp->var->type == STACK_NULL)
				{
					st_empty(inv->t,&sp->var->single_ptr);
					sp->var->single_ptr_w = sp->var->single_ptr;
					sp->var->type = STACK_PTR;
				}
				else if(sp->var->type != STACK_PTR)
					_rt_error(inv,__LINE__);
			case STACK_PTR:
			case STACK_OUTPUT:
				break;
			default:
				_rt_error(inv,__LINE__);
			}
			break;
		case OP_2STR:
			tmp = *inv->top->pc++;	// vars
			if(tmp != 1)
			{
				_rt_error(inv,__LINE__);
				break;
			}
			// fall throu
		case OP_CAT:
			{
				struct _rt_stack to;
				st_empty(inv->t,&to.single_ptr);
				to.single_ptr_w = to.single_ptr;
				to.type = STACK_PTR;
				if(_rt_out(inv,&sp,&to,sp))
					_rt_error(inv,__LINE__);
				*sp = to;
			}
			break;
		case OP_OUTL:
			if(sp[1].type == STACK_REF && sp[1].var->type == STACK_NULL)
			{
				*sp[1].var = *sp;
				break;
			}
			// fall throu
		case OP_OUT:	// stream out string
			if(sp[1].type == STACK_REF && _rt_ref_out(inv,&sp,sp))
				_rt_error(inv,__LINE__);
			else if(_rt_out(inv,&sp,sp,sp + 1))
				_rt_error(inv,__LINE__);
			sp++;
			break;
		case OP_NEXT:	// non-string (concat) out-ing [OUT Last Tree]
			if(sp->type == STACK_OUTPUT)
				sp->out->next(sp->outdata);
			break;

		case OP_AVAR:
			inv->top->vars[*inv->top->pc++] = *sp;
			sp++;
			break;
		case OP_DEFP:
			// emit Is2 (branch forward)
			tmp = *inv->top->pc++;	// var
			if(inv->top->vars[tmp].type == STACK_NULL)
			{
				sp--;
				inv->top->vars[tmp].var = sp;
				inv->top->vars[tmp].type = STACK_REF;
				inv->top->pc += sizeof(ushort);
			}
			else
			{
				tmp = *((ushort*)inv->top->pc);
				inv->top->pc += tmp + sizeof(ushort);
			}
			break;
		case OP_END:
			if(inv->top->parent == 0)
			{
				cle_stream_end(inv->hdl);
				return;
			}
			else
			{
				struct _rt_callframe* cf = inv->top;
				inv->top = inv->top->parent;
				sp = inv->top->sp;

				cf->parent = cf->code->free;
				cf->code->free = cf;
			}
			break;
		case OP_DOCALL:
			tmp = *inv->top->pc++;	// params
			if(_rt_call(inv,sp,tmp))
				_rt_error(inv,__LINE__);
			else
			{
				inv->top->parent->sp = sp + 1 + tmp;	// return-stack
				*(--inv->top->sp) = *(sp + 1 + tmp);	// copy output-target
				sp = inv->top->sp;		// set new stack
			}
			break;
		case OP_DOCALL_N:
			tmp = *inv->top->pc++;	// params
			if(_rt_call(inv,sp,tmp))
				_rt_error(inv,__LINE__);
			else
			{
				inv->top->parent->sp = sp + tmp;	// return-stack
				inv->top->sp--;
				inv->top->sp->type = STACK_REF;	// ref to sp-top
				inv->top->sp->var = sp + tmp;
				inv->top->sp->var->type = STACK_NULL;
				sp = inv->top->sp;		// set new stack
			}
			break;
		case OP_ERROR:	// system exception
			return;
		default:
			_rt_error(inv,__LINE__);
		}
	}
}

/*
	load method from "handler" and invoke on "object"
	if method takes any number of parameters -> collect parameters in next, before invoke
*/
static void _rt_start(event_handler* hdl)
{
	struct _rt_invocation* inv = (struct _rt_invocation*)tk_alloc(hdl->instance_tk,sizeof(struct _rt_invocation));
	hdl->handler_data = inv;

	inv->t = hdl->instance_tk;
	inv->code_cache = 0;
	inv->hdl = hdl;
	inv->top = 0;

	if(_rt_load_code(inv,hdl->handler))
	{
		cle_stream_fail(hdl,"runtime:start",14);
		return;
	}
	_rt_newcall(inv,inv->code_cache,&hdl->object,0);

	// push response-pipe
	inv->top->sp--;
	inv->top->sp->out = hdl->response;
	inv->top->sp->outdata = hdl->respdata;
	inv->top->sp->type = STACK_OUTPUT;

	// get parameters before launch?
	inv->params_before_run = inv->code_cache->body.maxparams;

	if(inv->params_before_run == 0)
	{
		hdl->response->start(hdl->respdata);

		_rt_run(inv);
	}
}

static void _rt_next(event_handler* hdl)
{
	struct _rt_invocation* inv = (struct _rt_invocation*)hdl->handler_data;

	if(inv->params_before_run == 0)
	{
		inv->top->sp--;
		inv->top->sp->single_ptr = hdl->top->pt;
		inv->top->sp->type = STACK_RO_PTR;
	}
	else
	{
		struct _rt_stack* var = inv->top->vars + (inv->top->code->body.maxparams - inv->params_before_run);

		var->single_ptr = hdl->top->pt;
		var->type = STACK_RO_PTR;

		if(--inv->params_before_run != 0)
			return;

		hdl->response->start(hdl->respdata);
	}

	_rt_run(inv);
}

static void _rt_end(event_handler* hdl, cdat code, uint length)
{
	struct _rt_invocation* inv = (struct _rt_invocation*)hdl->handler_data;

	if(inv == 0)
	{
		cle_stream_fail(hdl,code,length);
		return;
	}

	if(length == 0 && inv->params_before_run != 0)
	{
		hdl->response->start(hdl->respdata);

		_rt_run(inv);
	}

	if(hdl->eventdata->error == 0)
	{
		// if blocked on get (not failed) -> raise exception
	}
}

cle_syshandler _runtime_handler = {0,{_rt_start,_rt_next,_rt_end,cle_standard_pop,cle_standard_push,cle_standard_data,cle_standard_submit},0};
