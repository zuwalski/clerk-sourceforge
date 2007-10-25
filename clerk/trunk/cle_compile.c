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
#include "cle_input.h"

#define BUFFER_GROW 256
#define READ_BUFFER 1000

#define ST_0 1
#define ST_ALPHA 2
#define ST_DOT 4
#define ST_STR 8
#define ST_BREAK 16
#define ST_VAR 32
#define ST_DEF 64
#define ST_NUM 128
#define ST_NUM_OP 256
//#define ST_IF 512
//#define ST_CALL 1024

#define PROC_EXPR 0
#define PURE_EXPR 1
#define NEST_EXPR 2

struct _cmp_buffer
{
	st_ptr src;
	uint current,max;
	char buffer[READ_BUFFER];
};

#define NUMBER_OF_SKIPS 32
struct _skips
{
	struct _skips* next;
	long skips[NUMBER_OF_SKIPS];
};

struct _skip_list
{
	struct _skips* top;
	uint glevel;
	uint home;
	uint index;
};

struct _cmp_state
{
	cle_output* response;
	void* data;

	struct _cmp_buffer* src;
	task* t;
	char* opbuf;
	char* code;
	char* lastop;
	struct _skips* freeskip;
	st_ptr root;
	st_ptr strs;
	st_ptr cur_string;

	uint err;

	uint bsize;
	uint top;

	// code-size
	uint code_size;
	uint code_next;

	// code-offset of first handler (if any)
	uint first_handler;

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
	int glevel;

	// parameters
	uint params;

	int c;

	ushort stringidx;
};

struct _cmp_var
{
	uint prev;
	uint id;
	uint name;
	uint leng;
	uint level;
};
#define PEEK_VAR(v) ((struct _cmp_var*)(cst->opbuf + (v)))

struct _cmp_op
{
	uint  prev;
	uchar opc;
	uchar prec;
};
#define PEEK_OP(o) ((struct _cmp_op*)(cst->opbuf + (o)))

static const char* keywords[] = {
	"do","end","if","elseif","else","while","repeat","until","send",
	"var","new","each","for","break","and","or","not","null",
	"handle","raise","switch","case","default",0
//	"application","type","extends","as","num","text","time","tree","list","data","event",0
};

#define KW_MAX_LEN 7

enum cmp_keywords {
	KW_DO = 1,
	KW_END,
	KW_IF,
	KW_ELSEIF,
	KW_ELSE,
	KW_WHILE,
	KW_REPEAT,
	KW_UNTIL,
	KW_SEND,
	KW_VAR,
	KW_NEW,
	KW_EACH,
	KW_FOR,
	KW_BREAK,
	KW_AND,
	KW_OR,
	KW_NOT,
	KW_NULL,
	KW_HANDLE,
	KW_RAISE,
	KW_SWITCH,
	KW_CASE,
	KW_DEFAULT
};

struct _cmp_buildin
{
	const char* id;
	uint opcode_0;
	uint opcode_dot;
	uint opcode_out;
	uint opcode_num;
};

static const struct _cmp_buildin buildins[] = {
	{"event",0,OP_NULL,0,0},			// register event-handler on object
	{"void",OP_NULL,0,0,0},				// throw away values
	{"recv",OP_NULL,0,0,0},				// recieve data-structure from event-queue
	{"delete",0,OP_NULL,0,0},			// delete object or delete sub-tree
	{"first",0,OP_NULL,0,0},
	{"last",0,OP_NULL,0,0},
	{0,0,0,0,0}	// STOP
};

static char str_err[] = "[cmp]err ";

static void print_err(struct _cmp_state* cst, int line)
{
	char bf[6];
	int i;
	cst->response->data(cst->data,str_err,sizeof(str_err));

	for(i = 4; i != 0 && line > 0; i--)
	{
		bf[i] = '0' + (line % 10);
		line /= 10;
	}

	bf[5] = '\n';
	i++;
	cst->response->data(cst->data,bf + i,sizeof(bf) - i);
}

#define err(line) {cst->err++;print_err(cst,line);}

static int _cmp_expr(struct _cmp_state* cst, struct _skip_list* skips, uchar nest);

static struct _skips* _cmp_new_skips(struct _cmp_state* cst)
{
	struct _skips* skips = cst->freeskip;

	if(skips)
		cst->freeskip = skips->next;
	else
		skips = (struct _skips*)tk_alloc(cst->t,sizeof(struct _skips));

	skips->next = 0;
	return skips;
}

static void _cmp_new_skiplist(struct _cmp_state* cst, struct _skip_list* list)
{
	list->top = 0;
	list->glevel = cst->glevel;
	list->home = cst->code_next;
	list->index = 0;
}

static void _cmp_free_skiplist(struct _cmp_state* cst, struct _skip_list* list, uint jmpto)
{
	while(list->top)
	{
		struct _skips* tmp;

		while(list->index-- > 0)
		{
			ushort* ptr = (ushort*)(cst->code + list->top->skips[list->index]);
			*ptr = (jmpto - list->top->skips[list->index] - sizeof(ushort));
		}

		tmp = list->top->next;
		list->top->next = cst->freeskip;
		cst->freeskip = list->top;
		list->top = tmp;
	}
}

static void _cmp_add_to_skiplist(struct _cmp_state* cst, struct _skip_list* list, long entry)
{
	if(list->top == 0)
	{
		list->top = _cmp_new_skips(cst);
	}
	else if(list->index == NUMBER_OF_SKIPS)
	{
		struct _skips* tmp = _cmp_new_skips(cst);
		tmp->next = list->top;

		list->top = tmp;
		list->index = 0;
	}

	list->top->skips[list->index++] = entry;
}

static int _cmp_next_bf(struct _cmp_buffer* bf)
{
	if(bf->current >= bf->max)
	{
		bf->max = st_get(&bf->src,bf->buffer,READ_BUFFER);

		bf->current = 1;

		return (bf->max > 0)? bf->buffer[0] : -1;
	}

	return bf->buffer[bf->current++];
}

static int _cmp_nextc(struct _cmp_state* cst)
{
	if(cst->c >= 0)
		cst->c = _cmp_next_bf(cst->src);

	return cst->c;
}

static int _cmp_comment_bf(struct _cmp_buffer* bf)
{
	int c;
	do {
		c = _cmp_next_bf(bf);
	} while(c > 0 && c != '\n' && c != '\r');
	return c;
}

#define _cmp_comment(cst) _cmp_comment_bf(cst->src)

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

// TODO: replace by static-hash
static int _cmp_keyword(const char* buffer)
{
	int i;
	for(i = 0; keywords[i]; i++)
	{
		if(strcmp(buffer,keywords[i]) == 0)
		return i + 1;
	}

	return 0;
}

// TODO: replace by static-hash
static const struct _cmp_buildin* _cmp_buildins(const char* buffer)
{
	int i;
	for(i = 0; buildins[i].id; i++)
	{
		if(strcmp(buffer,buildins[i].id) == 0)
		return &buildins[i];
	}

	return 0;
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

static void _cmp_compute_free_var(struct _cmp_state* cst, uint to_glevel)
{
	struct _cmp_var* var = 0;
	uint nxtvar = cst->top_var;
	uint from,count = 0;

	while(nxtvar)
	{
		var = PEEK_VAR(nxtvar);
		if(var->level <= to_glevel) break;
		count++;
		from = var->id;
		nxtvar = var->prev;
	}

	if(count != 0 && cst->glevel != 0)
		_cmp_emitIc2(cst,OP_FREE,count,from);
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

/* "test ""test"" 'test'" | 'test ''test'' "test"' */
static void _cmp_string(struct _cmp_state* cst, st_ptr* out, int c, uchar append)
{
	char buffer[BUFFER_GROW];
	int ic = 0,i = 0;

	if(!append)
		st_update(cst->t,out,HEAD_STR,HEAD_SIZE);

	while(1)
	{
		ic = _cmp_nextc(cst);
		if(ic == c)
		{
			ic = _cmp_nextc(cst);
			if(ic != c)
				break;
		}
		else if(ic <= 0)
		{
			err(__LINE__)
			return;
		}

		buffer[i++] = ic;
		if(i == BUFFER_GROW)
		{
			if(st_append(cst->t,out,buffer,i)) err(__LINE__)
			i = 0;
		}
	}

//	buffer[i++] = '\0';
	if(st_append(cst->t,out,buffer,i)) err(__LINE__)
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
	
	_cmp_string(cst,&cst->cur_string,cst->c,app);
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

static uint _cmp_buildin_parameters(struct _cmp_state* cst)
{
	uint term,pcount = 0;

	if(cst->c != '(')
	{
		err(__LINE__)
		return 0;
	}

	_cmp_nextc(cst);

	do {
		uint stack = cst->s_top;
		term = _cmp_expr(cst,0,NEST_EXPR);	// construct parameters
		if(pcount != 0 || term != ')')
		{
			if(stack == cst->s_top)
				_cmp_emit0(cst,OP_NULL);
			pcount++;
		}
	} while(term == ',');
	if(term != ')') err(__LINE__)
	else _cmp_nextc(cst);

	_cmp_stack(cst,-pcount);
	return pcount;
}

static void _cmp_call(struct _cmp_state* cst, uchar nest)
{
	uint term,pcount = 0;

	_cmp_emit0(cst,OP_CALL);
	_cmp_op_push(cst,0,0xFF);

	do {
		uint stack = cst->s_top;
		term = _cmp_expr(cst,0,NEST_EXPR);	// construct parameters
		if(stack != cst->s_top)
		{
			_cmp_emitIc(cst,OP_SETP,pcount);
			_cmp_stack(cst,-1);
		}
		pcount++;
	} while(term == ',');
	if(term != ')') err(__LINE__)
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
		_cmp_nextc(cst);
		if(count == 0)
		{
			cst->code_next = coff;			// undo avars
			_cmp_emitIc(cst,OP_AVAR,var? var->id : 0);
			_cmp_stack(cst,1);
			if(_cmp_expr(cst,0,PURE_EXPR) != ';') err(__LINE__)
			_cmp_emit0(cst,OP_POP);
			_cmp_stack(cst,-1);
		}
		else
		{
			uint term;
			count++;
			cst->code[coff + 1] = count;
			count++;
			_cmp_stack(cst,count);
			do {
				term = _cmp_expr(cst,0,NEST_EXPR);
			} while(term == ',');
			if(term != ';') err(__LINE__)
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
		_cmp_nextc(cst);
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

static void _cmp_for(struct _cmp_state* cst)
{
	uint count = 0;
	uint coff  = cst->code_next;
	_cmp_emitIc(cst,OP_AVARS,0);	// for-assign-vars

	do
	{
		if(whitespace(cst->c)) _cmp_whitespace(cst);

		if(cst->c == '$')
		{
			uint len;
			_cmp_nextc(cst);
			len = _cmp_name(cst);
			if(len > 0)
			{
				struct _cmp_var* var = _cmp_find_var(cst,len);
				if(var == 0)
					var = _cmp_def_var(cst);

				_cmp_check_code(cst,cst->code_next + 1);
				cst->code[cst->code_next++] = var? var->id : 0;

				count++;
			}
			else
			{
				err(__LINE__)
				break;
			}
		}
		else err(__LINE__)

		if(whitespace(cst->c)) _cmp_whitespace(cst);
	}
	while(cst->c == ',');
	if(cst->c != '=') err(__LINE__)
	cst->code[coff + 1] = count;

	_cmp_nextc(cst);
	if(_cmp_expr(cst,0,NEST_EXPR) != 'd') err(__LINE__)
}

#define chk_state(legal) if(((legal) & state) == 0) err(__LINE__)

/*
* path.+|-$bindvar{path_must_exsist;other.$endvar;+|-*.$x;}
*/
static void _cmp_it_expr(struct _cmp_state* cst)
{
	uint state = ST_0;
	uint level = 0;

	while(1)
	{
		switch(cst->c)
		{
		case '+':
		case '-':
			chk_state(ST_0|ST_DOT)
			state = ST_NUM_OP;
			break;
		case '{':
			chk_state(ST_0|ST_ALPHA|ST_VAR)
			level++;
			state = ST_0;
			break;
		case '}':
			chk_state(ST_0)
			if(level > 0) level--;
			else err(__LINE__)
			if(level == 0) return;
			state = ST_0;
			break;
		case ';':
			chk_state(ST_ALPHA|ST_VAR)
			if(level == 0) return;
			state = ST_0;
			break;
		case '.':
			chk_state(ST_ALPHA|ST_VAR)
			state = ST_DOT;
			break;
		case '$':
			chk_state(ST_0|ST_DOT)
			{
				uint len;
				_cmp_nextc(cst);
				len = _cmp_name(cst);
				if(len > 0)
				{
					struct _cmp_var* var = _cmp_find_var(cst,len);
					if(var == 0)
						var = _cmp_def_var(cst);
				}
			}
			state = ST_VAR;
			break;
		case '*':
			chk_state(ST_0|ST_DOT|ST_NUM_OP)
			state = ST_ALPHA;
			break;
		case '#':
			_cmp_comment(cst);
			break;
		default:
			chk_state(ST_0|ST_DOT|ST_NUM_OP)
			if(alpha(cst->c))
			{
				uint len = _cmp_name(cst);
				if(len <= KW_MAX_LEN && _cmp_keyword(cst->opbuf + cst->top) != 0) err(__LINE__)

				state = ST_ALPHA;
			}
			else
			{
				err(__LINE__)
				return;
			}
		}
	}
}

static void _cmp_new(struct _cmp_state* cst)
{
	uint stack = cst->s_top;
	uint state = ST_0;
	uint level = 1;

	_cmp_emit0(cst,OP_NULL);	// OP_?? validate write

	_cmp_nextc(cst);
	while(1)
	{
		switch(cst->c)
		{
		case '=':
			chk_state(ST_ALPHA)
			_cmp_nextc(cst);
			if(_cmp_expr(cst,0,PURE_EXPR) != ';') err(__LINE__)
			_cmp_emit0(cst,OP_POPW);
			_cmp_stack(cst,-1);
			state = ST_0;
			break;
		case '{':
			chk_state(ST_0|ST_ALPHA)
			state = ST_0;
			level++;
			break;
		case '}':
			chk_state(ST_0|ST_ALPHA)
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
			chk_state(ST_0|ST_ALPHA)
			_cmp_nextc(cst);
			if(_cmp_expr(cst,0,NEST_EXPR) != ']') err(__LINE__);
			_cmp_emit0(cst,OP_WIDX);
			state = ST_ALPHA;
			break;
		case ',':
		case ';':
			chk_state(ST_ALPHA)
			_cmp_emit0(cst,OP_POPW);
			_cmp_stack(cst,-1);
			state = ST_0;
			break;
		case '.':							// ONLY Alpha -> build path incl dots
			chk_state(ST_ALPHA)
			state = ST_DOT;
			break;
		case '#':
			_cmp_comment(cst);
			break;
		case ' ':
		case '\t':
		case '\n':
		case '\r':
			break;
		default:
			if(alpha(cst->c))
			{
				uint len = _cmp_name(cst);
				if(len <= KW_MAX_LEN && _cmp_keyword(cst->opbuf + cst->top) != 0) err(__LINE__)
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

// in nested exprs dont out the first element, but out all following (concating to the first)
#define chk_out() if(state & (ST_ALPHA|ST_STR|ST_VAR|ST_NUM)) \
	{_cmp_op_clear(cst); if(nest == NEST_EXPR) \
	{nest = (PURE_EXPR|NEST_EXPR);_cmp_emit0(cst,OP_CAT);} \
	else{_cmp_emit0(cst,OP_OUT);_cmp_stack(cst,-1);}}

#define end_expr() \
	_cmp_op_clear(cst);\
	if((nest & NEST_EXPR) == 0 && (state & (ST_ALPHA|ST_STR|ST_VAR|ST_NUM)))\
	{_cmp_emit0(cst,OP_OUT);_cmp_stack(cst,-1);}

// direct call doesnt leave anything on the stack - force it
// if the next instr. needs the return-value
#define chk_call() if(*cst->lastop == OP_DOCALL)\
	{*(cst->code + cst->code_next - 1) = OP_DOCALL_N;_cmp_stack(cst,1);}

#define num_op(opc,prec) \
			chk_state(ST_ALPHA|ST_VAR|ST_NUM)\
			chk_call()\
			_cmp_op_push(cst,opc,prec);\
			state = ST_NUM_OP;\

static int _cmp_block_expr(struct _cmp_state* cst, struct _skip_list* skips, uchar nest)
{
	uint exittype;
	cst->glevel++;
	while(1)
	{
		exittype = _cmp_expr(cst,skips,nest);
		if(exittype == ';') _cmp_nextc(cst);
		else break;
	}
	_cmp_free_var(cst);
	return exittype;
}

static int _cmp_block_expr_nofree(struct _cmp_state* cst, struct _skip_list* skips, uchar nest)
{
	uint exittype;
	cst->glevel++;
	while(1)
	{
		exittype = _cmp_expr(cst,skips,nest);
		if(exittype == ';') _cmp_nextc(cst);
		else break;
	}
	return exittype;
}

static void _cmp_fwd_loop(struct _cmp_state* cst, uint loop_coff, uchar nest, uchar opc)
{
	struct _skip_list sl;
	uint coff = cst->code_next;
	_cmp_new_skiplist(cst,&sl);
	_cmp_emitIs(cst,opc,0);
	_cmp_stack(cst,-1);
	if(_cmp_block_expr_nofree(cst,&sl,nest) != 'e') err(__LINE__)
	_cmp_emitIs(cst,OP_LOOP,cst->code_next - loop_coff + 1 + sizeof(ushort));
	_cmp_update_imm(cst,coff + 1,cst->code_next - coff - 1 - sizeof(ushort));
	_cmp_free_skiplist(cst,&sl,cst->code_next);
	_cmp_free_var(cst);
}

static int _cmp_expr(struct _cmp_state* cst, struct _skip_list* skips, uchar nest)
{
	uint state = ST_0;
	uint dbg = cst->code_next + 1;

	_cmp_emitIs(cst,OP_DEBUG,0);	// OP_DEBUG (file-ptr)

	while(1)
	{
		switch(cst->c)
		{
		case -1:
			chk_state(ST_0)
			if(cst->glevel != 0) err(__LINE__)
			return -1;
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
			chk_state(ST_ALPHA|ST_VAR|ST_NUM)
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
			chk_state(ST_ALPHA|ST_VAR|ST_NUM)
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
			_cmp_nextc(cst);
			if(cst->c == '=')
			{
				chk_state(ST_ALPHA|ST_VAR|ST_NUM)
				_cmp_op_push(cst,OP_EQ,1);
				state = ST_NUM_OP;
				break;
			}
			else
			{	
				if(nest) err(__LINE__)
				chk_state(ST_ALPHA)
				_cmp_emit0(cst,OP_CLEAR);
				if(_cmp_expr(cst,0,PURE_EXPR) != ';') err(__LINE__)
				_cmp_emit0(cst,OP_POP);
				_cmp_stack(cst,-1);
				state = ST_0;
				continue;
			}
		case '$':	// var (all states ok)
			if(state == ST_0 && !(nest & NEST_EXPR))
				state = _cmp_var_assign(cst,ST_0);
			else
			{
				struct _cmp_var* var;
				if(state == ST_DOT) err(__LINE__)
				chk_out()
				var = _cmp_var(cst);
				if(var)
				{
					_cmp_emitIc(cst,OP_LVAR,var->id);
					_cmp_stack(cst,1);
				}
				state = ST_VAR;
			}
			continue;
		case '\'':
		case '"':
			chk_state(ST_0|ST_ALPHA|ST_STR|ST_VAR)
			if(state != ST_STR)
				chk_out()
			_cmp_str(cst,state == ST_STR);
			state = ST_STR;
			continue;
		case '(':
			// st_alpha|st_var -> function-call
			if(state & (ST_ALPHA|ST_VAR))
				_cmp_call(cst,nest);
			else
			{
				chk_state(ST_0|ST_STR)
				chk_out()
				_cmp_op_push(cst,0,0xFF);
				_cmp_nextc(cst);
				if(_cmp_expr(cst,0,NEST_EXPR) != ')') err(__LINE__)
			}
			state = ST_ALPHA;
			break;
		case '[':
			chk_state(ST_0|ST_ALPHA|ST_VAR)
			chk_call()
			if(state == ST_0)
			{
				_cmp_emit0(cst,OP_NULL);	// OP_LFUN
				_cmp_stack(cst,1);
			}
			_cmp_op_push(cst,0,0xFF);
			_cmp_nextc(cst);
			if(_cmp_expr(cst,0,NEST_EXPR) != ']') err(__LINE__);
			_cmp_emit0(cst,OP_RIDX);
			_cmp_stack(cst,-1);
			state = ST_ALPHA;
			break;
		case ']':
		case ')':
			chk_state(ST_0|ST_ALPHA|ST_STR|ST_VAR|ST_NUM)
			_cmp_op_clear(cst);
			if((nest & NEST_EXPR) == 0) err(__LINE__)
			if(nest & PURE_EXPR)
			{
				_cmp_emit0(cst,OP_OUT);
				_cmp_stack(cst,-1);
			}
			_cmp_update_imm(cst,dbg,cst->code_next - dbg - sizeof(ushort));
			return cst->c;
		case '{':
			chk_state(ST_0|ST_ALPHA|ST_VAR)
			chk_call()
			_cmp_new(cst);
			state = ST_0;
			break;
		case ';':
		case ',':
			chk_state(ST_0|ST_ALPHA|ST_STR|ST_VAR|ST_NUM)
			_cmp_op_clear(cst);
			if(state != ST_0)
			{
				_cmp_emit0(cst,OP_OUTLT);
				_cmp_stack(cst,-1);
			}
			_cmp_update_imm(cst,dbg,cst->code_next - dbg - sizeof(ushort));
			return cst->c;
		case '.':
			chk_state(ST_ALPHA|ST_VAR)
			chk_call()
			state = ST_DOT;
			break;
		case '#':
			_cmp_comment(cst);
			break;
		case ' ':
		case '\t':
		case '\n':
		case '\r':
			break;
		default:
			if(num(cst->c))
			{
				int val = 0;
				chk_state(ST_0|ST_NUM_OP)
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
				len = _cmp_name(cst);
				switch(len > KW_MAX_LEN? 0 :_cmp_keyword(cst->opbuf + cst->top))
				{
				case KW_DO:
					end_expr()
					chk_state(ST_0|ST_ALPHA|ST_STR|ST_VAR|ST_NUM)
					if(nest & NEST_EXPR)
					{
						_cmp_update_imm(cst,dbg,cst->code_next - dbg - sizeof(ushort));
						return 'd';
					}

					if(_cmp_block_expr(cst,skips,nest) != 'e') err(__LINE__)
					state = ST_0;
					continue;
				case KW_END:
					end_expr()
					chk_state(ST_0|ST_ALPHA|ST_STR|ST_VAR|ST_NUM|ST_BREAK)
					_cmp_update_imm(cst,dbg,cst->code_next - dbg - sizeof(ushort));
					return 'e';
				case KW_IF:	// if expr do bexpr [elseif expr do bexpr [else bexpr]] end
					end_expr()
					chk_state(ST_0|ST_ALPHA|ST_STR|ST_VAR|ST_NUM)
					{
						struct _skip_list sl;
						_cmp_new_skiplist(cst,&sl);
						while(1)
						{
							uint term,coff;
							if(_cmp_expr(cst,0,NEST_EXPR) != 'd') err(__LINE__)
							coff = cst->code_next;
							_cmp_emitIs(cst,OP_BZ,0);
							_cmp_stack(cst,-1);
							term = _cmp_block_expr(cst,skips,nest);

							if(term == 'i' || term == 'l')
							{
								_cmp_add_to_skiplist(cst,&sl,cst->code_next + 1);
								_cmp_emitIs(cst,OP_BR,0);
								// update - we need the "skip-jump" before
								_cmp_update_imm(cst,coff + 1,cst->code_next - coff - 1 - sizeof(ushort));

								if(term == 'i') continue;

								term = _cmp_block_expr(cst,skips,nest);
							}
							else
								_cmp_update_imm(cst,coff + 1,cst->code_next - coff - 1 - sizeof(ushort));

							if(term != 'e') err(__LINE__)	// must end with "end"
							break;
						}
						_cmp_free_skiplist(cst,&sl,cst->code_next);
					}
					continue;
				case KW_ELSEIF:
					end_expr()
					chk_state(ST_0|ST_ALPHA|ST_STR|ST_VAR|ST_NUM|ST_BREAK)
					_cmp_update_imm(cst,dbg,cst->code_next - dbg - sizeof(ushort));
					return 'i';
				case KW_ELSE:
					end_expr()
					chk_state(ST_0|ST_ALPHA|ST_STR|ST_VAR|ST_NUM|ST_BREAK)
					_cmp_update_imm(cst,dbg,cst->code_next - dbg - sizeof(ushort));
					return 'l';
				case KW_WHILE:	// while expr do bexpr end
					end_expr()
					chk_state(ST_0|ST_ALPHA|ST_STR|ST_VAR)
					{
						uint loop_coff = cst->code_next;
						if(_cmp_expr(cst,0,NEST_EXPR) != 'd') err(__LINE__)
						_cmp_fwd_loop(cst,loop_coff,nest,OP_BZ);
					}
					state = ST_0;
					continue;
				case KW_REPEAT:		// repeat bexpr until expr end
					end_expr()
					chk_state(ST_0|ST_ALPHA|ST_STR|ST_VAR)
					{
						struct _skip_list sl;
						uint loop_coff = cst->code_next;
						_cmp_new_skiplist(cst,&sl);
						if(_cmp_block_expr_nofree(cst,&sl,nest) != 'u') err(__LINE__)
						_cmp_emitIs(cst,OP_LOOP,cst->code_next - loop_coff + 1 + sizeof(ushort));	// OP_NZ_LOOP
						_cmp_stack(cst,-1);
						_cmp_free_skiplist(cst,&sl,cst->code_next);
						_cmp_free_var(cst);
					}
					state = ST_0;
					continue;
				case KW_UNTIL:
					end_expr()
					chk_state(ST_0|ST_ALPHA|ST_STR|ST_VAR|ST_BREAK)
					if(_cmp_expr(cst,0,NEST_EXPR) != 'e') err(__LINE__)
					_cmp_update_imm(cst,dbg,cst->code_next - dbg - sizeof(ushort));
					return 'u';
				case KW_SEND:	// send expr do bexpr end
					chk_state(ST_0)
					if(nest) err(__LINE__)
					if(_cmp_expr(cst,0,NEST_EXPR) != 'd') err(__LINE__)
					_cmp_emit0(cst,OP_NULL);	// OP_SEND
					// TODO: handle skips ...
					if(_cmp_block_expr(cst,0,PROC_EXPR) != 'e') err(__LINE__)
					_cmp_emit0(cst,OP_NULL);	// OP_POP_SEND
					_cmp_stack(cst,-1);
					state = ST_0;
					continue;
				case KW_VAR:
					chk_state(ST_0)
					if(nest & NEST_EXPR) err(__LINE__)
					if(whitespace(cst->c)) _cmp_whitespace(cst);
					state = _cmp_var_assign(cst,ST_DEF);
					continue;
				case KW_NEW:					// .new | new typename(param_list) | new $var(param_list)
					chk_state(ST_DOT)
					_cmp_emit0(cst,OP_NULL);	// OP_NEW
					continue;
				case KW_EACH:	// .each it_expr do bexpr end
					chk_state(ST_DOT)
					{
						uint loop_coff = cst->code_next;
						cst->glevel++;
						_cmp_it_expr(cst);
						_cmp_fwd_loop(cst,loop_coff,nest,OP_BZ);	// OP_EACH
						_cmp_free_var(cst);
					}
					state = ST_0;
					continue;
				case KW_FOR:	// for var_list : expr do bexpr end
					end_expr()
					chk_state(ST_0|ST_ALPHA|ST_STR|ST_VAR)
					{
						uint loop_coff = cst->code_next;
						cst->glevel++;
						_cmp_for(cst);
						_cmp_fwd_loop(cst,loop_coff,nest,OP_BZ);	// OP_FOR
						_cmp_free_var(cst);
					}
					state = ST_0;
					continue;
				case KW_BREAK:
					end_expr()
					chk_state(ST_0|ST_ALPHA|ST_STR|ST_VAR)
					if(skips == 0) err(__LINE__)
					else
					{
						_cmp_compute_free_var(cst,skips->glevel + 1);
						_cmp_add_to_skiplist(cst,skips,cst->code_next + 1);
						_cmp_emitIs(cst,OP_BR,0);
					}
					state = ST_BREAK;
					continue;
				case KW_AND:
					chk_state(ST_ALPHA|ST_STR|ST_VAR)
					{
						uint term,coff = cst->code_next;
						_cmp_emitIs(cst,OP_BZ,0);			// TODO: no-pop BZ
						_cmp_stack(cst,-1);
						term = _cmp_expr(cst,skips,nest);
						_cmp_update_imm(cst,coff + 1,cst->code_next - coff - 1 - sizeof(ushort));
						_cmp_update_imm(cst,dbg,cst->code_next - dbg - sizeof(ushort));
						return term;
					}
				case KW_OR:
					chk_state(ST_ALPHA|ST_STR|ST_VAR)
					{
						uint term,coff = cst->code_next;
						_cmp_emitIs(cst,OP_BNZ,0);			// TODO: no-pop BNZ
						_cmp_stack(cst,-1);
						term = _cmp_expr(cst,skips,nest);
						_cmp_update_imm(cst,coff + 1,cst->code_next - coff - 1 - sizeof(ushort));
						_cmp_update_imm(cst,dbg,cst->code_next - dbg - sizeof(ushort));
						return term;
					}
				case KW_NOT:
					chk_state(ST_0|ST_ALPHA|ST_STR|ST_VAR)
					{
						uint term;
						term = _cmp_expr(cst,skips,nest);
						_cmp_emit0(cst,OP_NOT);
						_cmp_update_imm(cst,dbg,cst->code_next - dbg - sizeof(ushort));
						return term;
					}
				case KW_NULL:
					chk_state(ST_0|ST_NUM_OP)
					_cmp_emit0(cst,OP_NULL);
					_cmp_stack(cst,1);
					continue;
				case KW_HANDLE:	// handle it_expr do bexpr end
					chk_state(ST_0)
					if(nest & NEST_EXPR) err(__LINE__)
					if(cst->glevel) err(__LINE__)
					_cmp_emit0(cst,OP_END);		// end func/expr here - begin handler-code
					if(cst->first_handler == 0) cst->first_handler = cst->code_next;

					_cmp_it_expr(cst);

					// TODO: no match? Skip to next handler...
					return _cmp_block_expr(cst,0,nest);
				case KW_RAISE:	// raise {new}
					chk_state(ST_0)
					if(nest & NEST_EXPR) err(__LINE__)
					if(whitespace(cst->c)) _cmp_whitespace(cst);
					_cmp_emit0(cst,OP_NULL);	// OP_PRE_RAISE
					_cmp_stack(cst,1);
					if(cst->c != '{') err(__LINE__)
					else _cmp_new(cst);
					_cmp_emit0(cst,OP_NULL);	// OP_RAISE
					_cmp_stack(cst,-1);
					state = ST_0;
					continue;
				case KW_SWITCH:		// switch expr case const do bexpr | default bexpr | end
					end_expr()
					chk_state(ST_0|ST_ALPHA|ST_STR|ST_VAR)
					{
						struct _skip_list sl;
						uint term = _cmp_expr(cst,0,NEST_EXPR);
						_cmp_new_skiplist(cst,&sl);

						while(term != 'e')
						{
							switch(term)
							{
							case 'c':
								// todo: parse constant and add to map
								term = _cmp_block_expr(cst,&sl,nest);
								break;
							case 'f':
								term = _cmp_block_expr(cst,&sl,nest);
								break;
							case 'e':
								break;
							default:
								err(__LINE__)
								term = 'e';
							}
						}

						_cmp_free_skiplist(cst,&sl,cst->code_next);
					}
					state = ST_0;
					continue;
				case KW_CASE:
					end_expr()
					chk_state(ST_0|ST_ALPHA|ST_STR|ST_VAR|ST_BREAK)
					_cmp_update_imm(cst,dbg,cst->code_next - dbg - sizeof(ushort));
					return 'c';
				case KW_DEFAULT:
					end_expr()
					chk_state(ST_0|ST_ALPHA|ST_STR|ST_VAR|ST_BREAK)
					_cmp_update_imm(cst,dbg,cst->code_next - dbg - sizeof(ushort));
					return 'f';
				default:
					chk_out()
					if(cst->c == ':')	// load from config
					{
						if(state & ST_DOT) err(__LINE__)
						_cmp_emitS(cst,OP_CMV,cst->opbuf + cst->top,len);
						_cmp_stack(cst,1);
						state = ST_DOT;
						break;
					}
					else
					{
						// compile buildin-functions
						const struct _cmp_buildin* cmd = _cmp_buildins(cst->opbuf + cst->top);

						if(cmd != 0)
						{
							uint params = _cmp_buildin_parameters(cst);

							switch(state)
							{
							case ST_0:
								if(cmd->opcode_0)
									_cmp_emitIc(cst,cmd->opcode_0,params);
								else err(__LINE__)
								break;
							case ST_DOT:
								if(cmd->opcode_dot)
									_cmp_emitIc(cst,cmd->opcode_dot,params);
								else err(__LINE__)
								break;
							case ST_ALPHA:
							case ST_VAR:
							case ST_STR:
								if(cmd->opcode_out)
									_cmp_emitIc(cst,cmd->opcode_out,params);
								else err(__LINE__)
								break;
							case ST_NUM_OP:
								if(cmd->opcode_num)
									_cmp_emitIc(cst,cmd->opcode_num,params);
								else err(__LINE__)
								break;
							default:
								err(__LINE__)
							}
						}
						else if(state & ST_DOT)
							_cmp_emitS(cst,OP_MV,cst->opbuf + cst->top,len);
						else
						{
							_cmp_emitS(cst,OP_FMV,cst->opbuf + cst->top,len);
							_cmp_stack(cst,1);
						}
						state = ST_ALPHA;
					}
					continue;
				}
			}
			else
			{
				err(__LINE__)
				return 0;
			}
		}
		_cmp_nextc(cst);
	}
}

/*
 read annotations:
 key ( value )
 key.key ( (val) )
 key
 ...
 do
*/
static int _cmp_annotations(struct _cmp_state* cst, st_ptr root)
{
	_cmp_whitespace(cst);

	while(1)
	{
		st_ptr tmp = root;

		while(1)
		{
			uint len = _cmp_name(cst);

			if(len == 0)
				return __LINE__;
			else if(len <= KW_MAX_LEN) 
			{
				switch(_cmp_keyword(cst->opbuf + cst->top))
				{
				case 0:
					break;
				case KW_DO:
					return 0;
				default:
					return __LINE__;
				}
			}

			// insert key
			st_insert(cst->t,&tmp,cst->opbuf + cst->top,len);

			if(cst->c != '.')
				break;

			_cmp_nextc(cst);
		}

		if(whitespace(cst->c)) _cmp_whitespace(cst);

		// value-copy
		if(cst->c == '(')
		{
			uint level = 1;
			cst->c = '=';
			do
			{
				int i;
				char buffer[100];

				for(i = 0; i < sizeof(buffer) && level != 0; i++)
				{
					buffer[i] = cst->c;

					switch(_cmp_nextc(cst))
					{
					case -1:
						return __LINE__;
					case '(':
						level++;
						break;
					case ')':
						level--;
					}
				}

				st_append(cst->t,&tmp,buffer,i);
			}
			while(level != 0);

			_cmp_whitespace(cst);
		}
	}
}

// setup call-site and funspace
static int _cmp_header(struct _cmp_state* cst)
{
	st_ptr params;

	// clean and mark expr
	st_update(cst->t,&cst->root,HEAD_FUNCTION,HEAD_SIZE);
	cst->strs = cst->root;

	// strings
	st_insert(cst->t,&cst->strs,"S",2);

	params = cst->root;
	st_insert(cst->t,&params,"P",2);

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
				_cmp_nextc(cst);
				_cmp_expr(cst,0,PURE_EXPR);
				// update branch-offset
				_cmp_update_imm(cst,coff + 2,cst->code_next - coff - 2 - sizeof(ushort));
				// mark optional
				st_append(cst->t,&params,"?",1);
			}
			else
				cst->code_next = coff;	// undo OP_DEFP

			if(var)	// append-param name
				st_append(cst->t,&params,cst->opbuf + var->name,var->leng);

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
static void _cmp_init(struct _cmp_state* cst, struct _cmp_buffer* bf, task* t, st_ptr* ref, cle_output* response, void* data)
{
	memset(cst,0,sizeof(struct _cmp_state));	// clear struct. MOST vars have default 0 value
	// none-0 defaults:
	cst->src = bf;
	cst->t = t;
	cst->root = *ref;
	cst->s_max = cst->s_top = 1;	// output-context

	cst->response = response;
	cst->data = data;

	cst->glevel = 0;
	// begin code
	_cmp_emit0(cst,OP_BODY);
	cst->code_next += sizeof(ushort)*2 + 3;
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
		ptr = (ushort*)(cst->code + 6);
		*ptr = cst->first_handler;	// first handler

		st_insert(cst->t,&cst->root,"B",2);
		st_insert(cst->t,&cst->root,cst->code,cst->code_next);
	}

	tk_mfree(cst->code);
}

static void cmp_function(struct _cmp_buffer* bf, task* t, st_ptr* ref, cle_output* response, void* data)
{
	struct _cmp_state cst;
	int ret;

	_cmp_init(&cst,bf,t,ref,response,data);

	// create header:
	ret = _cmp_header(&cst);
	if(ret == 0)
	{
		st_ptr anno = cst.root;
		// create annotations
		st_insert(t,&anno,"A",2);
		ret = _cmp_annotations(&cst,anno);
		if(ret == 0)
		{
			_cmp_nextc(&cst);
			// compile body
			if(_cmp_block_expr(&cst,0,PROC_EXPR) != 'e') {cst.err++; print_err(&cst,__LINE__);}
		}
		else cst.err++;
	}
	else cst.err++;

	_cmp_end(&cst);
}

static void cmp_expr(struct _cmp_buffer* bf, task* t, st_ptr* ref, cle_output* response, void* data)
{
	struct _cmp_state cst;

	_cmp_init(&cst,bf,t,ref,response,data);

	// clean and mark expr
	st_update(t,&cst.root,HEAD_EXPR,HEAD_SIZE);
	cst.strs = cst.root;

	// strings
	st_insert(t,&cst.strs,"S",2);

	_cmp_nextc(&cst);
	if(_cmp_block_expr(&cst,0,PURE_EXPR) != -1) {cst.err++; print_err(&cst,__LINE__);}

	_cmp_end(&cst);
}

static int _cmp_do_cmp(st_ptr* src, cle_output* response, void* data, task* t, st_ptr* ref)
{
	struct _cmp_buffer bf;
	// setup buffer
	bf.src = *src;
	bf.current = bf.max = 0;

	// read header and find first primary token...
	while(1)
	{
		switch(_cmp_next_bf(&bf))
		{
		case '#':
			_cmp_comment_bf(&bf);
			continue;
		case ' ':
		case '\t':
		case '\n':
		case '\r':
			continue;
		case '=':	// expr
			cmp_expr(&bf,t,ref,response,data);
			return 0;
		case '(':	// function
			cmp_function(&bf,t,ref,response,data);
			break;
		case ':':	// attr-def
			break;
		case '0':	// number ...
		default:
			return -1;
		}
		break;
	}

	// only whitespace & comments allowed after 'body'
	while(1)
	{
		switch(_cmp_next_bf(&bf))
		{
		case -1:
			return 0;
		case '#':
			_cmp_comment_bf(&bf);
			break;
		case ' ':
		case '\t':
		case '\n':
		case '\r':
			break;
		default:
			return -1;
		}
	}
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

static int _do_setup(sys_handler_data* hd)
{
	struct _field* param = (struct _field*)tk_malloc(sizeof(struct _field));
	hd->data = param;

	param->ptr = hd->instance;
	return 0;
}

static int _do_end(sys_handler_data* hd, cdat code, uint length)
{
	tk_mfree(hd->data);
	return 0;
}

static int _cmp_do_next(sys_handler_data* hd, st_ptr pt, uint depth)
{
	struct _field* param = (struct _field*)hd->data;
	char* data;
	uint length;
	int rcode = 0;

	if(depth != 0 || hd->next_call > 2)
		return -1;

	switch(hd->next_call)
	{
	case 0:		// type
		// the type MUST exist
		if(st_move(&param->ptr,HEAD_TYPE,HEAD_SIZE))
			return -1;

		data = st_get_all(&pt,&length);

		if(st_move(&param->ptr,data,length))
			rcode = -1;

		tk_mfree(data);
		break;
	case 1:		// field
		// if field doesnt exsist - create it
		data = st_get_all(&pt,&length);

		st_insert(hd->t,&param->ptr,data,length);

		tk_mfree(data);
		break;
	case 2:		// value
		// parse and compile fieldvalue
		return _cmp_do_cmp(&pt,hd->response,hd->respdata,hd->t,&param->ptr);
	}

	return rcode;
}

static cle_syshandler handle_sf = {"sf",2,_do_setup,_cmp_do_next,_do_end,0};

void cmp_setup()
{
	cle_add_sys_handler(&handle_sf);
}
