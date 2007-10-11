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

enum cle_opcode
{
	OP_ILLEGAL = 0,
	OP_NOOP,
	OP_FREE,
	OP_STR,
	OP_CALL,
	OP_SETP,
	OP_DOCALL,
	OP_DOCALL_N,
	OP_AVAR,
	OP_AVARS,
	OP_OVARS,
	OP_LVAR,
	OP_POP,
	OP_POPW,
	OP_WIDX,
	OP_WVAR,
	OP_WVAR0,
	OP_DMVW,
	OP_MVW,
	OP_OUT,
	OP_OUTL,
	OP_OUTLT,
	OP_CONF,
	OP_FUN,
	OP_RIDX,
	OP_RVAR,
	OP_MV,
	OP_DEFP,
	OP_BODY,
	OP_END,
	OP_ADD,
	OP_SUB,
	OP_MUL,
	OP_DIV,
	OP_REM,
	OP_IMM,
	OP_BNZ,
	OP_BZ,
	OP_BR,
	OP_GE,
	OP_NE,
	OP_GT,
	OP_LE,
	OP_LT,
	OP_EQ,
	OP_LOOP,
	OP_CAV,
	OP_NULL,
	OP_CLEAR,
	OP_ERROR,
	OP_CAT,
	OP_CMV,
	OP_FMV,
	OP_NOT,
	OP_DEBUG,

	OP_OP_MAX
};

struct _body_
{
	char body;
	uchar maxparams;
	uchar maxvars;
	uchar maxstack;
	ushort codesize;
	ushort firsthandler;
};

int rt_do_call(task* t, st_ptr* app, st_ptr* root, st_ptr* fun, st_ptr* param);

#define whitespace(c) (c == ' ' || c == '\t' || c == '\n' || c == '\r')
#define num(c) (c >= '0' && c <= '9')
#define minusnum(c) (c == '-' || num(c))
#define alpha(c) ((c & 0x80) || (c >= 'a' && c <= 'z')  || (c >= 'A' && c <= 'Z') || (c == '_'))
#define alphanum(c) (alpha(c) || num(c))

#endif
