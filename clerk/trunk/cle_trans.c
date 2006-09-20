/* Copyrigth(c) Lars Szuwalski, 2006 */

#include "cle_runtime.h"
#include "cle_struct.h"

#define BUFFERSIZE (PAGE_SIZE/2)
#define BUFFER_GROW 128

#define whitespace(c) (c == ' ' || c == '\t' || c == '\n' || c == '\r')
#define num(c) (c >= '0' && c <= '9')
#define minusnum(c) (c == '-' || num(c))
#define alpha(c) ((c & 0x80) || (c >= 'a' && c <= 'z')  || (c >= 'A' && c <= 'Z') || (c == '_'))
#define alphanum(c) (alpha(c) || num(c))

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
	if(parser->bsize <= top)
	{
		parser->bsize += BUFFER_GROW;
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

static void _cle_vpush(_parser_state* ps, uint length, uint level, uint name)
{
	struct _var_stack* elm = ps->vsfree;
	
	if(elm == 0)
		elm = (struct _var_stack*)tk_alloc(ps->t,sizeof(struct _var_stack));
	else
		ps->vsfree = elm->prev;

	elm->length = length;
	elm->level  = level;
	elm->name   = name;

	elm->id = ps->fs->vs? ps->fs->vs->id + 1 : 0;

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

/* "test ""test"" 'test'" | 'test ''test'' "test"' */
static int _cle_string(_parser_state* parser, st_ptr* out, int c, int* nxtchar)
{
	char buffer[BUFFERSIZE];
	int ic = 0,i = 0;

	while(1)
	{
		ic = getc(parser->f);
		if(ic == c)
		{
			ic = getc(parser->f);
			if(ic != c)
				break;
		}
		else if(ic <= 0)
			return(__LINE__);

		buffer[i++] = ic;
		if(i == BUFFERSIZE)
		{
			if(st_append(parser->t,out,buffer,i))
				return(__LINE__);
			i = 0;
		}
	}

	buffer[i++] = '\0';
	if(st_append(parser->t,out,buffer,i))
		return(__LINE__);

	*nxtchar = ic;
	return 0;
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

static int _cle_write(_parser_state* parser, st_ptr* root, uint clear);

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

static int _cle_var(_parser_state* parser, st_ptr* code, uint level, uchar newvar, uchar pathval_rw)
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

	if(newvar)
	{
		if(vs) err(__LINE__);	// re-def

		_cle_vpush(parser,len,level,parser->top);
		_cle_emitIs(parser,code,OP_DEF_VAR,parser->fs->vs->id);

		parser->top = op;
	}
	else if(vs)
	{
		switch(pathval_rw)
		{
		case 0:
			_cle_emitIs(parser,code,OP_LOAD_VAR,parser->fs->vs->id);
			break;
		case 1:
			_cle_emitIs(parser,code,OP_VAR_READ,parser->fs->vs->id);
			break;
		case 2:
			_cle_emitIs(parser,code,OP_VAR_WRITE,parser->fs->vs->id);
		}
	}
	else
	{
		st_ptr ptr;
		// check parameters
		ptr = parser->fs->code;
		if(st_move(&ptr,"A\0P",4)) err(__LINE__);

		if(st_exsist(&ptr,parser->opbuf + parser->top,len))
		{
			//its a parameter
			switch(pathval_rw)
			{
			case 0:
				_cle_emitS(parser,code,parser->opbuf + parser->top,len,OP_LOAD_PARAM);
				break;
			case 1:
				_cle_emitS(parser,code,parser->opbuf + parser->top,len,OP_PARAM_READ);
				break;
			case 2:
				_cle_emitS(parser,code,parser->opbuf + parser->top,len,OP_PARAM_WRITE);
			}
		}
		else
			err(__LINE__);	// var not found
	}

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
	struct _op_stack* stack = 0;

	uint type  = 0;	// 0 - any, 1 - tree, 2 - str,
	uint level = 0;
	enum parser_states state;

	int c = getc(parser->f);
	ushort stringidx;
	uchar tmp;

	if(expr && parser->fs)		// called from function header (parameter set)
	{
		strings = parser->fs->code;
		st_move(&strings,"S",2);
		stringidx = parser->fs->stringidx;
		state = ST_EQ;
	}
	else
	{
		strings = code;
		st_insert(parser->t,&strings,"S",2);	// begin string-space
		st_delete(parser->t,&strings,0,0);		// remove any old strings

		st_insert(parser->t,&code,"B",2);	// body-part
		st_delete(parser->t,&code,0,0);		// remove any old code
		stringidx = 0;
		state = ST_INIT;

		if(expr || parser->fs->publicfun)
			_cle_emit0(parser,&code,OP_PUBLIC_FUN);
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
				case ONEW:
					_cle_emit0(parser,&code,OP_POP_WRITER);	//	OP_WRITER_POP);
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
					_cle_emit0(parser,&code,OP_READER_POP);
					parser->fs->stringidx = stringidx;
					return c;
				}
				else
					err(__LINE__);
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
			tmp = 0;
			switch(state)
			{
			case ST_INIT:
				tmp = OP_APP_ROOT;
				state = APP_0;
				break;
			case ST_EQ:
				tmp = OP_APP_ROOT;
				state = APP_EQ;
				break;
			case ALPHA_EQ:
			case VAR_EQ:
				_cle_emit0(parser,&code,OP_READER_POP);
				tmp = OP_APP_ROOT;
				state = APP_EQ;
				type = 2;
				break;
			case ALPHA_0:
			case VAR_0:
				_cle_emit0(parser,&code,OP_READER_POP);
				tmp = OP_APP_ROOT;
				state = APP_0;
				type = 2;
				break;
			default:
				err(__LINE__);
				state = ST_INIT;
			}
			_cle_emit0(parser,&code,tmp);
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
				state = CURL_NEW;
				break;
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
			switch(state)
			{
			case CURL_0:
				_cle_emit0(parser,&code,OP_POP_READER);
				break;
			case CURL_NEW:
				_cle_emit0(parser,&code,OP_POP_WRITER);
//			case ST_NEW:
				break;
			default:
				err(__LINE__);
				state = ST_INIT;
			}

			if(level == 0) err(__LINE__);
			else
			{
				if(--level == 0)
					state = ST_INIT;

				if(expr == 0 && parser->fs->vs)
				{
					while(parser->fs->vs->level > level)
					{
						_cle_emitIs(parser,&code,OP_VAR_FREE,parser->fs->vs->id);
						_cle_vpop(parser);
					}
				}
			}
			break;
		case ',':
		case ';':
			tmp = 0;
			switch(state)
			{
			case ALPHA_0:
			case VAR_0:
				tmp = OP_READER_POP;
				state = level? CURL_0 : ST_INIT;
				break;
			case ALPHA_NEW:
			case VAR_NEW:
				tmp = OP_POP_WRITER;
				state = CURL_NEW;	// ST_NEW
				break;
			case STR_0:
				tmp = OP_READER_POP;	//OP_STR_POP;
				state = level? CURL_0 : ST_INIT;
				break;
			case ALPHA_EQ:
			case VAR_EQ:
				tmp = OP_READER_POP;	// (ins)
				break;
			case STR_EQ:
				tmp = OP_READER_POP;	//OP_STR_POP;
				break;
			case ST_VAR_DEF:
				if(c == ',') err(__LINE__);
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
//					if(stack->state == CURL_NEW || stack->state == ST_NEW)
						_cle_emit0(parser,&code,OP_POP_WRITER);
//					else
//						_cle_emit0(parser,&code,OP_WRITER_POP);
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
				return c;
			}
			break;
		case '\'':
		case '"':
			{
				int app = 0;
				tmp = 0;
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
					tmp = OP_READER_POP;
					state = STR_0;
					break;
				case ALPHA_EQ:
				case VAR_EQ:
					tmp = OP_READER_POP;
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
					cur_string = strings;
					st_insert(parser->t,&cur_string,(cdat)&stringidx,sizeof(ushort));
					stringidx++;
				}
				
				app = _cle_string(parser,&cur_string,c,&c);
				if(app) err(app);
				type = 2;
			}
			continue;
		case '=':
			if(type > 1) err(__LINE__);
			switch(state)
			{
			case ALPHA_0:
			case VAR_0:
				_cle_emit0(parser,&code,OP_READER_TO_WRITER);
				state = level? CURL_0 : ST_INIT;
				break;
			case ST_VAR_DEF:
				state = ST_INIT;
				break;
			case ALPHA_NEW:
			case VAR_NEW:
//				_cle_emit0(parser,&code,OP_READER_TO_WRITER);
				state = CURL_NEW;	//ST_NEW;
				break;
			default:
				err(__LINE__);
			}
			_cle_oppush(parser,&stack,state,type,level,0,OEQL,0,0);
			state = ST_EQ;
			type = 0;
			break;
		case '$':
			{
				uchar pathval_rw = 0;	// 0 - load, 1 - read with, 2 - write with
				switch(state)
				{
				case ST_INIT:
					state = VAR_0;
					break;
				case ST_EQ:
					state = VAR_EQ;
					break;
				case ALPHA_0:
				case VAR_0:
					_cle_emit0(parser,&code,OP_READER_POP);
					state = VAR_0;
					if(type != 0 && type != 2) err(__LINE__);
					type = 2;
					break;
				case STR_0:
					_cle_emit0(parser,&code,OP_READER_POP);	//OP_STR_POP);
					state = VAR_0;
					break;
				case ALPHA_EQ:
				case VAR_EQ:
					_cle_emit0(parser,&code,OP_READER_POP);
					state = VAR_EQ;
					if(type != 0 && type != 2) err(__LINE__);
					type = 2;
					break;
				case STR_EQ:
					_cle_emit0(parser,&code,OP_READER_POP);	//OP_STR_POP);
					state = VAR_EQ;
					break;
				case DOT_0:
				case CURL_0:
				case APP_0:
					state = VAR_0;
					pathval_rw = 1;
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
					c = _cle_var(parser,&code,0,0,pathval_rw);
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

						_cle_write(parser,&funcode,0);
 						c = getc(parser->f);
					}
					break;
				case 2:		// begin
					state = ST_INIT;
					err(__LINE__);
					break;
				case 3:		// end
					if(state != ST_INIT) err(__LINE__);

					if(stack)
					{
						//if(stack->opc == ONEW)
						//	_cle_emit0(parser,&code,OP_READER_POP);
						//else
						//	err(__LINE__);

						while(stack)
							_cle_oppop(parser,&stack);
					}

					if(parser->fs)
					{
						while(parser->fs->vs)
							_cle_vpop(parser);

						_cle_fpop(parser);
					}
					_cle_emit0(parser,&code,OP_FUNCTION_END);
					return c;
				case 4:		// new
					_cle_oppush(parser,&stack,state,type,level,0,ONEW,0,0);
					switch(state)
					{
					case ST_INIT:
						_cle_emit0(parser,&code,OP_WRITER_TO_READER);
						break;
					case ST_EQ:
//						_cle_emit0(parser,&code,OP_NEW);
						break;
					default:
						err(__LINE__);
					}
					state = ST_NEW;
					type = 1;
					break;
				case 5:		// var $name
					if(state != ST_INIT) err(__LINE__);
					if(expr) err(__LINE__);

					_cle_whitespace(parser,&c);

					if(c != '$') err(__LINE__);
					c = _cle_var(parser,&code,level,1,0);

					_cle_whitespace(parser,&c);

					state = ST_VAR_DEF;
					if(c == ';')
						_cle_emit0(parser,&code,OP_POP_WRITER);	//OP_WRITER_POP);
					else if(c != '=')
						state = ST_NEW;
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
					case ST_INIT:
						tmp = OP_MOVE_READER_FUN;
						state = ALPHA_0;
						break;
					case STR_0:
						_cle_emit0(parser,&code,OP_READER_POP);	//OP_STR_POP);
						tmp = OP_MOVE_READER;
						state = ALPHA_0;
						break;
					case ALPHA_0:
					case VAR_0:
						_cle_emit0(parser,&code,OP_READER_POP);
						if(type != 0 && type != 2) err(__LINE__);
						type = 2;
					case CURL_0:
						tmp = OP_DUB_MOVE_READER;
						state = ALPHA_0;
						break;
					case DOT_0:
					case APP_0:
						tmp = OP_MOVE_READER;
						state = ALPHA_0;
						break;
					case ST_EQ:
						tmp = OP_MOVE_READER_FUN;
						state = ALPHA_EQ;
						break;
					case STR_EQ:
						_cle_emit0(parser,&code,OP_READER_POP);	//OP_STR_POP);
						tmp = OP_MOVE_READER;
						state = ALPHA_EQ;
						break;
					case ALPHA_EQ:
					case VAR_EQ:
						_cle_emit0(parser,&code,OP_READER_POP);
						if(type != 0 && type != 2) err(__LINE__);
						type = 2;
					case DOT_EQ:
					case APP_EQ:
						tmp = OP_MOVE_READER;
						state = ALPHA_EQ;
						break;
					case CURL_NEW:
					case ST_NEW:
						tmp = OP_DUB_MOVE_WRITER;
						state = ALPHA_NEW;
						break;
//					case ST_NEW:
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
						if(tmp != OP_MOVE_READER && tmp != OP_MOVE_READER_FUN) err(__LINE__);

						_cle_oppush(parser,&stack,state,type,level,op - parser->top,OCALL,0,0);
						_cle_emit0(parser,&code,OP_PARAMS);
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

static int _cle_write(_parser_state* parser, st_ptr* root, uint clear)
{
	st_ptr after = *root;
	uint state = 0;
	uint level;
	int c = getc(parser->f);
	uchar infun = parser->fs?1:0;
	level = infun;

	if(clear)
		st_delete(parser->t,root,0,0);
	clear = 1;

	while(1)
	{
		switch(c)
		{
		case '.':
			return(__LINE__);
		case '{':
			if(state > 1) return(__LINE__);
			state = 0;
			_cle_ppush(parser,&after,'{');
			level++;
			break;
		case '}':
			if(state == 3) return(__LINE__);
			if(_cle_ppop(parser) != '{')
				return (__LINE__);
			level--;
			if(level == 0)
				return 0;
			after = parser->ps->pt;
			state = 0;
			break;
		case ';':
			if(state == 3) return(__LINE__);
			if(level == 0)
				return 0;
		case ',':
			if(state == 3) return(__LINE__);
			after = parser->ps->pt;
			state = 0;
			break;
		case ')':
			return 1;
		case '\'':
		case '"':
			if(state != 0 && state != 2) return(__LINE__);

			if(state == 2 || !clear)
			{
				char buffer[HEAD_SIZE];
				buffer[1] = 0;
				st_get(&after,buffer,HEAD_SIZE);
				if(memcmp(buffer,HEAD_STR,HEAD_SIZE) != 0)
					return(__LINE__);
			}
			else
				st_update(parser->t,&after,HEAD_STR,HEAD_SIZE);

			state = _cle_string(parser,&after,c,&c);
			if(state != 0)
				return state;
			state = 2;
			continue;
		case '+':
			if(state == 2) return(__LINE__);
			state = 3;
			break;
		case '=':
			if(state == 2 || state == 0) return(__LINE__);
			clear = (state != 3);
			if(clear)
				st_delete(parser->t,&after,0,0);
			state = 0;
			break;
		case ' ':
		case '\t':
		case '\r':
		case '\n':
			if(state == 3) return(__LINE__);
			break;
		default:
  			if(alpha(c))
  			{
				uint idx = 0;
				char buffer[BUFFERSIZE];

				if(state == 2) return(__LINE__);

  				do
  				{
					buffer[idx++] = (c == '.')? '\0' : c;
					if(idx == BUFFERSIZE - 2)
					{
						st_insert(parser->t,&after,buffer,idx);
						idx = 0;
					}
					c = getc(parser->f);
     			}
  				while(alphanum(c) || c == '.');

				if(buffer[idx] == '.')
					return(__LINE__);
				
				// private/public Name(params) [annotations] begin [expr] end
				if((idx == 7 && memcmp(buffer,keywords[0],7) == 0)
					|| (idx == 6 && memcmp(buffer,keywords[1],6) == 0))
				{
					st_ptr funcode;

					if(!whitespace(c)) return(__LINE__);
					if(state != 0) return(__LINE__);
					if(infun) return(__LINE__);

					_cle_function_header(parser,&funcode,&after,idx == 6);

					after = funcode;
					state = 0;
					infun = 1;
					level++;
				}
				else if(idx == 5 && memcmp(buffer,"begin",5) == 0)
				{
					uchar rmbuffer = parser->opbuf?0:1;

					if(!whitespace(c)) return(__LINE__);
					else if(state != 0) return(__LINE__);

					c = _cle_function_body(parser, infun?0:&after);

					if(rmbuffer)
					{
						tk_mfree(parser->opbuf);

						parser->opbuf = 0;
						parser->bsize = 0;
						parser->top   = 0;
					}

					if(infun && --level == 0)
						return 0;

					after = parser->ps->pt;
					infun = 0;
					continue;
				}
				else
				{
					buffer[idx++] = '\0';
					st_insert(parser->t,&after,buffer,idx);
					state = 1;
					continue;
				}
    		}
			else if(minusnum(c))
			{
				uint minus = 0; 
				int val = 0;
				if(c == '-')
				{
					minus = 1;
					c = getc(parser->f);	// eat whitespace
					if(!num(c))
						return(__LINE__);
				}

				do
				{
					val *= 10;
					val += c - '0';
					c = getc(parser->f);
				}
				while(num(c));

				if(minus) val *= -1;
				st_update(parser->t,&after,HEAD_INT,HEAD_SIZE);
				st_append(parser->t,&after,(cdat)&val,sizeof(int));
				state = 4;
				continue;
			}
			else
			{
				printf("bad char %c\n",c);
				return(__LINE__);
			}
 		}

 		c = getc(parser->f);
 	}
}

int cle_trans(FILE* f, task* t, st_ptr* root)
{
	_parser_state parser;
	st_ptr after = *root;
	uint state = 0;
	int c = getc(f);

	parser.bsize = 0;
	parser.errors = 0;
	parser.fs = 0;
	parser.ps = 0;
	parser.opbuf = 0;
	parser.fsfree = 0;
	parser.psfree = 0;
	parser.vsfree = 0;
	parser.osfree = 0;
	parser.top = 0;

	parser.funidx = 0;	// should read it from app TEST

	parser.app = *root;
	parser.f = f;
	parser.t = t;
	parser.line = 1;

	_cle_ppush(&parser,root,0);

	while(1)
	{
		switch(c)
		{
		case EOF:
		case 0:
			return 0;
		case '#':					// TEST TEST TEST
			st_prt_page(root);
			break;
		case '.':
		case '{':
			if(state != 1) return(__LINE__);
			state = (c == '.')? 2 : 0;
			_cle_ppush(&parser,&after,c);
			break;
		case '}':
		case ';':
			if(state > 1) return(__LINE__);

			if(rt_do_read(0,&parser.app,after))		// TOOL inspection reader (NO expr eval)
				return(__LINE__);

			while(_cle_pcheck_pop(&parser,'.'))
				;

			if(c == '}' && _cle_ppop(&parser) != '{')
				return(__LINE__);

			after = parser.ps->pt;
			state = 0;
			break;
		case '+':	// +=
			if(state == 2) return(__LINE__);
			state = 3;
			break;
		case '!':	// delete path;
			if(state == 0)
			{
				char buffer[BUFFERSIZE];
				uint idx = 0;

				c = getc(f);
				while((alphanum(c) || c == '.') && idx < BUFFERSIZE)
					buffer[idx++] = (c == '.')? '\0' : c;

				if(idx == BUFFERSIZE) return(__LINE__);
				if(c != ';') return(__LINE__);

				st_delete(parser.t,&parser.ps->pt,buffer,idx);
			}
			else
				return(__LINE__);
			break;
		case '=':
			if(state == 2) return(__LINE__);
			// write
			c = _cle_write(&parser,&after,state != 3);
			if(c)
				return c;
			state = 0;
			break;
		case ' ':
		case '\t':
		case '\r':
		case '\n':
			if(state == 3) return(__LINE__);
			break;
		default:
  			if(alpha(c))
  			{
				{
					uint idx = 0;
					char buffer[BUFFERSIZE];

					if(state == 1 || state == 3) return(__LINE__);
					after = parser.ps->pt;
  					do
  					{
  						buffer[idx++] = c;
						if(idx == BUFFERSIZE - 2)
						{
							if(st_move(&after,buffer,idx))
								return(__LINE__);
							idx = 0;
						}
						c = getc(f);
     				}
  					while(alphanum(c));

					buffer[idx++] = '\0';
					if(st_move(&after,buffer,idx))
						return(__LINE__);
				}

				state = 1;
				if(c != '(')
					continue;
				// get fun-def
				if(st_move(&after,HEAD_FUNCTION,HEAD_SIZE))
					return(__LINE__);
				else
				{
					st_ptr param;
					task* param_t = tk_create_task(0);

					st_empty(param_t,&param);
					parser.t = param_t;
					if(_cle_write(&parser,&param,0) != 1)	// parameters
						return(__LINE__);

					parser.t = t;

					if(rt_do_call(parser.t,&parser.app,&parser.ps->pt,&after,&param))
						return(__LINE__);

					tk_drop_task(param_t);
					state = 0;
				}
    		}
			else
				return(__LINE__);
 		}

 		c = getc(f);
 	}
}
