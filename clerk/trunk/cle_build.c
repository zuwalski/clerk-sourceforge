/* Copyrigth(c) Lars Szuwalski, 2006 */

#include "cle_runtime.h"
#include "cle_struct.h"

#define BUFFERSIZE (PAGE_SIZE/2)
#define BUFFER_GROW 128

#define whitespace(c) (c == ' ' || c == '\t' || c == '\n' || c == '\r')
#define num(c) (c >= '0' && c <= '9')
#define minusnum(c) (c == '-' || num(c))
#define alpha(c) ((c >= 'a' && c <= 'z')  || (c >= 'A' && c <= 'Z') || (c == '_'))
#define alphanum(c) (alpha(c) || num(c))

static char* keywords[] = {
	"function","begin","end","new","var","if","then","else","asc","desc",0
};

static void err(int line)
{
	printf("error on line %d\n",line);
}

typedef struct _ptr_stack
{
	struct _ptr_stack* prev;
	st_ptr pt;
	uint code;
}_ptr_stack;

static void _cle_ppush(task* t, _ptr_stack** stack, st_ptr* ptr, uint code)
{
	_ptr_stack* elm = (struct _ptr_stack*)t->free;
	
	if(elm == 0)
		elm = (_ptr_stack*)tk_alloc(t,sizeof(_ptr_stack));
	else
		t->free = elm->prev;

	elm->pt = *ptr;
	elm->code = code;
	elm->prev = *stack;

	*stack = elm;
}

static int _cle_ppop(task* t, _ptr_stack** stack)
{
	_ptr_stack* elm = *stack;

	if(elm == 0)
		return 0;

	*stack = elm->prev;
	elm->prev = (_ptr_stack*)t->free;
	t->free = elm;

	return (elm->code);
}

static int _cle_pop_until(task* t, _ptr_stack** stack, uint code)
{
	int ret = _cle_ppop(t,stack);
	while(ret && ret != code)
		ret = _cle_ppop(t,stack);
	return ret;
}

/* "test ""test"" 'test'" | 'test ''test'' "test"' */
static int _cle_string(task* t, st_ptr* out, FILE* f, int c, uchar opcode)
{
	char buffer[BUFFERSIZE];
	int ic = 0,i = 0;

	if(opcode)
		buffer[i++] = opcode;

	while(1)
	{
		int ic = getc(f);
		if(ic == c)
		{
			ic = getc(f);
			if(ic != c)
				break;
		}
		else if(ic <= 0)
		{
			err(__LINE__);
			break;
		}

		buffer[i++] = ic;
		if(i == BUFFERSIZE)
		{
			if(st_append(t,out,buffer,i))
				err(__LINE__);
			i = 0;
		}
	}

	buffer[i++] = '\0';
	if(st_append(t,out,buffer,i))
		err(__LINE__);

	return ic;
}

static struct _var_stack
{
	uint prev;
	uint name_offset;
	uint length;
	uint funlevel;
};

static struct _fun_body
{
	_ptr_stack* ps;

	FILE* f;
	task* t;
	char* opbuf;
	uint  stack;
	uint  bsize;
	uint  top;
	uint  funlevel;
};

static int _cle_write(FILE* f, task* t, st_ptr* root, struct _fun_body* fb, uint clear);

static void _cle_function_header(task* t, st_ptr* parent, st_ptr* after, _ptr_stack** ps)
{
	if(parent)
	{
		it_ptr it;
		st_ptr tmp = *after;
		uchar opc = OP_INNER_FUNCTION;

		*after = *parent;

		st_insert(t,after,"S",2);
		it_create(&it,after);
		it_new(t,&it,after,0);

		st_append(t,&tmp,&opc,1);			// output link to subfunction 
		st_append(t,&tmp,it.kdata,it.kused);// trigger context/session output

		it_dispose(&it);
	}

	st_update(t,after,HEAD_FUNCTION,HEAD_SIZE);	// function-header (clears ptr/no merge)
	_cle_ppush(t,ps,after,'F');

	st_insert(t,after,"A",2);		// annotation-part
	_cle_ppush(t,ps,after,'A');
}

static void _cle_check_buffer(struct _fun_body* fb, uint top)
{
	if(fb->bsize <= top)
	{
		fb->bsize += BUFFER_GROW;
		fb->opbuf = (char*)tk_realloc(fb->opbuf,fb->bsize);
	}
}

static int _cle_var(struct _fun_body* fb, int defname)
{
	/*
	uint op = fb->top;

	while(alphanum(defname) && op < sizeof(opbuf)-1)
	{
		_cle_check_buffer(fb);
		fb->opbuf[op++] = defname;
		defname = getc(fb->f);
	}

	if(op == sizeof(opbuf)-1) err(__LINE__);

	fb->opbuf[op] = '\0';

				uint length = op - fb->top;
	varref = (struct _var_stack*)((char*)fb->opbuf + fb->stack);

	while(varref)
	{
		if(varref->length == length
			&& memcmp(fb->opbuf[varref->name_offset],fb->opbuf[fb->top],length) == 0)
			break;

		varref = (struct _var_stack*)((char*)fb->opbuf + varref->prev);
	}

	if(varref == 0)
	{
		if(lvar)
		{
			tmp = OP_NEW_VAR;
			st_append(t,root,&tmp,1);
			op += op & (sizeof(int) - 1);

		}
		else
		{}
	}
	else if(lvar)
		err(__LINE__);


	char* search = opbuf;
	for(i = 0; *search == '$'; i++)	//todo: search parent functions
	{
		if(strcmp(opbuf,search) == 0)
		{
			search = 0;
			op = begin;
			break;
		}

		do
		{
			search++;
		}
		while(search < lock && *search != '$');
	}

	if(search)	// found (index i) read
	{
		if(lvar) err(__LINE__);	// redef var
		type = 2;
	}
	// not found
	else if(lvar)	// new var
	{
		tmp = OP_NEW_VAR;
		st_append(t,root,&tmp,1);
		lock = opbuf + op;
	}
	else			// read from parameters
	{
		st_ptr param = parent;
		if(!st_move(&param,"A\0Param",8)
			|| !st_exsist(&param,opbuf,op - begin))
			err(__LINE__);		// unknown parameter
		else
			type = 3;
	}
	*/
	return 0;
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

static void _cle_push_op(struct _fun_body* fb, uchar op, uchar prec)
{
	_cle_check_buffer(fb,fb->top + 2);
	fb->opbuf[fb->top++] = op;
	fb->opbuf[fb->top++] = prec;
}

static void _cle_pop_op(struct _fun_body* fb)
{
}

static int _cle_function_body(struct _fun_body* fb)
{
	struct _var_stack* varref = 0;
	st_ptr parent;

	uint writing = 0;
	uint type  = 0;		// 0 - init, 1 - path, 2 - localvar 3 - paramvar, 4 - str, 5 - num
	uint state = 0;
	uint level = 0;

	uchar tmp;
	int c = getc(fb->f);

	st_ptr* code = &(fb->ps->pt);
	parent = *code;

	st_insert(fb->t,code,"B",2);		// body-part

	_cle_push_op(fb,0,0);

	while(1)
	{
		switch(c)
		{
		case '[':
			break;
		case ']':
			break;
		case '(':
			break;
		case ')':
			break;
		case '.':
			state = 2;
			type  = 1;
			break;
		case '{':
			level++;
		case ',':
		case ';':
			state = 0;
			break;
		case '}':
			level--;
			state = 0;
			break;
		case '\'':
		case '"':
			if(state == 2) err(__LINE__);
			c = _cle_string(fb->t,code,fb->f,c,OP_STR);
			state = 3;
			type  = 4;
			continue;
		case '=':
			state = 0;
			break;
		case '$':
			_cle_var(fb,0);
			break;
		case ' ':
		case '\t':
		case '\n':
		case '\r':
			break;
		default:
  			if(alpha(c))
  			{
				uint op = fb->top;
				do
  				{
					_cle_check_buffer(fb,op);
					fb->opbuf[op++] = c;
					c = getc(fb->f);
     			}
  				while(alphanum(c));

				fb->opbuf[op] = '\0';

				switch(_cle_keyword(fb->opbuf + fb->top))
				{
				case 0:		// function (in function)
					if(!writing || !whitespace(c)) err(__LINE__);
					else
					{
						st_ptr froot = *code;
						_cle_function_header(fb->t,&parent,&froot,&fb->ps);

						fb->funlevel++;
						_cle_write(fb->f,fb->t,&froot,fb,0);
						fb->funlevel--;
					}
					break;
				case 1:		// begin
					err(__LINE__);
					break;
				case 2:		// end
					if(state != 0) err(__LINE__);
					if(!whitespace(c)) err(__LINE__);
					tmp = OP_FUNCTION_END;
					st_append(fb->t,code,&tmp,1);
					return 0;
				case 4:		// new
					if(state != 0) err(__LINE__);
					if(!whitespace(c)) err(__LINE__);
					if(!writing)
						writing = level;
					tmp = OP_NEW;
					st_append(fb->t,code,&tmp,1);
					break;
				case 5:		// var Name
					if(state != 0) err(__LINE__);
					if(!whitespace(c)) err(__LINE__);
					if(type != 0) err(__LINE__);
					do
					{
						c = getc(fb->f);
					}
					while(whitespace(c));
					_cle_var(fb,c);
					break;
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
					if(writing)
					{
						tmp = OP_MOVE_WRITER;
						st_append(fb->t,code,&tmp,1);
						st_append(fb->t,code,fb->opbuf + fb->top,op - fb->top);
					}
					else
					{
						tmp = OP_MOVE_READER;
						st_append(fb->t,code,&tmp,1);
						st_append(fb->t,code,fb->opbuf + fb->top,op - fb->top);
					}

					if(c == '(')	// call
					{
						if(writing)
							err(__LINE__);

						tmp = OP_BEGIN_CALL;
						st_append(fb->t,code,&tmp,1);
						st_append(fb->t,code,fb->opbuf + fb->top,op - fb->top);
						_cle_push_op(fb,tmp,0);
					}
				}
  				continue;
    		}
			else
			{
				err(__LINE__);
				return c;
			}
 		}

 		c = getc(fb->f);
	}
}

static int _cle_write(FILE* f, task* t, st_ptr* root, struct _fun_body* fb, uint clear)
{
	_ptr_stack* ps = 0;
	st_ptr after;
	uint state = 0;
	int c = getc(f);
	uchar infun = fb?1:0;

	if(clear)
		st_delete(t,root,0,0);

	after = *root;
	_cle_ppush(t,&ps,root,0);

	while(1)
	{
		switch(c)
		{
		case '.':
			err(__LINE__);
			break;
		case '{':
			if(state > 1) err(__LINE__);
			state = 0;
			_cle_ppush(t,&ps,&after,c);
			break;
		case '}':
			if(state == 3) err(__LINE__);
			if(ps->prev == 0)
				return -1;
			_cle_ppop(t,&ps);
			if(ps->prev == 0 && !infun)
				return 0;
			state = 0;
			break;
		case ';':
		case ',':
			if(state == 3) err(__LINE__);
			state = 0;
			break;
		case ')':
			if(infun) err(__LINE__);
			return c;
		case '\'':
		case '"':
			if(state != 0 && state != 3) err(__LINE__);
			c = _cle_string(t,&ps->pt,f,c,0);
			state = 2;
			continue;
		case '+':
			if(state == 2) err(__LINE__);
			state = 3;
			break;
		case '=':
			if(state == 2 || state == 0) err(__LINE__);
			if(state != 3)
				st_delete(t,&ps->pt,0,0);
			state = 0;
			break;
		case ' ':
		case '\t':
		case '\r':
		case '\n':
			break;
		default:
  			if(alpha(c))
  			{
				uint idx = 0;
				char buffer[BUFFERSIZE];

				if(state == 2) err(__LINE__);

				after = ps->pt;
  				do
  				{
					buffer[idx++] = (c == '.')? '\0' : c;
					if(idx == BUFFERSIZE - 2)
					{
						if(st_append(t,&after,buffer,idx))
							err(__LINE__);
						idx = 0;
					}
					c = getc(f);
     			}
  				while(alphanum(c) || c == '.');

				if(buffer[idx] == '.') err(__LINE__);
				// function [annotations] begin [expr] end
				else if(idx == 8 && memcmp(buffer,"function",8) == 0)
				{
					if(!whitespace(c)) err(__LINE__);
					if(state != 0) err(__LINE__);
					if(infun) err(__LINE__);
					else
					{
						infun = 1;
						_cle_function_header(t,0,&after,&ps);
					}
				}
				else if(idx == 5 && memcmp(buffer,"begin",5) == 0)
				{
					if(!whitespace(c)) err(__LINE__);
					else if(state != 0) err(__LINE__);
					else if(!infun) err(__LINE__);

					else if(_cle_ppop(t,&ps) != 'A')
						err(__LINE__);
					else if(fb)
						_cle_function_body(fb);
					else
					{
						struct _fun_body sfb;

						sfb.opbuf = 0;
						sfb.bsize = 0;
						sfb.stack = sfb.top = sfb.funlevel = 0;
						sfb.ps = ps;
						sfb.f = f;
						sfb.t = t;

						_cle_function_body(&sfb);

						tk_mfree(sfb.opbuf);
					}

					if(_cle_ppop(t,&ps) != 'F')
						err(__LINE__);

					if(ps->prev == 0)
						return 0;
					infun = 0;
				}
				else
				{
					buffer[idx++] = '\0';
					if(st_append(t,&after,buffer,idx))
						err(__LINE__);
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
					c = getc(f);
					if(!num(c))
					{
						err(__LINE__);
						continue;
					}
				}

				do
				{
					val *= 10;
					val += c - '0';
					c = getc(f);
				}
				while(num(c));

				if(minus) val *= -1;
				after = ps->pt;
				st_update(t,&after,HEAD_INT,HEAD_SIZE);
				st_append(t,&after,(cdat)val,sizeof(int));

				state = 2;
				continue;
			}
			else
			{
				err(__LINE__);
				return c;
			}
 		}

 		c = getc(f);
 	}
}
