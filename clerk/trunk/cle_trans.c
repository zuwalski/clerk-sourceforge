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

#define BUFFERSIZE (PAGE_SIZE/2)

static struct _ptr_stack
{
	struct _ptr_stack* prev;
	st_ptr pt;
	uint code;
};

static struct _the_stack
{
	struct _ptr_stack* ps;
	struct _ptr_stack* psfree;
};

static void _cle_ppush(task* t, struct _the_stack* ps, st_ptr* ptr, uint code)
{
	struct _ptr_stack* elm = ps->psfree;
	
	if(elm == 0)
		elm = (struct _ptr_stack*)tk_alloc(t,sizeof(struct _ptr_stack));
	else
		ps->psfree = elm->prev;

	elm->pt = *ptr;
	elm->code = code;
	elm->prev = ps->ps;

	ps->ps = elm;
}

static int _cle_ppop(struct _the_stack* ps)
{
	struct _ptr_stack* elm = ps->ps;

	if(elm == 0)
		return 0;

	ps->ps = elm->prev;
	elm->prev = ps->psfree;
	ps->psfree = elm;

	return (elm->code);
}

static uint _cle_pcheck_pop(struct _the_stack* ps, uint code)
{
	struct _ptr_stack* elm = ps->ps;

	if(elm == 0 || elm->code != code)
		return 0;

	ps->ps = elm->prev;
	elm->prev = ps->psfree;
	ps->psfree = elm;
	return 1;
}

/* "test ""test"" 'test'" | 'test ''test'' "test"' */
int cle_string(FILE* f, task* t, st_ptr* out, int c, int* nxtchar, uchar append)
{
	char buffer[BUFFERSIZE];
	int ic = 0,i = 0;

	if(append)
	{
		buffer[1] = 0;
		st_get(out,buffer,HEAD_SIZE);
		if(memcmp(buffer,HEAD_STR,HEAD_SIZE) != 0)
			return(__LINE__);
	}
	else
		st_update(t,out,HEAD_STR,HEAD_SIZE);

	while(1)
	{
		ic = getc(f);
		if(ic == c)
		{
			ic = getc(f);
			if(ic != c)
				break;
		}
		else if(ic <= 0)
			return(__LINE__);

		buffer[i++] = ic;
		if(i == BUFFERSIZE)
		{
			if(st_append(t,out,buffer,i))
				return(__LINE__);
			i = 0;
		}
	}

	buffer[i++] = '\0';
	if(st_append(t,out,buffer,i))
		return(__LINE__);

	*nxtchar = ic;
	return 0;
}

void cle_num(task* t, st_ptr* out, int num)
{
	st_update(t,out,HEAD_INT,HEAD_SIZE);
	st_append(t,out,(cdat)&num,sizeof(int));
}

int cle_write(FILE* f, task* t, st_ptr* root, uint clear, uchar infun)
{
	struct _the_stack ps;
	st_ptr after = *root;
	uint state = 0;
	uint level = 0;
	int c = getc(f);

	ps.ps = ps.psfree = 0;

	if(clear)
		st_delete(t,root,0,0);
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
			_cle_ppush(t,&ps,&after,'{');
			level++;
			break;
		case '}':
			if(state == 3) return(__LINE__);
			if(_cle_ppop(&ps) != '{')
				return (__LINE__);
			level--;
			if(level == 0)
				return 0;
			after = ps.ps->pt;
			state = 0;
			break;
		case ';':
			if(state == 3) return(__LINE__);
			if(level == 0)
				return 0;
		case ',':
			if(state == 3) return(__LINE__);
			after = ps.ps->pt;
			state = 0;
			break;
		case ')':
			return 1;
		case '\'':
		case '"':
			if(state != 0 && state != 2) return(__LINE__);

			state = cle_string(f,t,&after,c,&c,state == 2 || !clear);

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
				st_delete(t,&after,0,0);
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
						st_insert(t,&after,buffer,idx);
						idx = 0;
					}
					c = getc(f);
     			}
  				while(alphanum(c) || c == '.');

				if(buffer[idx] == '.')
					return(__LINE__);
				
				// function Name(params) [annotations] begin [expr] end
				if((idx == 8 && memcmp(buffer,"function",8) == 0))
				{
					if(!whitespace(c)) return(__LINE__);
					if(state != 0) return(__LINE__);
					if(infun) return(__LINE__);

					c = cmp_function(f,t,&after);

					after = ps.ps->pt;
				}
				else if(idx == 5 && memcmp(buffer,"begin",5) == 0)
				{
					if(!whitespace(c)) return(__LINE__);
					if(state != 0) return(__LINE__);

					if(infun)
						return (level != 0? __LINE__ : 0);

					c = cmp_expr(f,t,&after);

					after = ps.ps->pt;
				}
				else
				{
					buffer[idx++] = '\0';
					st_insert(t,&after,buffer,idx);
					state = 1;
				}
				continue;
    		}
			else if(minusnum(c))
			{
				uint minus = 0; 
				int val = 0;
				if(c == '-')
				{
					minus = 1;
					c = getc(f);	// eat whitespace
					if(!num(c))
						return(__LINE__);
				}

				do
				{
					val *= 10;
					val += c - '0';
					c = getc(f);
				}
				while(num(c));

				if(minus) val *= -1;
				cle_num(t,&after,val);
				state = 4;
				continue;
			}
			else
			{
//				printf("bad char %c\n",c);
				return(__LINE__);
			}
 		}

 		c = getc(f);
 	}
}

int cle_trans(FILE* f, task* t, st_ptr* app)
{
	struct _the_stack ps;
	st_ptr after = *app;
	uint state = 0;
	int c = getc(f);

	ps.ps = ps.psfree = 0;

	_cle_ppush(t,&ps,app,0);

	while(1)
	{
		switch(c)
		{
		case EOF:
		case 0:
			return 0;
		case '#':					// TEST TEST TEST
			st_prt_page(app);
			break;
		case '.':
		case '{':
			if(state != 1) return(__LINE__);
			state = (c == '.')? 2 : 0;
			_cle_ppush(t,&ps,&after,c);
			break;
		case '}':
		case ';':
			if(state > 1) return(__LINE__);

			if(rt_do_read(0,app,after))		// TOOL inspection reader (NO expr eval)
				return(__LINE__);

			while(_cle_pcheck_pop(&ps,'.'))
				;

			if(c == '}' && _cle_ppop(&ps) != '{')
				return(__LINE__);

			after = ps.ps->pt;
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

				st_delete(t,&ps.ps->pt,buffer,idx);
			}
			else
				return(__LINE__);
			break;
		case '=':
			if(state == 2) return(__LINE__);
			// write
			c = cle_write(f,t,&after,state != 3,0);
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
					after = ps.ps->pt;
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
				else
				{
					st_ptr param;
					task* param_t = tk_create_task(t);

					st_empty(param_t,&param);
					c = cle_write(f,param_t,&param,0,1);	// parameters
					if(c != 1)
						return c;

					if(rt_do_call(t,app,&ps.ps->pt,&after,&param))
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
