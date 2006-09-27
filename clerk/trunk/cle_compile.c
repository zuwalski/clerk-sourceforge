/* Copyrigth(c) Lars Szuwalski, 2006 */

#include "cle_runtime.h"
#include "cle_struct.h"

#define BUFFER_GROW 128

static enum parser_states
{
	ST_INIT = 0,
	DOT_0,
	ALPHA_0,
	VAR_0,
	CURL_0,
	APP_0,
	STR_0,
	ST_NEW,
	ST_EQ,
	DOT_EQ,
	ALPHA_EQ,
	VAR_EQ,
	APP_EQ,
	STR_EQ,
	DOT_NEW,
	ALPHA_NEW,
	VAR_NEW,
	CURL_NEW,
	ST_VAR_DEF
};

static const char* keywords[] = {
	"private","public","begin","end","new","var","if","then","else","asc","desc",0
};

static struct _ptr_stack
{
	struct _ptr_stack* prev;
	st_ptr pt;
	uint code;
};

static struct _var_stack
{
	struct _var_stack* prev;
	uint length;
	uint level;
	uint name;
	ushort id;
};

static struct _fun_stack
{
	struct _fun_stack* prev;
	struct _var_stack* vs;
	st_ptr code;
	uint id;
	ushort stringidx;
	uchar publicfun;
};

static enum opc
{
	OPARN,	// (
	OSQR,	// [
	OCALL,	// ( - begin call
	ONEW,	// new
	OEQL,	// =
	ONUMOP	// + - * / %
};

static struct _op_stack
{
	struct _op_stack* prev;
	uint state;
	uint type;
	uint level;
	uint bufidx;
	uint length;
	enum opc opc;
	uchar pres;
	uchar op;
};

static struct _rt_stack
{
	ushort* codepos;
	ushort stop;
	ushort smax;
};

typedef struct _parser_state
{
	struct _ptr_stack* ps;
	struct _ptr_stack* psfree;
	struct _fun_stack* fs;
	struct _fun_stack* fsfree;
	struct _var_stack* vsfree;
	struct _op_stack*  osfree;
	char* opbuf;
	FILE* f;
	task* t;
	st_ptr app;

	uint line;
	uint errors;

	uint bsize;
	uint top;

	uint funidx;
}
_parser_state;

static void err(int line)
{
	printf("error on line %d\n",line);
}

static void _cle_check_buffer(_parser_state* parser, uint top)
{
	if(top >= parser->bsize)
	{
		uint diff = top - parser->bsize;

		parser->bsize += BUFFER_GROW > diff? BUFFER_GROW : diff;
		parser->opbuf = (char*)tk_realloc(parser->opbuf,parser->bsize);
	}
}

static void _cle_oppush(_parser_state* ps, struct _op_stack** stack, uint state, uint type, uint level, uint length, enum opc opc, uchar pres, uchar op)
{
	struct _op_stack* elm = ps->osfree;

	if(elm == 0)
		elm = (struct _op_stack*)tk_alloc(ps->t,sizeof(struct _op_stack));
	else
		ps->osfree = elm->prev;

	elm->state = state;
	elm->type = type;
	elm->level = level;
	elm->length = length;
	elm->bufidx = ps->top;
	elm->opc = opc;
	elm->pres = pres;
	elm->op = op;

	elm->prev = *stack;
	*stack = elm;
}

static void _cle_oppop(_parser_state* ps, struct _op_stack** stack)
{
	if(*stack)
	{
		struct _op_stack* tmp = (*stack)->prev;

		ps->top = (*stack)->bufidx;

		(*stack)->prev = ps->osfree;
		ps->osfree = *stack;
		*stack = tmp;
	}
}

static void _cle_vpush(_parser_state* ps, uint length, uint level, uint name, ushort id)
{
	struct _var_stack* elm = ps->vsfree;
	
	if(elm == 0)
		elm = (struct _var_stack*)tk_alloc(ps->t,sizeof(struct _var_stack));
	else
		ps->vsfree = elm->prev;

	elm->length = length;
	elm->level  = level;
	elm->name   = name;

	elm->id = id;

	elm->prev = ps->fs->vs;
	ps->fs->vs = elm;
}

static void _cle_vpop(_parser_state* ps)
{
	struct _var_stack* elm = ps->fs->vs;

	if(elm == 0)
		return;

	ps->top = elm->name;

	ps->fs->vs = elm->prev;
	elm->prev = ps->vsfree;
	ps->vsfree = elm;
}

static void _cle_fpush(_parser_state* ps, st_ptr* code, uchar publicfun)
{
	struct _fun_stack* elm = ps->fsfree;
	
	if(elm == 0)
		elm = (struct _fun_stack*)tk_alloc(ps->t,sizeof(struct _fun_stack));
	else
		ps->fsfree = elm->prev;

	elm->code = *code;
	elm->prev = ps->fs;
	elm->vs = 0;
	elm->stringidx = 0;
	elm->publicfun = publicfun;

	elm->id = (ps->fs)? ps->fs->id + 1 : 0;

	ps->fs = elm;
}

static void _cle_fpop(_parser_state* ps)
{
	struct _fun_stack* elm = ps->fs;

	if(elm == 0)
		return;

	ps->fs = elm->prev;
	elm->prev = ps->fsfree;
	ps->fsfree = elm;
}

static void _cle_ppush(_parser_state* ps, st_ptr* ptr, uint code)
{
	struct _ptr_stack* elm = ps->psfree;
	
	if(elm == 0)
		elm = (struct _ptr_stack*)tk_alloc(ps->t,sizeof(struct _ptr_stack));
	else
		ps->psfree = elm->prev;

	elm->pt = *ptr;
	elm->code = code;
	elm->prev = ps->ps;

	ps->ps = elm;
}

static int _cle_ppop(_parser_state* ps)
{
	struct _ptr_stack* elm = ps->ps;

	if(elm == 0)
		return 0;

	ps->ps = elm->prev;
	elm->prev = ps->psfree;
	ps->psfree = elm;

	return (elm->code);
}

static uint _cle_pcheck_pop(_parser_state* ps, uint code)
{
	struct _ptr_stack* elm = ps->ps;

	if(elm == 0 || elm->code != code)
		return 0;

	ps->ps = elm->prev;
	elm->prev = ps->psfree;
	ps->psfree = elm;
	return 1;
}

static void _cle_emit0(_parser_state* parser, st_ptr* code, uchar opc)
{
	if(opc)
	{
		st_append(parser->t,code,&opc,1);
		//printf("%s\n",rt_opc_name(opc));
	}
}

static void _cle_emitI(_parser_state* parser, st_ptr* code, uchar opc, uint im)
{
	if(opc)
	{
		st_append(parser->t,code,&opc,1);
		st_append(parser->t,code,(cdat)&im,sizeof(uint));
		//printf("%s %d\n",rt_opc_name(opc),im);
	}
}

static void _cle_emitIs(_parser_state* parser, st_ptr* code, uchar opc, ushort im)
{
	if(opc)
	{
		st_append(parser->t,code,&opc,1);
		st_append(parser->t,code,(cdat)&im,sizeof(ushort));
		//printf("%s %d\n",rt_opc_name(opc),im);
	}
}

static void _cle_emitS(_parser_state* parser, st_ptr* code, char* param, ushort len, uchar opc)
{
	if(opc)
	{
		st_append(parser->t,code,&opc,1);
		st_append(parser->t,code,(cdat)&len,sizeof(ushort));
		st_append(parser->t,code,param,len);
		//printf("%s %s\n",rt_opc_name(opc),param);
	}
}

static void _cle_whitespace(_parser_state* parser, int* c)
{
	while(whitespace(*c))
		*c = getc(parser->f);
}

static uint _cle_name(_parser_state* parser, int* c)
{
	uint op = parser->top;
	while(alphanum(*c))
	{
		_cle_check_buffer(parser,op);
		parser->opbuf[op++] = *c;
		*c = getc(parser->f);
	}

	if(op == parser->top) err(__LINE__);

	_cle_check_buffer(parser,op);
	parser->opbuf[op++] = '\0';
	return op;
}

static int _cle_function_body(_parser_state* parser, st_ptr* expr);

/* parse: function Name($param $param...) -> [annotations] begin ... end */
static void _cle_function_header(_parser_state* parser, st_ptr* code, st_ptr* ref, uchar publicfun)
{
	st_ptr tmpptr,param,funhome;
	uint op,funidx;
	int c = getc(parser->f);

	// get name
	if(!alpha(c)) return;

	op = _cle_name(parser,&c);

	_cle_whitespace(parser,&c);

	// parameters start
	if(c != '(') err(__LINE__);

	if(parser->fs)	// inner function
	{
		*code = parser->fs->code;
		st_insert(parser->t,code,"subs",5);
	}
	else			// top-level function
		*code = *ref;

	// insert/move name
	st_insert(parser->t,code,parser->opbuf + parser->top, op - parser->top);

	if(st_insert(parser->t,code,HEAD_FUNCTION,HEAD_SIZE))	// new
	{
		if(!parser->fs)
			*ref = *code;

		*code = parser->app;
		st_insert(parser->t,code,funspace,FUNSPACE_SIZE);

		funidx = parser->funidx++;
		st_insert(parser->t,code,(cdat)&funidx,sizeof(uint));
	}
	else	// old
	{
		if(st_get(ref,(char*)&funidx,sizeof(uint)) > 0)
			err(__LINE__);

		*code = parser->app;
		if(!st_move(code,funspace,FUNSPACE_SIZE)) err(__LINE__);
		if(!st_move(code,(cdat)&funidx,sizeof(uint))) err(__LINE__);
	}

	// write/move ref
	if(parser->fs)
	{
		// gen code to produce fun-ref
		_cle_emitS(parser,ref,parser->opbuf + parser->top, op - parser->top,OP_MOVE_WRITER);
		_cle_emitI(parser,ref,OP_FUNCTION_REF,funidx);
	}
	else
	{
		st_append(parser->t,ref,HEAD_FUNCTION,HEAD_SIZE);
		st_insert(parser->t,ref,(cdat)&funidx,sizeof(uint));	// put fun-ref
	}

	// mark begining of this function-space
	funhome = *code;

	// begin annotations-section
	st_insert(parser->t,code,"A",2);

	param = *code;

	// begin and clear parameter section
	st_insert(parser->t,&param,"P",2);
	st_delete(parser->t,&param,0,0);

	// parse parameters
	c = getc(parser->f);
	while(1)
	{
		_cle_whitespace(parser,&c);

		if(c == ')')
			break;

		if(c != '$') err(__LINE__);

		c = getc(parser->f);
		op = _cle_name(parser,&c);

		tmpptr = param;
		st_insert(parser->t,&tmpptr,parser->opbuf + parser->top, op - parser->top);

		_cle_whitespace(parser,&c);
		if(c == '=')
		{
			if(parser->fs == 0)
				err(__LINE__);
			
			_cle_emitS(parser,ref,parser->opbuf + parser->top, op - parser->top,OP_SET_PARAM);
			c = _cle_function_body(parser,ref);
			if(c != ',' && c != ')')
				err(__LINE__);
		}

		if(c == ',')
			c = getc(parser->f);
	}

	if(parser->fs)
		_cle_emit0(parser,ref,OP_FUNCTION_REF_DONE);

	// remember this state -> and make it the top-function
	_cle_fpush(parser,&funhome,publicfun);
}

static void _cle_put_stack(struct _rt_stack* rt_stack, ushort count)
{
	rt_stack->stop += count;
	if(rt_stack->stop > rt_stack->smax)
		rt_stack->smax = rt_stack->stop;
}

static int _cle_var(_parser_state* parser, st_ptr* code, struct _rt_stack* rt_stack, uint level, uchar newvar, uchar pathval_rw)
{
	struct _var_stack* vs;
	uint len, op;
	int c;
	uchar opc;

	// get var name
	c = getc(parser->f);
	op = _cle_name(parser,&c);

	len = op - parser->top;
	vs = parser->fs->vs;
	while(vs)
	{
		if(vs->length == len && memcmp(parser->opbuf + vs->name,parser->opbuf + parser->top,len) == 0)
			break;

		vs = vs->prev;
	}

	_cle_whitespace(parser,&c);

	if(newvar)
	{
		if(vs) err(__LINE__);	// re-def

		_cle_vpush(parser,len,level,parser->top,rt_stack->stop);
		if(c == '=')
		{
			_cle_emit0(parser,code,OP_DEF_VAR_REF);
			_cle_put_stack(rt_stack,3);
		}
		else
		{
			_cle_emit0(parser,code,OP_DEF_VAR);
			_cle_put_stack(rt_stack,2);
		}

		parser->top = op;
	}
	else if(vs)
	{
		switch(pathval_rw)
		{
		case 3:
			if(c == '=')
			{
				_cle_emitIs(parser,code,OP_VAR_REF,parser->fs->vs->id);
				_cle_put_stack(rt_stack,2);
				break;
			}
		case 0:
			_cle_emitIs(parser,code,OP_LOAD_VAR,parser->fs->vs->id);
			_cle_put_stack(rt_stack,1);
			break;
		case 1:
			_cle_emitIs(parser,code,OP_VAR_READ,parser->fs->vs->id);
			break;
		case 2:
			_cle_emitIs(parser,code,OP_VAR_WRITE,parser->fs->vs->id);
			_cle_put_stack(rt_stack,1);
			break;
		}
	}
	else
		err(__LINE__);	// var not found

	return c;
}

static int _cle_keyword(const char* buffer)
{
	int i;
	for(i = 0; keywords[i]; i++)
	{
		if(strcmp(buffer,keywords[i]) == 0)
			return i;
	}

	return -1;
}

static int _cle_function_body(_parser_state* parser, st_ptr* expr)
{
	st_ptr code = expr? *expr : parser->fs->code;
	st_ptr strings,cur_string;
	struct _rt_stack rt_stack;
	struct _op_stack* stack = 0;

	uint type  = 0;	// 0 - any, 1 - tree, 2 - str,
	uint level = 0;
	enum parser_states state;

	int c = getc(parser->f);
	ushort stringidx;
	uchar tmp,isRef = 0;

	rt_stack.smax = rt_stack.stop = 0;

	if(expr && parser->fs)		// called from function header (parameter set)
	{
		strings = parser->fs->code;
		st_move(&strings,"S",2);
		stringidx = parser->fs->stringidx;
		state = ST_EQ;
	}
	else
	{
		cur_string = strings = code;
		st_insert(parser->t,&strings,"S",2);	// begin string-space
		st_delete(parser->t,&strings,0,0);		// remove any old strings

		st_insert(parser->t,&code,"B",2);	// body-part
		st_delete(parser->t,&code,0,0);		// remove any old code
		stringidx = 0;
		state = ST_INIT;

		if(expr || parser->fs->publicfun)
			_cle_emit0(parser,&code,OP_PUBLIC_FUN);

		// first load all parameters into vars
		if(!st_move(&cur_string,"A\0P",4))
		{
			it_ptr it;
			it_create(&it,&cur_string);
			while(it_next(0,&it))
			{
				_cle_emitS(parser,&code,it.kdata,it.kused,OP_LOAD_PARAM);

				_cle_check_buffer(parser,parser->top + it.kused);
				memcpy(parser->opbuf + parser->top,it.kdata,it.kused);

				_cle_vpush(parser,it.kused,0,parser->top,rt_stack.stop);
				parser->top += it.kused;
				rt_stack.stop++;
			}

			it_dispose(&it);
			rt_stack.smax = rt_stack.stop;
		}
	}

	while(1)
	{
		switch(c)
		{
		case '(':
			_cle_oppush(parser,&stack,state,type,level,0,OPARN,0,0);
			break;
		case ')':
			while(stack && stack->opc != OPARN && stack->opc != OCALL)
			{
				switch(stack->opc)
				{
				case OEQL:
					_cle_emit0(parser,&code,OP_POP);
					rt_stack.stop--;
				case ONEW:
					break;
				default:
					err(__LINE__);
				}
				_cle_oppop(parser,&stack);
			}

			if(stack == 0)
			{
				if(expr && parser->fs)	// set param done
				{
					_cle_emit0(parser,&code,OP_READER_OUT);
					rt_stack.stop--;
					parser->fs->stringidx = stringidx;
					return c;
				}

				err(__LINE__);
				state = ST_INIT;
			}
			else if(stack->opc == OCALL)
				_cle_emitS(parser,&code,parser->opbuf + stack->bufidx,stack->length,OP_CALL);

			if(stack)
			{
				state = stack->state;
				type = stack->type;
				_cle_oppop(parser,&stack);
			}
			break;
		case '.':
			switch(state)
			{
			case ALPHA_0:
			case VAR_0:
				state = DOT_0;
				break;
			case ALPHA_NEW:
			case VAR_NEW:
				state = DOT_NEW;
				break;
			case ALPHA_EQ:
			case VAR_EQ:
				state = DOT_EQ;
				break;
			default:
				err(__LINE__);
				state = DOT_0;	// recover
			}
			break;
		case '\\':
			switch(state)
			{
			case ALPHA_0:
			case VAR_0:
				_cle_emit0(parser,&code,isRef? OP_VAR_OUT : OP_READER_OUT);
				rt_stack.stop--;
				if(type != 0 && type != 2) err(__LINE__);
				type = 2;
			case ST_INIT:
				state = APP_0;
				break;
			case ALPHA_EQ:
			case VAR_EQ:
				_cle_emit0(parser,&code,isRef? OP_VAR_OUT : OP_READER_OUT);
				rt_stack.stop--;
				if(type != 0 && type != 2) err(__LINE__);
				type = 2;
			case ST_EQ:
				state = APP_EQ;
				break;
			default:
				err(__LINE__);
				state = ST_INIT;
			}
			_cle_emit0(parser,&code,OP_APP_ROOT);
			_cle_put_stack(&rt_stack,1);
			break;
		case '{':
			level++;
			switch(state)
			{
			case ALPHA_0:
			case VAR_0:
				state = CURL_0;
				break;
			case ST_NEW:
			case ALPHA_NEW:
			case VAR_NEW:
				state = CURL_NEW;
				break;
			default:
				err(__LINE__);
				state = ST_INIT;
			}
			break;
		case '}':
			tmp = 0;
			switch(state)
			{
			case CURL_0:
			case CURL_NEW:
				tmp = OP_POP;
				break;
			default:
				err(__LINE__);
				state = ST_INIT;
			}

			if(level == 0) err(__LINE__);
			else
			{
				_cle_emit0(parser,&code,tmp);

				if(--level == 0)
					state = ST_INIT;

				if(expr == 0 && parser->fs->vs)
				{
					while(parser->fs->vs->level > level)
					{
						_cle_emitIs(parser,&code,OP_POP,parser->fs->vs->id);
						rt_stack.stop--;
						_cle_vpop(parser);
					}
				}
			}
			break;
		case '=':
			if(type > 1) err(__LINE__);
			tmp = 0;
			switch(state)
			{
			case ALPHA_0:
				tmp = OP_READER_TO_WRITER;
				_cle_put_stack(&rt_stack,1);
				state = level? CURL_0 : ST_INIT;
				break;
			case VAR_0:
				if(isRef == 0)
				{
					tmp = OP_READER_TO_WRITER;
					_cle_put_stack(&rt_stack,1);
				}
			case ST_VAR_DEF:
				state = level? CURL_0 : ST_INIT;
				break;
			case ALPHA_NEW:
			case VAR_NEW:
				tmp = OP_READER_TO_WRITER;
				_cle_put_stack(&rt_stack,1);
				state = CURL_NEW;
				break;
			default:
				err(__LINE__);
			}
			_cle_emit0(parser,&code,tmp);
			_cle_oppush(parser,&stack,state,type,level,0,OEQL,0,0);
			state = ST_EQ;
			type = 0;
			break;
		case ',':
		case ';':
			tmp = 0;
			switch(state)
			{
			case VAR_0:
			case ALPHA_0:
			case STR_0:
				tmp = OP_READER_CLEAR;
				rt_stack.stop -= 2;
				state = level? CURL_0 : ST_INIT;
				break;
			case ALPHA_NEW:
			case VAR_NEW:
				tmp = OP_POP;
				rt_stack.stop--;
				state = CURL_NEW;
				break;
			case ALPHA_EQ:
			case VAR_EQ:
			case STR_EQ:
				tmp = isRef? OP_VAR_CLEAR : OP_READER_CLEAR;
				rt_stack.stop -= 2;
				break;
			case ST_VAR_DEF:
				if(c == ',' && expr && parser->fs) err(__LINE__);
				state = ST_INIT;
				break;
			default:
				err(__LINE__);
				state = ST_INIT;
			}
			_cle_emit0(parser,&code,tmp);

			while(stack && stack->level >= level && stack->opc != OCALL)
			{
				switch(stack->opc)
				{
				case OEQL:
					_cle_emit0(parser,&code,OP_POP);
					rt_stack.stop--;
				case ONEW:
					state = stack->state;
					type = stack->type;
					break;
				default:
					err(__LINE__);
					state = ST_INIT;
				}
				_cle_oppop(parser,&stack);
			}

			if(c == ',' && expr && parser->fs)	// set param done
			{
				parser->fs->stringidx = stringidx;
				if(level) err(__LINE__);
				return c;
			}
			isRef = 0;
			break;
		case '\'':
		case '"':
			{
				int app = 0;
				tmp = 0;
				if(type != 0 && type != 2) err(__LINE__);
				switch(state)
				{
				case ST_INIT:
					state = STR_0;
					break;
				case ST_EQ:
					state = STR_EQ;
					break;
				case ALPHA_0:
				case VAR_0:
					tmp = isRef? OP_VAR_OUT : OP_READER_OUT;
					rt_stack.stop--;
					state = STR_0;
					break;
				case ALPHA_EQ:
				case VAR_EQ:
					tmp = isRef? OP_VAR_OUT : OP_READER_OUT;
					rt_stack.stop--;
					state = STR_EQ;
					break;
				case STR_0:
				case STR_EQ:
					app = 1;
					break;
				default:
					err(__LINE__);
					state = ST_INIT;
				}
				_cle_emit0(parser,&code,tmp);

				if(app == 0)
				{
					_cle_emitIs(parser,&code,OP_STR,stringidx);
					_cle_put_stack(&rt_stack,1);

					cur_string = strings;
					st_insert(parser->t,&cur_string,(cdat)&stringidx,sizeof(ushort));
					stringidx++;
				}
				
				app = cle_string(parser->f,parser->t,&cur_string,c,&c,app);
				if(app) err(app);
				type = 2;
			}
			continue;
		case '$':
			{
				uchar pathval_rw = 0;	// 0 - load, 1 - read with, 2 - write with, 3 - ref
				switch(state)
				{
				case ST_INIT:
				case CURL_0:
					pathval_rw = 3;
					state = VAR_0;
					break;
				case ALPHA_0:
				case VAR_0:
					if(type != 0 && type != 2) err(__LINE__);
					type = 2;
				case STR_0:
					_cle_emit0(parser,&code,isRef? OP_VAR_OUT : OP_READER_OUT);
					rt_stack.stop--;
					state = VAR_0;
					break;
				case DOT_0:
				case APP_0:
					state = VAR_0;
					pathval_rw = 1;
					break;
				case ALPHA_EQ:
				case VAR_EQ:
					if(type != 0 && type != 2) err(__LINE__);
					type = 2;
				case STR_EQ:
					_cle_emit0(parser,&code,isRef? OP_VAR_OUT : OP_READER_OUT);
					rt_stack.stop--;
				case ST_EQ:
					state = VAR_EQ;
					break;
				case DOT_EQ:
				case APP_EQ:
					state = VAR_EQ;
					pathval_rw = 1;
					break;
				case ST_NEW:
				case DOT_NEW:
				case CURL_NEW:
					state = VAR_NEW;
					pathval_rw = 2;
					break;
				default:
					err(__LINE__);
					state = ST_INIT;
				}

				if(parser->fs)
				{
					c = _cle_var(parser,&code,&rt_stack,0,0,pathval_rw);
					if(pathval_rw == 3 && c == '=')
						isRef = 1;
				}
				else err(__LINE__);
			}
			continue;
		case ' ':
		case '\t':
		case '\n':
		case '\r':
			break;
		default:
  			if(alpha(c))
  			{
				int keyword;
				uint op = _cle_name(parser,&c);

				keyword = _cle_keyword(parser->opbuf + parser->top);
				switch(keyword)
				{
				case 0:		// private function (in function)
				case 1:		// public function (in function)
					if(!whitespace(c)) err(__LINE__);
					else if(expr) err(__LINE__);

					switch(state)
					{
					case ST_INIT:
					case ST_NEW:
					case CURL_0:
					case CURL_NEW:
						break;
					default:
						err(__LINE__);
						state = ST_INIT;
					}
					{
						st_ptr funcode;
						parser->fs->stringidx = stringidx;
						_cle_function_header(parser,&funcode,&code,keyword != 0);
						stringidx = parser->fs->stringidx;

//						cle_write(parser->f,parser->t,&funcode,0,1);
 						c = getc(parser->f);
					}
					break;
				case 2:		// begin
					err(__LINE__);
					state = ST_INIT;
					break;
				case 3:		// end
					if(state != ST_INIT) err(__LINE__);

					while(stack)
					{
						switch(stack->opc)
						{
						case OEQL:
							_cle_emit0(parser,&code,OP_POP);
							rt_stack.stop--;
						case ONEW:
							break;
						default:
							err(__LINE__);
						}
						_cle_oppop(parser,&stack);
					}

					if(parser->fs)
					{
						while(parser->fs->vs)
							_cle_vpop(parser);

						_cle_fpop(parser);
					}
					_cle_emitIs(parser,&code,OP_FUNCTION_END,rt_stack.smax + 1);
					return c;
				case 4:		// new
					_cle_oppush(parser,&stack,state,type,level,0,ONEW,0,0);
					switch(state)
					{
					case ST_INIT:
						_cle_emit0(parser,&code,OP_WRITER_TO_READER);
					case ST_EQ:
						break;
					default:
						err(__LINE__);
					}
					state = ST_NEW;
					type = 1;
					break;
				case 5:		// var $name
					if(state != ST_INIT && state != CURL_0) err(__LINE__);
					if(expr) err(__LINE__);

					_cle_whitespace(parser,&c);

					if(c != '$') err(__LINE__);
					c = _cle_var(parser,&code,&rt_stack,level,1,0);
					switch(c)
					{
					case '=':
						isRef = 1;
						state = ST_VAR_DEF;
						break;
					case ';':
						_cle_emit0(parser,&code,OP_VAR_POP);
						state = ST_VAR_DEF;
						break;
					default:
						state = ST_NEW;
					}
					continue;
				case 6:		// if
					break;
				case 7:		// then
					break;
				case 8:		// else
					break;
				case 9:		// asc
					break;
				case 10:	// desc
					break;
				default:
					tmp = 0;
					switch(state)
					{
					case ALPHA_0:
					case VAR_0:
						if(type != 0 && type != 2) err(__LINE__);
						type = 2;
					case STR_0:
						_cle_emit0(parser,&code,isRef? OP_VAR_OUT : OP_READER_OUT);
						rt_stack.stop--;
					case ST_INIT:
						tmp = OP_MOVE_READER_FUN;
						_cle_put_stack(&rt_stack,1);
						state = ALPHA_0;
						break;
					case CURL_0:
						tmp = OP_DUB_MOVE_READER;
						_cle_put_stack(&rt_stack,1);
						state = ALPHA_0;
						break;
					case DOT_0:
					case APP_0:
						tmp = OP_MOVE_READER;
						state = ALPHA_0;
						break;
					case ALPHA_EQ:
					case VAR_EQ:
						if(type != 0 && type != 2) err(__LINE__);
						type = 2;
					case STR_EQ:
						_cle_emit0(parser,&code,isRef? OP_VAR_OUT : OP_READER_OUT);
						rt_stack.stop--;
					case ST_EQ:
						tmp = OP_MOVE_READER_FUN;
						_cle_put_stack(&rt_stack,1);
						state = ALPHA_EQ;
						break;
					case DOT_EQ:
					case APP_EQ:
						tmp = OP_MOVE_READER;
						state = ALPHA_EQ;
						break;
					case CURL_NEW:
					case ST_NEW:
						tmp = OP_DUB_MOVE_WRITER;
						_cle_put_stack(&rt_stack,1);
						state = ALPHA_NEW;
						break;
					case DOT_NEW:
						tmp = OP_MOVE_WRITER;
						state = ALPHA_NEW;
						break;
					default:
						err(__LINE__);
						state = ST_INIT;
					}

					if(c == '(')	// function call?
					{
						if(state != ALPHA_0 && state != ALPHA_EQ) err(__LINE__);

						_cle_oppush(parser,&stack,state,type,level,op - parser->top,OCALL,0,0);
						_cle_emit0(parser,&code,OP_NEW);
						_cle_put_stack(&rt_stack,1);
						parser->top = op;
						state = ST_NEW;

 						c = getc(parser->f);
					}
					else
						_cle_emitS(parser,&code,parser->opbuf + parser->top,op - parser->top,tmp);
				}
  				continue;
    		}
			else
			{
				//printf("bad char %c\n",c);
				err(__LINE__);
				return c;
			}
 		}

 		c = getc(parser->f);
	}
}

///////////////////////////////////////////////////////

static struct _cmp_state
{
	task* t;
	FILE* f;
	st_ptr* app;
	st_ptr* ref;
	char* opbuf;
	st_ptr root;
	st_ptr code;
	st_ptr anno;
	st_ptr strs;

	uint funidx;
	uint bsize;
	uint top;
};

static void _cmp_check_buffer(struct _cmp_state* cst, uint top)
{
	if(top >= cst->bsize)
	{
		uint diff = top - cst->bsize;

		cst->bsize += BUFFER_GROW > diff? BUFFER_GROW : diff;
		cst->opbuf = (char*)tk_realloc(cst->opbuf,cst->bsize);
	}
}

static void _cmp_whitespace(struct _cmp_state* cst, int* c)
{
	while(whitespace(*c))
		*c = getc(cst->f);
}

static uint _cmp_name(struct _cmp_state* cst, int* c)
{
	uint op = cst->top;
	while(alphanum(*c))
	{
		_cmp_check_buffer(cst,op);
		cst->opbuf[op++] = *c;
		*c = getc(cst->f);
	}

	if(op == cst->top) return 0;

	_cmp_check_buffer(cst,op);
	cst->opbuf[op++] = '\0';
	return op;
}

// setup call-site and funspace
static int _cmp_header(struct _cmp_state* cst, uchar public_fun)
{
	st_ptr tmpptr,params;
	uint op,funidx;
	int c = getc(cst->f);

	// get function name
	op = _cmp_name(cst,&c);
	if(!op) return(__LINE__);

	// goto/insert name and check for fun-ref
	st_insert(cst->t,cst->ref,cst->opbuf + cst->top,op - cst->top);
	_cmp_check_buffer(cst,cst->top + HEAD_SIZE + sizeof(uint));

	cst->opbuf[op + 1] = 0;

	tmpptr = *cst->ref;
	// exsisting function? no? make new funidx etc.
	if(st_get(&tmpptr,cst->opbuf + op,HEAD_SIZE + sizeof(uint)) > 0
		|| memcmp(cst->opbuf + op,HEAD_FUNCTION,HEAD_SIZE) != 0)
	{
		// get next funidx
		tmpptr = *cst->app;
		if(st_insert(t,&tmpptr,"\0:",2)
			|| st_get(&tmpptr,cst->opbuf + op,sizeof(uint)) > 0)
			funidx = 0;
		else
			funidx = *(uint*)(cst->opbuf + op + HEAD_SIZE);

		// next...
		++funidx;

		// update application
		tmpptr = *cst->app;
		st_move(&tmpptr,"\0:",2);
		st_update(cst->t,&tmpptr,(cdat)&funidx,sizeof(uint));

		// write fun-ref
		memcpy(cst->opbuf + op,HEAD_FUNCTION,HEAD_SIZE);
		*(uint*)(cst->opbuf + op + HEAD_SIZE) = funidx;
		st_update(cst->t,cst->ref,cst->opbuf + op,HEAD_SIZE + sizeof(uint));
	}
	else
		// get old fun ref
		funidx = *(uint*)(cst->opbuf + op + HEAD_SIZE);

	// goto funspace
	cst->funidx = funidx;
	cst->root = *cst->app;
	st_insert(cst->t,&cst->root,funspace,FUNSPACE_SIZE);
	st_insert(cst->t,&cst->root,cst->opbuf + op + HEAD_SIZE,sizeof(uint));

	// code
	cst->code = cst->root;
	st_insert(cst->t,&cst->anno,"B",2);

	// write public/private opcode

	// annotation
	cst->anno = cst->root;
	st_insert(cst->t,&cst->anno,"A",2);

	// get parameters
	_cmp_whitespace(cst,&c);
	if(c != '(') return(__LINE__);

	params = cst->anno;
	st_insert(cst->t,&params,"P",2);
	st_delete(cst->t,&params,0,0);	// clear any old

	c = getc(cst->f);
	while(c != ')')
	{
		if(c == '$')
		{
			op = _cmp_name(cst,&c);	// param: $name
			if(!op) return(__LINE__);

			// save param-name in annotation
			tmpptr = params;
			st_insert(cst->t,&tmpptr,cst->opbuf + cst->top,op - cst->top);

			// create as var and pre-load it in fun
		}
		if(c <= 0) return(__LINE__);

		_cmp_whitespace(cst,&c);
		if(c == ',') c = getc(cst->f);
	}

	return 0;
}

static int _cmp_body(struct _cmp_state* cst)
{
	// parse and emit
	return 0;	// return last c
}

static int _cmp_init_body(struct _cmp_state* cst)
{
	// setup string-space
	return 0;
}

int cmp_function(FILE* f, task* t, st_ptr* app, st_ptr* ref, uchar public_fun)
{
	struct _cmp_state cst;
	int ret;
	cst.t = t;
	cst.f = f;
	cst.app = app;
	cst.ref = ref;

	// create header:
	ret = _cmp_header(&cst,public_fun);
	if(ret) return ret;

	// create annotations
	ret = cle_write(f,t,0,&cst.anno,0,1);
	if(ret) return ret;

	ret = _cmp_init_body(&cst);
	if(ret) return ret;

	// compile body
	return _cmp_body(&cst);
}

int cmp_expr(FILE* f, task* t, st_ptr* app, st_ptr* ref)
{
	struct _cmp_state cst;
	int ret;
	cst.t = t;
	cst.f = f;
	cst.app = app;
	cst.ref = 0;
	cst.code = *ref;

	ret = _cmp_init_body(&cst);
	if(ret) return ret;

	return _cmp_body(&cst);
}
