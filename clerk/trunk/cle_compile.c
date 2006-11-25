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

#define PROC_EXPR 0
#define NEST_EXPR 1
#define PURE_EXPR 2

#define TP_ANY 0
#define TP_TREE 1
#define TP_STR 2
#define TP_NUM 4

static struct _cmp_state
{
	task* t;
	FILE* f;
	char* opbuf;
	char* code;
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

	// prg-stack
	uint s_top;
	uint s_max;

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

static const char* keywords[] = {
	"function","begin","end","new","var","if","then","else","asc","desc",0
};

static void print_err(int line)
{
	printf("error on line %d\n",line);
}

#define err(line) {cst->err++;print_err(line);}

static void _cmp_expr(struct _cmp_state* cst, uchar nest);

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
	while(whitespace(cst->c))
		_cmp_nextc(cst);
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
	_cmp_check_code(cst,cst->code_size + 1);
	cst->code[cst->code_next++] = opc;
}

static void _cmp_emitS(struct _cmp_state* cst, uchar opc, char* param, ushort len)
{
	_cmp_check_code(cst,cst->code_size + len + 1 + sizeof(ushort));
	cst->code[cst->code_next++] = opc;
	memcpy(cst->code + cst->code_next,(char*)&len,sizeof(ushort));
	cst->code_next += sizeof(ushort);
	memcpy(cst->code + cst->code_next,param,len);
	cst->code_next += len;
}

static void _cmp_emitIc(struct _cmp_state* cst, uchar opc, uchar imm)
{
	_cmp_check_code(cst,cst->code_size + 2);
	cst->code[cst->code_next++] = opc;
	cst->code[cst->code_next++] = imm;
}

static uint _cmp_emitIs(struct _cmp_state* cst, uchar opc, ushort imm)
{
	_cmp_check_code(cst,cst->code_size + 1 + sizeof(ushort));
	cst->code[cst->code_next++] = opc;
	memcpy(cst->code + cst->code_next,(char*)&imm,sizeof(ushort));
	cst->code_next += sizeof(ushort);
	return (cst->code_next - sizeof(ushort));
}

static uint _cmp_emitIs2(struct _cmp_state* cst, uchar opc, ushort imm, ushort imm2)
{
	_cmp_check_code(cst,cst->code_size + 1 + sizeof(ushort));
	cst->code[cst->code_next++] = opc;
	memcpy(cst->code + cst->code_next,(char*)&imm,sizeof(ushort));
	cst->code_next += sizeof(ushort);
	memcpy(cst->code + cst->code_next,(char*)&imm2,sizeof(ushort));
	cst->code_next += sizeof(ushort);
	return (cst->code_next - sizeof(ushort)*2);
}

static void _cmp_update_imm(struct _cmp_state* cst, uint offset, ushort imm)
{
	ushort* ptr = (ushort*)(cst->code + offset);
	*ptr = imm;
}

static void _cmp_stack(struct _cmp_state* cst, int diff)
{
	cst->s_top += diff;
	if(cst->s_top > cst->s_max)
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
	uint top,begin,len;

	_cmp_whitespace(cst);

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

	begin = cst->top + len;
	if(begin & 3)
		begin += 4 - (cst->top & 3);
	top = begin + sizeof(struct _cmp_var);

	_cmp_check_buffer(cst,top);

	var = PEEK_VAR(begin);

	var->prev = cst->top_var;
	var->id   = cst->s_top;
	var->name = cst->top;
	var->leng = len;
	var->level = cst->glevel;

	_cmp_emit0(cst,OP_DEF);
	_cmp_stack(cst,1);

	cst->top_var = begin;
	cst->top = top;
	return var;
}

static void _cmp_free_var(struct _cmp_state* cst)
{
	struct _cmp_var* var = 0;
	uint nxtvar = cst->top_var;

	while(nxtvar)
	{
		var = PEEK_VAR(nxtvar);
		if(var->level == cst->glevel) break;
		_cmp_stack(cst,-1);
		nxtvar = var->prev;
	}

	if(nxtvar)
	{
		cst->top_var = nxtvar;
		cst->top = nxtvar + sizeof(struct _cmp_var);
	}
	else
		cst->top_var = 0;
}

static ushort _cmp_var(struct _cmp_state* cst)
{
	int len;
	_cmp_nextc(cst);
	len = _cmp_name(cst);
	if(len > 0)
	{
		struct _cmp_var* var = _cmp_find_var(cst,len);
		if(var)
			return var->id;
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

static void _cmp_call(struct _cmp_state* cst, uint len, uchar nest)
{
	uint pcount = 0;

	_cmp_emitS(cst,OP_CALL,cst->opbuf + cst->top,len);

	_cmp_nextc(cst);
	_cmp_whitespace(cst);
	do {
		if(cst->c == ')') break;
		_cmp_emitIc(cst,OP_SETP,pcount++);
		_cmp_expr(cst,1);	// construct parameters
	} while(cst->c == ',');
	if(cst->c != ')') err(__LINE__)

	if(nest & NEST_EXPR)
		_cmp_emit0(cst,OP_DOCALL_N);
	else
		_cmp_emit0(cst,OP_DOCALL);
	_cmp_nextc(cst);
}

#define chk_state(legal) if(((legal) & state) == 0) err(__LINE__)

static void _cmp_new(struct _cmp_state* cst, uint state)
{
	uint level = 0;

	_cmp_nextc(cst);
	while(1)
	{
		switch(cst->c)
		{
		case '=':
			chk_state(ST_ALPHA|ST_VAR)
			_cmp_emit0(cst,OP_ASGN);
			_cmp_expr(cst,PURE_EXPR);
			_cmp_emit0(cst,OP_ASGNRET);
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
			chk_state(ST_ALPHA|ST_VAR)
			_cmp_emit0(cst,OP_POP);
			if(level > 0) level--; else err(__LINE__)
			if(level == 0) return;
			state = ST_0;
			break;
		case '[':
			chk_state(ST_0|ST_ALPHA|ST_VAR)
			_cmp_expr(cst,NEST_EXPR);
			if(cst->c != ']') err(__LINE__)
			_cmp_emit0(cst,OP_WIDX);
			state = ST_ALPHA;
			break;
		case '$':
			chk_state(ST_0|ST_DOT)
			_cmp_emitIs(cst,OP_WVAR,_cmp_var(cst));
			break;
		case ';':
			chk_state(ST_ALPHA|ST_VAR)
			_cmp_emit0(cst,OP_POP);
			if(level == 0) return;
			break;
		case '.':
			chk_state(ST_ALPHA|ST_VAR)
			state = ST_DOT;
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
				if(_cmp_keyword(cst->opbuf + cst->top) != -1) err(__LINE__)
				switch(state)
				{
				case ST_0:
					_cmp_emitS(cst,OP_DMVW,cst->opbuf + cst->top,len);
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
				err(__LINE__)
		}
		_cmp_nextc(cst);
	}
}

#define chk_out() if(state & (ST_ALPHA|ST_STR|ST_VAR)) \
	{if(nest & NEST_EXPR)_cmp_emit0(cst,OP_APP); else _cmp_emit0(cst,OP_OUTS); \
	_cmp_stack(cst,-1);}

static void _cmp_expr(struct _cmp_state* cst, uchar nest)
{
	uint state = ST_0;
	uint level = 0;
	uint type  = TP_ANY;

	//_cmp_emit0(cst,OP_EXPR);
	_cmp_nextc(cst);
	while(1)
	{
		switch(cst->c)
		{
		case '=':
			if(nest) err(__LINE__)
			chk_state(ST_ALPHA|ST_VAR)
			_cmp_emit0(cst,OP_ASGN);
			_cmp_expr(cst,PURE_EXPR);
			_cmp_emit0(cst,OP_ASGNRET);
			_cmp_stack(cst,-1);
			state = ST_0;
			break;
		case '(':
			chk_state(ST_0|ST_ALPHA|ST_STR|ST_VAR)
			chk_out()
			_cmp_expr(cst,NEST_EXPR);
			if(cst->c != ')') err(__LINE__)
			state = ST_ALPHA;
			break;
		case '[':
			chk_state(ST_0|ST_ALPHA|ST_VAR|ST_CONF)
			chk_out()
			if(state == ST_CONF)
			{
				_cmp_emit0(cst,OP_CONF);
				_cmp_stack(cst,1);
			}
			_cmp_expr(cst,NEST_EXPR);
			if(cst->c != ']') err(__LINE__)
			_cmp_emit0(cst,OP_RIDX);
			state = ST_ALPHA;
			break;
		case ']':
			chk_state(ST_ALPHA|ST_STR|ST_VAR)
		case ')':
			if(nest) return;
			err(__LINE__)
			break;
		case '$':	// var (all states ok)
			chk_out()
			if(state == ST_DOT)
				_cmp_emitIs(cst,OP_RVAR,_cmp_var(cst));
			else
			{
				if(state == ST_CONF)
					_cmp_emitIs(cst,OP_CVAR,_cmp_var(cst));
				else
					_cmp_emitIs(cst,OP_LVAR,_cmp_var(cst));
				_cmp_stack(cst,1);
			}
			state = ST_VAR;
			break;
		case '\'':
		case '"':
			chk_state(ST_0|ST_ALPHA|ST_STR|ST_VAR)
			if(state != ST_STR)
				chk_out()
			_cmp_str(cst,state == ST_STR);
			state = ST_STR;
			break;
		case '{':
			chk_state(ST_0)
			level++;
			cst->glevel++;
			state = ST_0;
			break;
		case '}':
			chk_state(ST_0)
			if(level > 0)
			{
				level--;
				cst->glevel--;
				_cmp_free_var(cst);
			}
			else err(__LINE__)
			if(level == 0) return;
			state = ST_0;
			break;
		case ';':
		case ',':
			chk_out()
			if(level == 0) return;
			state = ST_0;
			break;
		case '.':
			chk_state(ST_ALPHA|ST_VAR)
			state = ST_DOT;
			break;
		case '\\':
			chk_state(ST_0|ST_ALPHA|ST_STR|ST_VAR)
			chk_out()
			state = ST_CONF;
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
				switch(_cmp_keyword(cst->opbuf + cst->top))
				{
				case -1:	// alpha (all states ok)
					chk_out()
					if(cst->c == '(')	// fun-call?
						_cmp_call(cst,len,nest);
					else
					{
						uchar op;
						switch(state)
						{
						case ST_DOT:
							op = OP_MV;
							break;
						case ST_CONF:
							op = OP_CMV;
							_cmp_stack(cst,1);
							break;
						default:
							op = OP_FMV;
							_cmp_stack(cst,1);
						}
						_cmp_emitS(cst,op,cst->opbuf + cst->top,len);
					}

					state = ST_ALPHA;
					break;
				case 2:		// end
					if(cst->glevel != 0) err(__LINE__)
					if(nest) err(__LINE__)
					chk_state(ST_0)
					_cmp_emit0(cst,OP_END);
					return;
				case 3:		// new
					chk_state(ST_0)
					_cmp_new(cst,ST_0);
					state = ST_0;
					break;
				case 4:		// var
					chk_state(ST_0)
					if(nest) err(__LINE__)
					_cmp_def_var(cst);
					_cmp_new(cst,ST_VAR);
					state = ST_0;
					break;
				default:
					err(__LINE__)
				}
				continue;
			}
			else
				err(__LINE__)
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

	// get parameters
	_cmp_whitespace(cst);
	if(cst->c != '(') return(__LINE__);

	params = cst->root;
	st_insert(cst->t,&params,"P",2);

	_cmp_nextc(cst);
	while(1)
	{
		_cmp_whitespace(cst);

		if(cst->c == '$')
		{
			struct _cmp_var* var = _cmp_def_var(cst);
			cst->params++;

			// create as var and pre-load it in fun
			_cmp_whitespace(cst);
			if(cst->c == '=')
			{
				uint coff;
				// mark optional
				st_append(cst->t,&params,"?",1);
				// append-param name
				if(var)
					st_append(cst->t,&params,cst->opbuf + var->name,var->leng);

				// default value
				coff = _cmp_emitIs(cst,OP_DEFP,0);
				_cmp_expr(cst,PURE_EXPR);
				if(coff == cst->code_next) err(__LINE__)
				else	// update branch-offset
					_cmp_update_imm(cst,coff,cst->code_next - coff + sizeof(ushort));
			}
			else if(var)
			{
				// append-param name
				st_append(cst->t,&params,cst->opbuf + var->name,var->leng);
				_cmp_emit0(cst,OP_CHKP);
			}
		}
		
		if(cst->c == ',')
			_cmp_nextc(cst);
		else
			break;
	}

	return (cst->c == ')'? 0 : __LINE__);
}

// setup _cmp_state
static void _cmp_init(struct _cmp_state* cst, FILE* f, task* t, st_ptr* ref)
{
	cst->t = t;
	cst->f = f;
	cst->root = *ref;
	cst->s_top = cst->s_max = 0;
	cst->top_var = 0;
	cst->code_size = 0;
	cst->code_next = 0;
	cst->opbuf = 0;
	cst->code  = 0;
	cst->bsize = cst->top = 0;
	cst->err = 0;
	cst->glevel = 0;
	cst->params = 0;

	_cmp_emitIs2(cst,OP_BODY,0,0);
}

static void _cmp_end(struct _cmp_state* cst)
{
	tk_mfree(cst->opbuf);

	// make body
	if(cst->err == 0)
	{
		ushort* ptr = (ushort*)(cst->code + 1);
		*ptr = cst->code_next;
		ptr++;
		*ptr = cst->params;
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
			// compile body
			_cmp_expr(&cst,PROC_EXPR);
		else cst.err++;
	}
	else cst.err++;

	_cmp_end(&cst);
	return ret;
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

	_cmp_expr(&cst,PURE_EXPR);

	_cmp_end(&cst);
	return 0;
}
