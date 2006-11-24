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
#ifndef __CLE_RUNTIME_H__
#define __CLE_RUNTIME_H__

#include "cle_clerk.h"

#define ERROR_MAX 20

#define HEAD_SIZE 2
#define HEAD_FUNCTION "\0F"
#define HEAD_TRIGGER "\0T"
#define HEAD_EXPR "\0E"
#define HEAD_INT "\0I"
#define HEAD_STR "\0S"

enum cle_opcode
{
	OP_ILLEGAL = 0,
	OP_NOOP,
	OP_STR,
	OP_PUBLIC_FUN,
	OP_FUNCTION_END,
	OP_FUNCTION_REF,
	OP_TRIGGER_REF,
	OP_FUNCTION_REF_DONE,
	OP_NEW,
	OP_CALL,
	OP_APP_ROOT,
	OP_MOVE_WRITER,
	OP_DUB_MOVE_WRITER,
	OP_MOVE_READER,
	OP_DUB_MOVE_READER,
	OP_MOVE_READER_FUN,
	OP_READER_TO_WRITER,
	OP_WRITER_TO_READER,
	OP_READER_OUT,
	OP_READER_CLEAR,
	OP_VAR_CLEAR,
	OP_VAR_OUT,
	OP_VAR_POP,
	OP_POP,
	OP_DEF_VAR_REF,
	OP_DEF_VAR,
	OP_LOAD_VAR,
	OP_VAR_REF,
	OP_VAR_READ,
	OP_VAR_WRITE,
	OP_LOAD_PARAM,
	OP_SET_PARAM,




	OP_OP_MAX
};

int rt_do_call(task* t, st_ptr* app, st_ptr* root, st_ptr* fun, st_ptr* param);
int rt_do_read(st_ptr* out, st_ptr* app, st_ptr root);

/* transaction-writer */
int cle_write(FILE* f, task* t, st_ptr* root, uint clear, uchar infun);
/* "test ""test"" 'test'" | 'test ''test'' "test"' */
int cle_string(FILE* f, task* t, st_ptr* out, int c, int* nxtchar, uchar append);

void cle_num(task* t, st_ptr* out, int num);

/* compiler functions */
int cmp_function(FILE* f, task* t, st_ptr* ref);
int cmp_expr(FILE* f, task* t, st_ptr* ref);

#define whitespace(c) (c == ' ' || c == '\t' || c == '\n' || c == '\r')
#define num(c) (c >= '0' && c <= '9')
#define minusnum(c) (c == '-' || num(c))
#define alpha(c) ((c & 0x80) || (c >= 'a' && c <= 'z')  || (c >= 'A' && c <= 'Z') || (c == '_'))
#define alphanum(c) (alpha(c) || num(c))

#endif
