/* Copyrigth(c) Lars Szuwalski, 2006 */

#include "cle_runtime.h"
#include "cle_struct.h"

#define BUFFER_GROW 256

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

static enum opc
{
	OPARN,	// (
	OPARN_END,	// )
	OLOADPARAM,
	OCALL,	// ( - begin call
	ONEW,	// new
	OPIPE,
	OEQL,	// =
	OAPP,
	OSTR,
	ODOT,	//.
	OPUSH,	//{
	OPOP,	//}
	OVAR,
	OVARREF,
	OVARREAD,
	OVARWRITE,
	OALPHA,
	OALPHANEW,
	ONUMOP,	// + - * / %
	OTERM
};

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

	// code-size
	uint code_size;

	// {}
	uint level;

	// top-var
	uint top_var;

	// opc
	uint first_opc;
	uint top_opc;

	// prg-stack
	uint s_top;
	uint s_max;
};

static struct _cmp_var
{
	uint prev;
	uint id;
	uint name;
	uint leng;
	uint type;
	uint level;
};
#define PEEK_VAR(v) ((struct _cmp_var*)(cst->opbuf + v))

static struct _cmp_op
{
	uint next;
	uint atcode;
	uint name;
	uint leng;
	uint prec;
	enum opc opc;
	uint imm;
};
#define PEEK_OP(o) ((struct _cmp_op*)(cst->opbuf + o))

static const char* keywords[] = {
	"private","public","begin","end","new","var","if","then","else","asc","desc",0
};

static void err(int line)
{
	printf("error on line %d\n",line);
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

static void _cmp_emit0(struct _cmp_state* cst, uchar opc)
{
	if(opc)
	{
		st_append(cst->t,&cst->code,&opc,1);
		cst->code_size++;
	}
}

static void _cmp_emitS(struct _cmp_state* cst, uchar opc, char* param, ushort len)
{
	if(opc)
	{
		st_append(cst->t,&cst->code,&opc,1);
		st_append(cst->t,&cst->code,(cdat)&len,sizeof(ushort));
		st_append(cst->t,&cst->code,param,len);
		cst->code_size += len + 1 + sizeof(ushort);
	}
}

static void _cmp_emitIs(struct _cmp_state* cst, uchar opc, ushort imm)
{
	if(opc)
	{
		st_append(cst->t,&cst->code,&opc,1);
		st_append(cst->t,&cst->code,(cdat)&imm,sizeof(ushort));
		cst->code_size += 1 + sizeof(ushort);
	}
}

static void _cmp_stack(struct _cmp_state* cst, int diff)
{
	cst->s_top += diff;
	if(cst->s_top > cst->s_max)
		cst->s_max = cst->s_top;
}

static void _cmp_def_var(struct _cmp_state* cst, uint op)
{
	struct _cmp_var* var;
	uint top,begin = op;
	if(begin & 3)
		begin += 4 - (cst->top & 3);
	top = begin + sizeof(struct _cmp_var);

	_cmp_check_buffer(cst,top);

	var = PEEK_VAR(begin);

	var->prev = cst->top_var;
	var->id   = cst->s_top;
	var->name = cst->top;
	var->leng = op - cst->top;
	var->type = 0;
	var->level = cst->level;

	_cmp_stack(cst,1);

	cst->top_var = begin;
	cst->top = top;
}

static struct _cmp_var* _cmp_find_var(struct _cmp_state* cst, uint op)
{
	struct _cmp_var* var = 0;
	uint nxtvar = cst->top_var;

	while(nxtvar)
	{
		var = PEEK_VAR(nxtvar);
		if(op - cst->top == var->leng &&
			memcmp(cst->opbuf + cst->top,cst->opbuf + var->name,var->leng) == 0)
			return var;

		nxtvar = var->prev;
	}

	return 0;
}

// operator to instruction-stack
static void _cmp_push_op(struct _cmp_state* cst, enum opc opc, uint imm, uint prec, uint op)
{
	struct _cmp_op* cop;
	uint top,begin = op;
	if(begin & 3)
		begin += 4 - (cst->top & 3);
	top = begin + sizeof(struct _cmp_op);

	_cmp_check_buffer(cst,top);

	cop = PEEK_OP(begin);

	cop->next = 0;
	cop->atcode = cst->code_size;
	cop->name = cst->top;
	cop->leng = op - cst->top;
	cop->prec = prec;
	cop->opc = opc;
	cop->imm = imm;

	if(cst->top_opc)
	{
		cop = PEEK_OP(cst->top_opc);
		cop->next = begin;
	}
	else
		cst->first_opc = begin;

	cst->top_opc = begin;
	cst->top = top;
}

static void _cmp_opc_emit(struct _cmp_state* cst)
{
	uint nopc = cst->first_opc;
	cst->first_opc = cst->top_opc = 0;

	while(nopc)
	{
		struct _cmp_op* cop = PEEK_OP(nopc);

		nopc = cop->next;
	}
}

// parse body to instruction-stack
static int _cmp_body(struct _cmp_state* cst, uchar mode)
{
	st_ptr cur_string;
	enum parser_states state = (mode? ST_EQ : ST_INIT);
	int c = getc(cst->f);
	uint lock_level  = 0;
	uint paran_level = 0;
	ushort stringidx = 0;

	while(1)
	{
		switch(c)
		{
		case '(':
			paran_level++;
			_cmp_push_op(cst,OPARN,0,0,0);
			break;
		case ')':
			if(paran_level == 0) err(__LINE__);
			else
			{
				paran_level--;
				_cmp_push_op(cst,OPARN_END,0,0,0);
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
			_cmp_push_op(cst,ODOT,0,0,0);
			break;
		case '\\':
			switch(state)
			{
			case ALPHA_0:
			case VAR_0:
			case ST_INIT:
				state = APP_0;
				break;
			case ALPHA_EQ:
			case VAR_EQ:
			case ST_EQ:
				state = APP_EQ;
				break;
			default:
				err(__LINE__);
				state = ST_INIT;
			}
			_cmp_push_op(cst,OAPP,0,0,0);
			break;
		case '{':
			cst->level++;
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
			_cmp_push_op(cst,OPUSH,cst->level,0,0);
			break;
		case '}':
			switch(state)
			{
			case CURL_0:
			case CURL_NEW:
				break;
			default:
				err(__LINE__);
				state = ST_INIT;
			}

			if(cst->level == 0) err(__LINE__);
			else
			{
				cst->level--;
				_cmp_push_op(cst,OPOP,cst->level,0,0);
			}
			break;
		case '|':
			switch(state)
			{
			case ALPHA_0:
			case VAR_0:
			case STR_0:
			case ALPHA_EQ:
			case VAR_EQ:
			case STR_EQ:
				break;
			default:
				err(__LINE__);
			}
			_cmp_push_op(cst,OPIPE,0,0,0);
			state = ST_EQ;
			break;
		case '=':
			switch(state)
			{
			case ALPHA_0:
				state = cst->level? CURL_0 : ST_INIT;
				break;
			case VAR_0:
			case ST_VAR_DEF:
				state = cst->level? CURL_0 : ST_INIT;
				break;
			case ALPHA_NEW:
			case VAR_NEW:
				state = CURL_NEW;
				break;
			default:
				err(__LINE__);
			}
			_cmp_push_op(cst,OEQL,state,0,0);
			state = ST_EQ;
			break;
		case ',':
		case ';':
			switch(state)
			{
			case VAR_0:
			case ALPHA_0:
			case STR_0:
				state = cst->level? CURL_0 : ST_INIT;
				break;
			case ALPHA_NEW:
			case VAR_NEW:
				state = CURL_NEW;
				break;
			case ALPHA_EQ:
			case VAR_EQ:
			case STR_EQ:
				state = lock_level? CURL_NEW : (cst->level? CURL_0 : ST_INIT);
				break;
			case ST_VAR_DEF:
				if(c == ',' && mode != 2) err(__LINE__);
				state = ST_INIT;
				break;
			default:
				err(__LINE__);
				state = ST_INIT;
			}

			if(cst->level == 0 || cst->level < lock_level)
			{
				lock_level = 0;
				_cmp_opc_emit(cst);

				if(mode)
					return c;
			}
			else
				_cmp_push_op(cst,OTERM,0,0,0);
			break;
		case '\'':
		case '"':
			{
				int app = 0;
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
					state = STR_0;
					break;
				case ALPHA_EQ:
				case VAR_EQ:
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

				if(app == 0)
				{
					cur_string = cst->strs;
					st_insert(cst->t,&cur_string,(cdat)&stringidx,sizeof(ushort));
					_cmp_push_op(cst,OSTR,stringidx,0,0);
					stringidx++;
				}
				
				app = cle_string(cst->f,cst->t,&cur_string,c,&c,app);
				if(app) err(app);
			}
			continue;
		case '$':
			{
				uint op;
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
				case STR_0:
					state = VAR_0;
					break;
				case DOT_0:
				case APP_0:
					state = VAR_0;
					pathval_rw = 1;
					break;
				case ALPHA_EQ:
				case VAR_EQ:
				case STR_EQ:
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

				c = getc(cst->f);
				op = _cmp_name(cst,&c);
				if(op)
				{
					struct _cmp_var* var = _cmp_find_var(cst,op);
					if(var)
					{
						switch(pathval_rw)
						{
						case 0:
							_cmp_push_op(cst,OVAR,var->id,0,0);
							break;
						case 1:
							_cmp_push_op(cst,OVARREAD,var->id,0,0);
							break;
						case 2:
							_cmp_push_op(cst,OVARWRITE,var->id,0,0);
							break;
						case 3:
							_cmp_push_op(cst,OVARREF,var->id,0,0);
							break;
						}
					}
					else
						err(__LINE__);
				}
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
				uint op = _cmp_name(cst,&c);

				keyword = _cmp_keyword(cst->opbuf + cst->top);
				switch(keyword)
				{
				case 0:		// private
				case 1:		// public
				case 2:		// begin
					err(__LINE__);
					state = ST_INIT;
					break;
				case 3:		// end
					if(state != ST_INIT) err(__LINE__);
					_cmp_opc_emit(cst);

					_cmp_emitIs(cst,OP_FUNCTION_END,cst->s_max);
					return c;
				case 4:		// new
					_cmp_push_op(cst,ONEW,0,0,0);
					switch(state)
					{
					case ST_INIT:
					case ST_EQ:
						break;
					default:
						err(__LINE__);
					}
					if(lock_level == 0)
						lock_level = cst->level + 1;
					state = ST_NEW;
					break;
				case 5:		// var $name
					if(state != ST_INIT && state != CURL_0) err(__LINE__);
					if(mode == 2) err(__LINE__);

					_cmp_whitespace(cst,&c);

					if(c != '$') err(__LINE__);

					op = _cmp_name(cst,&c);
					if(op > 0)
					{
						if(_cmp_find_var(cst,op))
							err(__LINE__);			// already defined
						else
							_cmp_def_var(cst,op);
					}

					_cmp_whitespace(cst,&c);
					switch(c)
					{
					case '=':
					case ';':
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
					switch(state)
					{
					case ALPHA_0:
					case VAR_0:
					case STR_0:
					case ST_INIT:
						state = ALPHA_0;
						break;
					case CURL_0:
						state = ALPHA_0;
						break;
					case DOT_0:
					case APP_0:
						state = ALPHA_0;
						break;
					case ALPHA_EQ:
					case VAR_EQ:
					case STR_EQ:
					case ST_EQ:
						state = ALPHA_EQ;
						break;
					case DOT_EQ:
					case APP_EQ:
						state = ALPHA_EQ;
						break;
					case CURL_NEW:
					case ST_NEW:
						state = ALPHA_NEW;
						break;
					case DOT_NEW:
						state = ALPHA_NEW;
						break;
					default:
						err(__LINE__);
						state = ST_INIT;
					}

					if(c == '(')	// function call?
					{
						if(state != ALPHA_0 && state != ALPHA_EQ) err(__LINE__);

						paran_level++;
						_cmp_push_op(cst,OCALL,0,0,op);
						state = ST_NEW;

 						c = getc(cst->f);
					}
					else if(state == ALPHA_NEW)
						_cmp_push_op(cst,OALPHANEW,0,0,op);
					else
						_cmp_push_op(cst,OALPHA,0,0,op);
				}
  				continue;
    		}
			else
			{
				err(__LINE__);
				return c;
			}
 		}

 		c = getc(cst->f);
	}

	return 0;	// return last c
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
		if(st_insert(cst->t,&tmpptr,"\0:",2)
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

	// clean
	st_delete(cst->t,&cst->root,0,0);

	// code
	cst->code = cst->root;
	st_insert(cst->t,&cst->anno,"B",2);

	// write public/private opcode
	if(public_fun)
		_cmp_emit0(cst,OP_PUBLIC_FUN);

	// annotation
	cst->anno = cst->root;
	st_insert(cst->t,&cst->anno,"A",2);

	// strings
	cst->strs = cst->root;
	st_insert(cst->t,&cst->strs,"S",2);

	// get parameters
	_cmp_whitespace(cst,&c);
	if(c != '(') return(__LINE__);

	params = cst->anno;
	st_insert(cst->t,&params,"P",2);
	st_delete(cst->t,&params,0,0);	// clear any old

	c = getc(cst->f);
	while(1)
	{
		_cmp_whitespace(cst,&c);

		if(c == '$')
		{
			op = _cmp_name(cst,&c);	// param: $name
			if(!op) return(__LINE__);

			// save param-name in annotation
			tmpptr = params;
			st_insert(cst->t,&tmpptr,cst->opbuf + cst->top,op - cst->top);

			// create as var and pre-load it in fun
			_cmp_whitespace(cst,&c);
			if(c == '=')
			{
				// default value
				_cmp_push_op(cst,OLOADPARAM,0,0,op);
				c = _cmp_body(cst,2);
			}
			else
			{
				_cmp_emitS(cst,OP_LOAD_PARAM,cst->opbuf + cst->top,op - cst->top);
				_cmp_stack(cst,1);
			}
		}
		
		if(c == ',')
			c = getc(cst->f);
		else
			break;
	}

	return (c == ')'? 0 : __LINE__);
}

// setup _cmp_state
static void _cmp_init(struct _cmp_state* cst, FILE* f, task* t, st_ptr* app)
{
	cst->t = t;
	cst->f = f;
	cst->app = app;
	cst->s_top = cst->s_max = 0;
	cst->top_var = 0;
	cst->level = 0;
	cst->code_size = 0;
	cst->opbuf = 0;
	cst->bsize = cst->top = 0;
	cst->first_opc = cst->top_opc = 0;
}

int cmp_function(FILE* f, task* t, st_ptr* app, st_ptr* ref, uchar public_fun)
{
	struct _cmp_state cst;
	int ret;

	_cmp_init(&cst,f,t,app);
	cst.ref = ref;

	// create header:
	ret = _cmp_header(&cst,public_fun);
	if(ret == 0)
	{
		// create annotations
		ret = cle_write(f,t,0,&cst.anno,0,1);
		if(ret == 0)
			// compile body
			ret = _cmp_body(&cst,0);
	}

	tk_mfree(cst.opbuf);
	return ret;
}

int cmp_expr(FILE* f, task* t, st_ptr* app, st_ptr* ref)
{
	struct _cmp_state cst;
	int ret;

	_cmp_init(&cst,f,t,app);
	cst.ref = 0;
	cst.root = *ref;

	// clean and mark expr
	st_update(t,&cst.root,HEAD_EXPR,HEAD_SIZE);

	// code (expr's never public)
	cst.code = cst.root;
	st_insert(t,&cst.code,"B",2);

	// strings
	cst.strs = cst.root;
	st_insert(t,&cst.strs,"S",2);

	ret = _cmp_body(&cst,1);

	tk_mfree(cst.opbuf);
	return ret;
}
