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

#define BUFFER_GROW 256

#define ST_0 1
#define ST_ALPHA 2
#define ST_DOT 4
#define ST_STR 8
#define ST_CONF 16
#define ST_VAR 32
#define ST_DEF 64
#define ST_NUM 128
#define ST_NUM_OP 256
#define ST_IF 512
#define ST_CALL 1024

#define PROC_EXPR 0
#define PURE_EXPR 1
#define NEST_EXPR 2

#define TP_ANY 0
#define TP_NUM 1
#define TP_TREE 2
#define TP_STR 4

static struct _cmp_state
{
	task* t;
	FILE* f;
	char* opbuf;
	char* code;
	char* lastop;
	st_ptr root;
	st_ptr strs;
	st_ptr cur_string;

	uint err;

	uint bsize;
	uint top;

	// code-size
	uint code_size;
	uint code_next;

	// top-var
	uint top_var;

	// top-operator
	uint top_op;

	// prg-stack
	uint s_top;
	uint s_max;

	// prg-stack
	uint v_top;
	uint v_max;

	// global level
	uint glevel;

	// parameters
	uint params;

	int c;

	ushort stringidx;
};

static struct _cmp_var
{
	uint prev;
	uint id;
	uint name;
	uint leng;
	uint level;
};
#define PEEK_VAR(v) ((struct _cmp_var*)(cst->opbuf + (v)))

static struct _cmp_op
{
	uint  prev;
	uchar opc;
	uchar prec;
};
#define PEEK_OP(o) ((struct _cmp_op*)(cst->opbuf + (o)))

static const char* keywords[] = {
	"function","body","new","var","if","else","while","null","and","or","not","stream",0
};

static void print_err(int line)
{
	printf("[cmp]error on line %d\n",line);
}

#define err(line) {cst->err++;print_err(line);}

static uint _cmp_expr(struct _cmp_state* cst, uint type, uchar nest);

static int _cmp_nextc(struct _cmp_state* cst)
{
	cst->c = getc(cst->f);
	return cst->c;
}

static void _cmp_check_buffer(struct _cmp_state* cst, uint top)
{
	if(top >= cst->bsize)
	{
		uint diff = top - cst->bsize;

		cst->bsize += BUFFER_GROW > diff? BUFFER_GROW : diff;
		cst->opbuf = (char*)tk_realloc(cst->opbuf,cst->bsize);
	}
}

static uint _cmp_push_struct(struct _cmp_state* cst, uint size)
{
	uint begin = cst->top;
	if(begin == 0)
		begin = 4;
	else if(begin & 3)
		begin += 4 - (cst->top & 3);
	cst->top = begin + size;

	_cmp_check_buffer(cst,cst->top);
	return begin;
}

static void _cmp_check_code(struct _cmp_state* cst, uint top)
{
	if(top >= cst->code_size)
	{
		uint diff = top - cst->code_size;

		cst->code_size += BUFFER_GROW > diff? BUFFER_GROW : diff;
		cst->code = (char*)tk_realloc(cst->code,cst->code_size);
	}
}

static int _cmp_keyword(const char* buffer)
{
	int i;
	for(i = 0; keywords[i]; i++)
	{
		if(strcmp(buffer,keywords[i]) == 0)
		return i;
	}

	return -1;
}

static void _cmp_whitespace(struct _cmp_state* cst)
{
	do{
		_cmp_nextc(cst);
	}
	while(whitespace(cst->c));
}

static uint _cmp_name(struct _cmp_state* cst)
{
	uint op = cst->top;
	while(alphanum(cst->c))
	{
		_cmp_check_buffer(cst,op);
		cst->opbuf[op++] = cst->c;
		_cmp_nextc(cst);
	}

	if(op == cst->top) return 0;

	_cmp_check_buffer(cst,op);
	cst->opbuf[op++] = '\0';
	return op - cst->top;
}

static void _cmp_emit0(struct _cmp_state* cst, uchar opc)
{
	_cmp_check_code(cst,cst->code_next + 1);
	cst->lastop = cst->code + cst->code_next;
	cst->code[cst->code_next++] = opc;
}

static void _cmp_emitS(struct _cmp_state* cst, uchar opc, char* param, ushort len)
{
	_cmp_check_code(cst,cst->code_next + len + 1 + sizeof(ushort));
	cst->lastop = cst->code + cst->code_next;
	cst->code[cst->code_next++] = opc;
	memcpy(cst->code + cst->code_next,(char*)&len,sizeof(ushort));
	cst->code_next += sizeof(ushort);
	memcpy(cst->code + cst->code_next,param,len);
	cst->code_next += len;
}

static void _cmp_emitIc(struct _cmp_state* cst, uchar opc, uchar imm)
{
	_cmp_check_code(cst,cst->code_next + 2);
	cst->lastop = cst->code + cst->code_next;
	cst->code[cst->code_next++] = opc;
	cst->code[cst->code_next++] = imm;
}

static void _cmp_emitIc2(struct _cmp_state* cst, uchar opc, uchar imm1, uchar imm2)
{
	_cmp_check_code(cst,cst->code_next + 3);
	cst->lastop = cst->code + cst->code_next;
	cst->code[cst->code_next++] = opc;
	cst->code[cst->code_next++] = imm1;
	cst->code[cst->code_next++] = imm2;
}

static void _cmp_emitIs(struct _cmp_state* cst, uchar opc, ushort imm)
{
	_cmp_check_code(cst,cst->code_next + 1 + sizeof(ushort));
	cst->lastop = cst->code + cst->code_next;
	cst->code[cst->code_next++] = opc;
	memcpy(cst->code + cst->code_next,(char*)&imm,sizeof(ushort));
	cst->code_next += sizeof(ushort);
}

static void _cmp_emitIs2(struct _cmp_state* cst, uchar opc, uchar imm1, ushort imm2)
{
	_cmp_check_code(cst,cst->code_next + 2 + sizeof(ushort));
	cst->lastop = cst->code + cst->code_next;
	cst->code[cst->code_next++] = opc;
	cst->code[cst->code_next++] = imm1;
	memcpy(cst->code + cst->code_next,(char*)&imm2,sizeof(ushort));
	cst->code_next += sizeof(ushort);
}

static void _cmp_emitII(struct _cmp_state* cst, uchar opc, int imm)
{
	_cmp_check_code(cst,cst->code_next + 1 + sizeof(ushort));
	cst->lastop = cst->code + cst->code_next;
	cst->code[cst->code_next++] = opc;
	memcpy(cst->code + cst->code_next,(char*)&imm,sizeof(int));
	cst->code_next += sizeof(int);
}

static void _cmp_update_imm(struct _cmp_state* cst, uint offset, ushort imm)
{
	ushort* ptr = (ushort*)(cst->code + offset);
	*ptr = imm;
}

static void _cmp_stack(struct _cmp_state* cst, int diff)
{
	cst->s_top += diff;
	if(diff > 0 && cst->s_top > cst->s_max)
		cst->s_max = cst->s_top;
}

static struct _cmp_var* _cmp_find_var(struct _cmp_state* cst, uint length)
{
	struct _cmp_var* var = 0;
	uint nxtvar = cst->top_var;

	while(nxtvar)
	{
		var = PEEK_VAR(nxtvar);
		if(length == var->leng &&
			memcmp(cst->opbuf + cst->top,cst->opbuf + var->name,length) == 0)
			return var;

		nxtvar = var->prev;
	}

	return 0;
}

static struct _cmp_var* _cmp_def_var(struct _cmp_state* cst)
{
	struct _cmp_var* var;
	uint begin,len,top = cst->top;

	if(cst->c != '$') err(__LINE__)
	_cmp_nextc(cst);
	len = _cmp_name(cst);
	if(len == 0)
	{
		err(__LINE__)
		return 0;
	}

	if(_cmp_find_var(cst,len))
	{
		err(__LINE__)
		return 0;
	}

	cst->top += len;	// res.name
	begin = _cmp_push_struct(cst,sizeof(struct _cmp_var));
	var = PEEK_VAR(begin);

	var->prev = cst->top_var;
	var->id   = cst->v_top;
	var->name = top;
	var->leng = len;
	var->level = cst->glevel;

	if(var->id > 0xFF) err(__LINE__)	// max 256 visible vars a any time
	cst->top_var = begin;

	cst->v_top++;
	if(cst->v_top > cst->v_max) cst->v_max = cst->v_top;
	return var;
}

static void _cmp_free_var(struct _cmp_state* cst)
{
	struct _cmp_var* var = 0;
	uint nxtvar = cst->top_var;
	uint from,count = 0;
	cst->glevel--;

	while(nxtvar)
	{
		var = PEEK_VAR(nxtvar);
		if(var->level <= cst->glevel) break;
		count++;
		from = var->id;
		nxtvar = var->prev;
	}

	if(count != 0)
	{
		if(cst->glevel != 0)	// free on glevel==0 done by OP_END
		{
			_cmp_emitIc2(cst,OP_FREE,count,from);
			cst->v_top -= count;
		}

		if(nxtvar)
		{
			cst->top_var = nxtvar;
			cst->top = nxtvar + sizeof(struct _cmp_var);
		}
		else
			cst->top_var = 0;
	}
}

static struct _cmp_var* _cmp_var(struct _cmp_state* cst)
{
	int len;
	_cmp_nextc(cst);
	len = _cmp_name(cst);
	if(len > 0)
	{
		struct _cmp_var* var = _cmp_find_var(cst,len);
		if(var)
			return var;
		else
			err(__LINE__)
	}
	else
		err(__LINE__)

	return 0;
}

static void _cmp_str(struct _cmp_state* cst, uint app)
{
	if(app == 0)
	{
		cst->cur_string = cst->strs;
		st_insert(cst->t,&cst->cur_string,(cdat)&cst->stringidx,sizeof(ushort));
		_cmp_emitIs(cst,OP_STR,cst->stringidx);
		_cmp_stack(cst,1);
		cst->stringidx++;
	}
	
	app = cle_string(cst->f,cst->t,&cst->cur_string,cst->c,&cst->c,app);
	if(app) err(app);
}

// output all operators
static void _cmp_op_clear(struct _cmp_state* cst)
{
	struct _cmp_op* oper;
	uint prev = cst->top_op;

	while(prev)
	{
		oper = PEEK_OP(prev);
		if(oper->opc == 0)
		{
			cst->top_op = oper->prev;
			return;
		}
		prev = oper->prev;
		_cmp_emit0(cst,oper->opc);
		_cmp_stack(cst,-1);
	}

	cst->top_op = 0;
}

// push new operator - release higher precedens first
static void _cmp_op_push(struct _cmp_state* cst, uchar opc, uchar prec)
{
	struct _cmp_op* oper;
	uint begin,prev = cst->top_op;

	while(prev)
	{
		oper = PEEK_OP(prev);
		if(oper->prec >= prec)
		{
			_cmp_emit0(cst,oper->opc);
			_cmp_stack(cst,-1);
		}
		else break;

		prev = oper->prev;
	}

	begin = _cmp_push_struct(cst,sizeof(struct _cmp_op));
	oper = PEEK_OP(begin);

	oper->opc = opc;
	oper->prec = prec;
	oper->prev = prev;

	cst->top_op = begin;
}

static void _cmp_call(struct _cmp_state* cst, uint len, uchar nest)
{
	uint stack = cst->s_top;
	uint pcount = 0;

	_cmp_emitS(cst,OP_CALL,cst->opbuf + cst->top,len);
	_cmp_op_push(cst,0,0xFF);

	do {
		_cmp_expr(cst,TP_ANY,NEST_EXPR);	// construct parameters
		if(stack != cst->s_top)
		{
			_cmp_emitIc(cst,OP_SETP,pcount);
			_cmp_stack(cst,-1);
		}
		pcount++;
	} while(cst->c == ',');
	if(cst->c != ')') err(__LINE__)
	else _cmp_nextc(cst);

	if(nest & NEST_EXPR)
		_cmp_emit0(cst,OP_DOCALL_N);
	else
	{
		_cmp_emit0(cst,OP_DOCALL);
		_cmp_stack(cst,-1);
	}
}

static uint _cmp_var_assign(struct _cmp_state* cst, const uint state)
{
	struct _cmp_var* var;
	uint coff  = cst->code_next;
	uint count = 0;

	_cmp_emitIc(cst,OP_AVARS,0);	// assign-vars
	while(1)
	{
		var = (state == ST_DEF)? _cmp_def_var(cst) : _cmp_var(cst);
		_cmp_check_code(cst,cst->code_next + 1);
		cst->code[cst->code_next++] = var? var->id : 0;

		if(whitespace(cst->c)) _cmp_whitespace(cst);
		if(cst->c == ',')
		{
			_cmp_whitespace(cst);
			if(cst->c == '$')
			{
				count++;
				continue;
			}
		}
		break;
	}

	if(cst->c == '=')
	{
		if(count == 0)
		{
			cst->code_next = coff;			// undo avars
			_cmp_emitIc(cst,OP_AVAR,var? var->id : 0);
			_cmp_stack(cst,1);
			_cmp_expr(cst,TP_ANY,PURE_EXPR);
			_cmp_emit0(cst,OP_POP);
			_cmp_stack(cst,-1);
		}
		else
		{
			count++;
			cst->code[coff + 1] = count;
			count++;
			_cmp_stack(cst,count);
			_cmp_expr(cst,TP_ANY,PURE_EXPR);
			_cmp_emitIs(cst,OP_CAV,cst->code_next - coff + 1 + sizeof(ushort));			// clear-var-assign
			_cmp_stack(cst,-count);
		}
		if(cst->c != ';' && cst->c != '}') err(__LINE__)
		_cmp_nextc(cst);
	}
	else if(state == ST_DEF)
	{
		if(cst->c != ';') err(__LINE__)
		cst->code_next = coff;			// undo avars
		_cmp_nextc(cst);
	}
	else if(count != 0)
	{
		if(cst->c != ';') err(__LINE__)
		cst->code[coff++] = OP_OVARS;
		cst->code[coff++] = count + 1;
	}
	else
	{
		cst->code_next = coff;			// undo avars

		if(var)
		{
			_cmp_emitIc(cst,OP_LVAR,var->id);
			_cmp_stack(cst,1);
			return ST_VAR;
		}
	}
	return ST_0;
}

#define chk_state(legal) if(((legal) & state) == 0) err(__LINE__)

static void _cmp_new(struct _cmp_state* cst)
{
	uint stack = cst->s_top;
	uint state = ST_0;
	uint level = 0;

	_cmp_nextc(cst);
	while(1)
	{
		switch(cst->c)
		{
		case '=':
			chk_state(ST_ALPHA|ST_VAR)
			_cmp_expr(cst,TP_ANY,PURE_EXPR);
			if(cst->c != ';' && cst->c != '}') err(__LINE__)
			_cmp_emit0(cst,OP_POPW);
			_cmp_stack(cst,-1);
			if(level == 0) return;
			state = ST_0;
			break;
		case '{':
			chk_state(ST_0|ST_ALPHA|ST_VAR)
			state = ST_0;
			level++;
			break;
		case '}':
			chk_state(ST_0|ST_ALPHA|ST_VAR)
			if(cst->s_top != stack)
			{
				_cmp_emit0(cst,OP_POPW);
				_cmp_stack(cst,-1);
			}
			if(level > 0) level--; else err(__LINE__)
			if(level == 0) return;
			state = ST_0;
			break;
		case '[':
			chk_state(ST_0|ST_ALPHA|ST_VAR)
			_cmp_expr(cst,TP_STR|TP_NUM,NEST_EXPR);
			if(cst->c != ']') err(__LINE__)
			_cmp_emit0(cst,OP_WIDX);
			state = ST_ALPHA;
			break;
		case '$':
			{
				struct _cmp_var* var = _cmp_var(cst);
				if(var)
					switch(state)
					{
					case ST_0:
						_cmp_emitIc(cst,OP_WVAR,var->id);
						_cmp_stack(cst,1);
						break;
					case ST_DOT:
						_cmp_emitIc(cst,OP_WVAR0,var->id);
						break;
					default:
						err(__LINE__)
					}
			}
			state = ST_VAR;
			continue;
		case ';':
			chk_state(ST_ALPHA|ST_VAR)
			_cmp_emit0(cst,OP_POPW);
			_cmp_stack(cst,-1);
			if(level == 0) return;
			state = ST_0;
			break;
		case '.':
			chk_state(ST_ALPHA|ST_VAR)
			state = ST_DOT;
			break;
		case ' ':
		case '\t':
		case '\n':
		case '\r':
			_cmp_whitespace(cst);
			continue;
		default:
			if(alpha(cst->c))
			{
				uint len = _cmp_name(cst);
				if(_cmp_keyword(cst->opbuf + cst->top) != -1) err(__LINE__)
				switch(state)
				{
				case ST_0:
					_cmp_emitS(cst,OP_DMVW,cst->opbuf + cst->top,len);
					_cmp_stack(cst,1);
					break;
				case ST_DOT:
					_cmp_emitS(cst,OP_MVW,cst->opbuf + cst->top,len);
					break;
				default:
					err(__LINE__)
				}

				if(cst->c == '(')
				{
					// create closure (unimpl)
					err(__LINE__)
				}
				state = ST_ALPHA;
				continue;
			}
			else
			{
				err(__LINE__)
				return;
			}
		}
		_cmp_nextc(cst);
	}
}

#define chk_typ(a) if(type != 0 && ((a) & type) == 0) err(__LINE__) type=(a);

// in nested exprs dont out the first element, but out all following (concating to the first)
#define chk_out() if(state & (ST_ALPHA|ST_STR|ST_VAR)) \
	{chk_typ(TP_STR) _cmp_op_clear(cst); if(nest == NEST_EXPR) \
	{nest = (PURE_EXPR|NEST_EXPR);_cmp_emit0(cst,OP_CAT);} \
	else{_cmp_emit0(cst,OP_OUT);_cmp_stack(cst,-1);}}

// direct call doesnt leave anything on the stack - force it
// if the next instr. needs the return-value
#define chk_call() if(state == ST_CALL && *cst->lastop == OP_DOCALL)\
	{*(cst->code + cst->code_next - 1) = OP_DOCALL_N;_cmp_stack(cst,1);}

#define num_op(opc,prec) \
			chk_typ(TP_NUM)\
			chk_state(ST_ALPHA|ST_VAR|ST_NUM|ST_CALL)\
			chk_call()\
			_cmp_op_push(cst,opc,prec);\
			state = ST_NUM_OP;\

static uint _cmp_expr(struct _cmp_state* cst, uint type, uchar nest)
{
	uint state = ST_0;
	uint level = 0;
	uint coff  = 0;

	_cmp_nextc(cst);
	while(1)
	{
		switch(cst->c)
		{
		case '+':
			num_op(OP_ADD,4)
			break;
		case '-':
			num_op(OP_SUB,4)
			break;
		case '*':
			num_op(OP_MUL,6)
			break;
		case '/':
			num_op(OP_DIV,5)
			break;
		case '%':
			num_op(OP_REM,5)
			break;

		case '<':
			chk_state(ST_ALPHA|ST_VAR|ST_NUM|ST_CALL)
			chk_call()
			state = ST_NUM_OP;
			_cmp_nextc(cst);
			if(cst->c == '=')
			{
				_cmp_op_push(cst,OP_GE,1);
				break;
			}
			else if(cst->c == '>')
			{
				_cmp_op_push(cst,OP_NE,1);
				break;
			}
			_cmp_op_push(cst,OP_GT,1);
			continue;
		case '>':
			chk_state(ST_ALPHA|ST_VAR|ST_NUM|ST_CALL)
			chk_call()
			state = ST_NUM_OP;
			_cmp_nextc(cst);
			if(cst->c == '=')
			{
				_cmp_op_push(cst,OP_LE,1);
				break;
			}
			_cmp_op_push(cst,OP_LT,1);
			continue;
		case '=':
			if(nest & NEST_EXPR)
			{
				chk_state(ST_ALPHA|ST_VAR|ST_NUM|ST_CALL)
				_cmp_op_push(cst,OP_EQ,1);
				state = ST_NUM_OP;
			}
			else
			{
				if(nest) err(__LINE__)	// assign
				chk_state(ST_ALPHA|ST_VAR)
				_cmp_emit0(cst,OP_CLEAR);
				_cmp_expr(cst,TP_ANY,PURE_EXPR);
				if(cst->c != ';' && cst->c != '}') err(__LINE__)
				_cmp_emit0(cst,OP_POP);
				_cmp_stack(cst,-1);
				state = ST_0;
			}
			break;

		case '(':
			chk_state(ST_0|ST_ALPHA|ST_STR|ST_VAR|ST_CALL)
			chk_out()
			_cmp_op_push(cst,0,0xFF);
			type = _cmp_expr(cst,type,NEST_EXPR);
			if(cst->c != ')') err(__LINE__)
			state = ST_ALPHA;
			break;
		case '[':
			if(state == ST_CALL)
			{
				chk_call()
			}
			else
			{
				chk_state(ST_ALPHA|ST_VAR)
			}
			_cmp_op_push(cst,0,0xFF);
			_cmp_expr(cst,TP_STR|TP_NUM,NEST_EXPR);
			if(cst->c != ']') err(__LINE__)
			_cmp_emit0(cst,OP_RIDX);
			_cmp_stack(cst,-1);
			state = ST_ALPHA;
			break;
		case ']':
		case ')':
			chk_state(ST_0|ST_ALPHA|ST_STR|ST_VAR|ST_NUM|ST_CALL)
			_cmp_op_clear(cst);
			if(level != 0) err(__LINE__)
			if(nest & NEST_EXPR) return type;
			err(__LINE__)
			state = ST_0;
			break;
		case '$':	// var (all states ok)
			if(state == ST_0 && !nest)
				state = _cmp_var_assign(cst,ST_0);
			else
			{
				struct _cmp_var* var;
				chk_out()
				var = _cmp_var(cst);
				if(var)
					switch(state)
					{
					case ST_DOT:
					case ST_CONF:
						_cmp_emitIc(cst,OP_RVAR,var->id);
						break;
					default:
						_cmp_emitIc(cst,OP_LVAR,var->id);
						_cmp_stack(cst,1);
					}
				state = ST_VAR;
			}
			continue;
		case '\'':
		case '"':
			chk_state(ST_0|ST_ALPHA|ST_STR|ST_VAR|ST_IF|ST_CALL)
			chk_typ(TP_STR)
			if(state != ST_STR)
				chk_out()
			_cmp_str(cst,state == ST_STR);
			state = ST_STR;
			continue;
		case '{':
			chk_state(ST_0)
			level++;
			cst->glevel++;
			state = ST_0;
			break;
		case '}':
		case ';':
		case ',':
			_cmp_op_clear(cst);
			if((nest & NEST_EXPR) == 0 && (state & (ST_ALPHA|ST_STR|ST_VAR|ST_NUM)))
			{
				_cmp_emit0(cst,type == TP_STR? OP_OUTL : OP_OUTLT);
				_cmp_stack(cst,-1);
			}
			if(cst->c == '}')
			{
				if(level > 0)
				{
					level--;
					_cmp_free_var(cst);
				}
				else err(__LINE__)
			}
			if(level == 0) return type;
			state = ST_0;
			type = TP_ANY;
			break;
		case '.':
			chk_state(ST_ALPHA|ST_VAR|ST_CALL)
			chk_call()
			state = ST_DOT;
			break;
		case ' ':
		case '\t':
		case '\n':
		case '\r':
			_cmp_whitespace(cst);
			continue;
		default:
			if(num(cst->c))
			{
				int val = 0;
				chk_state(ST_0|ST_NUM_OP)
				chk_typ(TP_NUM)
				do
				{
					val *= 10;
					val += cst->c - '0';
					_cmp_nextc(cst);
				}
				while(num(cst->c));

				_cmp_emitII(cst,OP_IMM,val);
				_cmp_stack(cst,1);
				state = ST_NUM;
				continue;
			}
			else if(alpha(cst->c))
			{
				uint len;
				chk_out()
				len = _cmp_name(cst);
				switch(_cmp_keyword(cst->opbuf + cst->top))
				{
				case -1:	// alpha 
					if(cst->c == '(')	// fun-call?
					{
						if(state & ST_DOT == 0)
						{
							_cmp_emit0(cst,OP_FUN);
							_cmp_stack(cst,1);
						}
						if(nest & NEST_EXPR || state & ST_NUM_OP)
						{
							_cmp_call(cst,len,NEST_EXPR);
							state = ST_ALPHA;
						}
						else
						{
							_cmp_call(cst,len,nest);
							state = ST_CALL;
						}
					}
					else if(cst->c == ':')	// load from config
					{
						if(state == ST_DOT) err(__LINE__)
						_cmp_emitS(cst,OP_CMV,cst->opbuf + cst->top,len);
						_cmp_stack(cst,1);
						state = ST_DOT;
						break;
					}
					else if(state & ST_DOT)
					{
						_cmp_emitS(cst,OP_MV,cst->opbuf + cst->top,len);
						state = ST_ALPHA;
					}
					else
					{
						_cmp_emitS(cst,OP_FMV,cst->opbuf + cst->top,len);
						_cmp_stack(cst,1);
						state = ST_ALPHA;
					}
					continue;
				case 2:		// new
					chk_state(ST_0)
					chk_typ(TP_TREE)
					_cmp_new(cst);
					state = ST_0;
					break;
				case 3:		// var
					chk_state(ST_0)
					if(nest) err(__LINE__)
					if(whitespace(cst->c)) _cmp_whitespace(cst);
					state = _cmp_var_assign(cst,ST_DEF);
					continue;
				case 4:		// if (expr) expr [else expr]
					chk_state(ST_0|ST_ALPHA|ST_STR|ST_VAR|ST_NUM_OP|ST_IF|ST_CALL)
					chk_out()
					if(whitespace(cst->c)) _cmp_whitespace(cst);
					if(cst->c != '(') err(__LINE__)
					_cmp_op_push(cst,0,0xFF);
					_cmp_expr(cst,TP_NUM,NEST_EXPR);
					if(cst->c != ')') err(__LINE__)
					coff = cst->code_next;
					_cmp_emitIs(cst,OP_BZ,0);
					_cmp_stack(cst,-1);
					type = _cmp_expr(cst,type,nest);
					_cmp_update_imm(cst,coff + 1,cst->code_next - coff - 1 - sizeof(ushort));
					if(cst->c == ')' || cst->c == ']') return type;
					if(cst->c == 'e') goto do_else;
					state = ST_IF;
					break;
				case 5:		// else
					if(state != ST_IF)
					{
						cst->c = 'e';
						return type;
					}
					else
					{
						uint else_coff;
do_else:
						else_coff = cst->code_next;
						_cmp_emitIs(cst,OP_BR,0);
						// update - we need the "skip-jump" before
						_cmp_update_imm(cst,coff + 1,cst->code_next - coff - 1 - sizeof(ushort));

						type = _cmp_expr(cst,type,nest);
						_cmp_update_imm(cst,else_coff + 1,cst->code_next - else_coff - 1 - sizeof(ushort));
					}
					if(cst->c == ')' || cst->c == ']') return type;
					state = ST_0;

					continue;		// end-outer-expr-as-well
				case 6:		// while
					chk_state(ST_0|ST_ALPHA|ST_STR|ST_VAR|ST_IF|ST_CALL)
					chk_out()
					if(whitespace(cst->c)) _cmp_whitespace(cst);
					if(cst->c != '(') err(__LINE__)
					{
						uint loop_coff = cst->code_next;
						_cmp_op_push(cst,0,0xFF);
						_cmp_expr(cst,TP_NUM,NEST_EXPR);
						if(cst->c != ')') err(__LINE__)
						coff = cst->code_next;
						_cmp_emitIs(cst,OP_BZ,0);
						_cmp_stack(cst,-1);
						type = _cmp_expr(cst,type,nest);
						_cmp_emitIs(cst,OP_LOOP,cst->code_next - loop_coff + 1 + sizeof(ushort));
						_cmp_update_imm(cst,coff + 1,cst->code_next - coff - 1 - sizeof(ushort));
					}
					if(cst->c == ')' || cst->c == ']') return type;
					state = ST_0;
					break;
				case 7:		// null
					chk_state(ST_0|ST_NUM_OP)
					_cmp_emit0(cst,OP_NULL);
					_cmp_stack(cst,1);
					continue;
				case 8:		// and
					chk_state(ST_ALPHA|ST_STR|ST_VAR|ST_IF|ST_CALL)
					coff = cst->code_next;
					_cmp_emitIs(cst,OP_BZ,0);
					type = _cmp_expr(cst,TP_NUM,NEST_EXPR);
					_cmp_update_imm(cst,coff + 1,cst->code_next - coff - 1 - sizeof(ushort));
					state = ST_0;
					continue;
				case 9:		// or
					chk_state(ST_ALPHA|ST_STR|ST_VAR|ST_IF|ST_CALL)
					coff = cst->code_next;
					_cmp_emitIs(cst,OP_BNZ,0);
					type = _cmp_expr(cst,TP_NUM,NEST_EXPR);
					_cmp_update_imm(cst,coff + 1,cst->code_next - coff - 1 - sizeof(ushort));
					state = ST_0;
					continue;
				case 10:	// not
					chk_state(ST_0|ST_ALPHA|ST_STR|ST_VAR|ST_IF|ST_CALL)
					type = _cmp_expr(cst,TP_NUM,NEST_EXPR);
					_cmp_emit0(cst,OP_NOT);
					continue;
				default:
					err(__LINE__)
				}
			}
			else
			{
				err(__LINE__)
				return type;
			}
		}
		_cmp_nextc(cst);
	}
}

// setup call-site and funspace
static int _cmp_header(struct _cmp_state* cst)
{
	st_ptr params;
	uint len;

	_cmp_nextc(cst);

	// get function name
	len = _cmp_name(cst);
	if(!len) return(__LINE__);

	// insert name 
	st_insert(cst->t,&cst->root,cst->opbuf + cst->top,len);

	// clean and mark expr
	st_update(cst->t,&cst->root,HEAD_FUNCTION,HEAD_SIZE);
	cst->strs = cst->root;

	// strings
	st_insert(cst->t,&cst->strs,"S",2);

	params = cst->root;
	st_insert(cst->t,&params,"P",2);

	// get parameters (MUST follow name like Name(...))
	if(cst->c != '(') return(__LINE__);

	_cmp_whitespace(cst);
	if(cst->c == '$')
		while(1)
		{
			struct _cmp_var* var = _cmp_def_var(cst);
			uint coff = cst->code_next;

			_cmp_emitIs2(cst,OP_DEFP,cst->params++,0);

			if(whitespace(cst->c)) _cmp_whitespace(cst);

			// set a default-value
			if(cst->c == '=')
			{
				_cmp_stack(cst,1);
				_cmp_expr(cst,TP_ANY,PURE_EXPR);
				// update branch-offset
				_cmp_update_imm(cst,coff + 2,cst->code_next - coff - 2 - sizeof(ushort));
				// mark optional
				st_append(cst->t,&params,"?",1);
			}
			else
				cst->code_next = coff;	// undo OP_DEFP

			if(var)	// append-param name
				st_append(cst->t,&params,cst->opbuf + var->name,var->leng);

			if(cst->c == '}') _cmp_whitespace(cst);

			if(cst->c == ',')
			{
				_cmp_whitespace(cst);
				if(cst->c == '$') continue;
			}
			break;
		}

	return (cst->c == ')'? 0 : __LINE__);
}

// setup _cmp_state
static void _cmp_init(struct _cmp_state* cst, FILE* f, task* t, st_ptr* ref)
{
	memset(cst,0,sizeof(struct _cmp_state));	// clear struct. MOST vars have default 0 value
	// none-0 defaults:
	cst->t = t;
	cst->f = f;
	cst->root = *ref;
	cst->s_max = cst->s_top = 1;	// output-context
	// begin code
	_cmp_emit0(cst,OP_BODY);
	cst->code_next += sizeof(ushort) + 3;
}

static void _cmp_end(struct _cmp_state* cst)
{
	tk_mfree(cst->opbuf);

	// make body
	if(cst->err == 0)
	{
		ushort* ptr;
		_cmp_emit0(cst,OP_END);

		cst->code[1] = cst->params;	// max-params
		cst->code[2] = cst->v_max;	// max-vars
		cst->code[3] = cst->s_max;	// max-stack
		ptr = (ushort*)(cst->code + 4);
		*ptr = cst->code_next;		// codesize

		st_insert(cst->t,&cst->root,"B",2);
		st_insert(cst->t,&cst->root,cst->code,cst->code_next);
	}

	tk_mfree(cst->code);
}

int cmp_function(FILE* f, task* t, st_ptr* ref)
{
	struct _cmp_state cst;
	int ret;

	_cmp_init(&cst,f,t,ref);

	// create header:
	ret = _cmp_header(&cst);
	if(ret == 0)
	{
		st_ptr anno = cst.root;
		// create annotations
		st_insert(t,&anno,"A",2);
		ret = cle_write(f,t,&anno,0,1);
		if(ret == 0)
		{
			// compile body
			_cmp_expr(&cst,TP_ANY,PROC_EXPR);
			if(cst.c != ';' && cst.c != '}') {cst.err++; print_err(__LINE__);}
		}
		else cst.err++;
	}
	else cst.err++;

	_cmp_end(&cst);
	return getc(f);				// OUT!
}

int cmp_expr(FILE* f, task* t, st_ptr* ref)
{
	struct _cmp_state cst;

	_cmp_init(&cst,f,t,ref);

	// clean and mark expr
	st_update(t,&cst.root,HEAD_EXPR,HEAD_SIZE);
	cst.strs = cst.root;

	// strings
	st_insert(t,&cst.strs,"S",2);

	_cmp_expr(&cst,TP_ANY,PURE_EXPR);
	if(cst.c != ';' && cst.c != '}') {cst.err++; print_err(__LINE__);}

	_cmp_end(&cst);
	return getc(f);				// OUT!
}
