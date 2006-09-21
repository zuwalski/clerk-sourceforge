/* Copyrigth(c) Lars Szuwalski, 2005 */

#ifndef __CLE_RUNTIME_H__
#define __CLE_RUNTIME_H__

#include "cle_clerk.h"

#define ERROR_MAX 20
#define MAX_READ_LEVEL 10

// !!!! match sizeof(struct key) in cle_struct.h
#define SIZE_OF_KEY 8

#define HEAD_SIZE 2
#define HEAD_FUNCTION "\0F"
#define HEAD_TRIGGER "\0T"
#define HEAD_INT "\0I"
#define HEAD_STR "\0S"

#define FUNSPACE_SIZE 5
extern const char* funspace;

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
	OP_READER_OUT,
	OP_READER_CLEAR,
	OP_POP,
	OP_DEF_VAR_REF,
	OP_DEF_VAR,
	OP_VAR_FREE,
	OP_LOAD_VAR,
	OP_VAR_REF,
	OP_VAR_READ,
	OP_VAR_WRITE,
	OP_LOAD_PARAM,
	OP_PARAM_READ,
	OP_PARAM_WRITE,
	OP_SET_PARAM,




	OP_OP_MAX
};

int rt_do_call(task* t, st_ptr* app, st_ptr* root, st_ptr* fun, st_ptr* param);
int rt_do_read(st_ptr* out, st_ptr* app, st_ptr root);

#endif
