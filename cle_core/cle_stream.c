/* 
 Clerk application and storage engine.
 Copyright (C) 2013  Lars Szuwalski

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "cle_stream.h"
#include "cle_object.h"

#include <string.h>
/*
 *	The main input-interface to the running system
 *	Events/messages are "pumped" in through the exported set of functions
 */

struct task_common {
	cle_pipe_inst response;
	cle_instance inst;
	st_ptr event_name;
	st_ptr user_roles;
	st_ptr userid;
	ptr_list* free;
	ptr_list* out;
	_ipt* ipt;
	st_ptr top;
};

struct handler_node {
	struct handler_node* next;
	struct task_common* cmn;
	cle_pipe_inst handler;
	st_ptr event_rest;
	uint flags;
	oid oid;
};

struct _syshandler {
	struct _syshandler* next_handler;
	const cle_pipe* handler;
	enum handler_type systype;
};

struct _scanner_ctx {
	cle_instance inst;
	struct handler_node* hdltypes[PIPELINE_RESPONSE + 1];
	st_ptr event_name_base;
	st_ptr event_name;
	st_ptr user_roles;
	st_ptr userid;
	st_ptr evt;
	st_ptr sys;
	uint allowed;
	uint state;
};

// ok node begin

static state _ok_start(void* v) {
	return OK;
}
static state _ok_next(void* v) {
	return OK;
}
static state _ok_end(void* v, cdat c, uint l) {
	return OK;
}
static state _ok_pop(void* v) {
	return OK;
}
static state _ok_push(void* v) {
	return OK;
}
static state _ok_data(void* v, cdat c, uint l) {
	return OK;
}

static const cle_pipe _ok_node = { _ok_start, _ok_next, _ok_end, _ok_pop, _ok_push, _ok_data, 0 };

// copy node begin
static const cle_pipe _copy_node = { _ok_start, resp_next, _ok_end, resp_pop, resp_push, resp_data, 0 };

// response node begin
static state _cn_start(void* v) {
	struct handler_node* h = (struct handler_node*) v;
	return h->cmn->response.pipe->start(h->cmn->response.data);
}

static state _cn_next(void* v) {
	struct handler_node* h = (struct handler_node*) v;
	return h->cmn->response.pipe->next(h->cmn->response.data);
}

static state _cn_pop(void* v) {
	struct handler_node* h = (struct handler_node*) v;
	return h->cmn->response.pipe->pop(h->cmn->response.data);
}

static state _cn_push(void* v) {
	struct handler_node* h = (struct handler_node*) v;
	return h->cmn->response.pipe->push(h->cmn->response.data);
}

static state _cn_data(void* v, cdat c, uint l) {
	struct handler_node* h = (struct handler_node*) v;
	return h->cmn->response.pipe->data(h->cmn->response.data, c, l);
}

static state _cn_end(void* v, cdat c, uint l) {
	struct handler_node* h = (struct handler_node*) v;
	return h->cmn->response.pipe->end(h->cmn->response.data, c, l);
}

static const cle_pipe _response_node = { _cn_start, _cn_next, _cn_end, _cn_pop, _cn_push, _cn_data, 0 };

static void _push(struct task_common* cmn) {
	ptr_list* l = cmn->free;
	if (l)
		cmn->free = l->link;
	else
		l = (ptr_list*) tk_alloc(cmn->inst.t, sizeof(ptr_list), 0);

	l->link = cmn->out;
	cmn->out = l;

	l->pt = cmn->top;
}

static struct handler_node* _hnode(struct _scanner_ctx* ctx, const cle_pipe* handler, oid id, enum handler_type type) {
	struct handler_node* hdl;

	if (type == SYNC_REQUEST_HANDLER && ctx->hdltypes[SYNC_REQUEST_HANDLER] != 0)
		hdl = ctx->hdltypes[SYNC_REQUEST_HANDLER];
	else
		hdl = (struct handler_node*) tk_alloc(ctx->inst.t, sizeof(struct handler_node), 0);

	hdl->next = ctx->hdltypes[type];
	ctx->hdltypes[type] = hdl;

	hdl->handler.pipe = handler;
	hdl->event_rest = ctx->event_name;
	hdl->oid = id;
	return hdl;
}

static void _ready_node(struct handler_node* n, struct task_common* cmn) {
	n->handler.data = 0;
	n->flags = 0;
	n->cmn = cmn;
}

static void _reg_handlers(struct _scanner_ctx* ctx, st_ptr pt) {
	if (st_move(ctx->inst.t, &pt, HEAD_LINK, HEAD_SIZE) == 0) {
		it_ptr it;
		it_create(ctx->inst.t, &it, &pt);

		while (it_next(ctx->inst.t, 0, &it, sizeof(oid) + 1)) {
			if (*it.kdata != SYNC_REQUEST_HANDLER || ctx->state == 0)
				_hnode(ctx, &_copy_node, *((oid*) (it.kdata + 1)), *it.kdata);	// TODO object-handler (new-ing)
		}

		it_dispose(ctx->inst.t, &it);
	}

	// trace last matching (single) object (if no obj-ref seen)
	if (ctx->state != 1) {
		st_ptr tpt = pt;
		oid id;
		if (st_move(ctx->inst.t, &tpt, HEAD_OBJECTS, HEAD_SIZE) == 0
				&& st_get(ctx->inst.t, &tpt, (char*) &id, sizeof(oid)) == -1) {
			_hnode(ctx, &_copy_node, id, SYNC_REQUEST_HANDLER);	// TODO object-handler (new-ing)

			ctx->state = 2;
		}
	}
}

static int _reg_objhandler(struct _scanner_ctx* ctx, st_ptr pt, cdat stoid, uint oidlen) {
	oid id = cle_oid_from_cdat(ctx->inst, stoid, oidlen);

	// must be in collection here
	if (st_move(ctx->inst.t, &pt, HEAD_OBJECTS, HEAD_SIZE) || st_move(ctx->inst.t, &pt, (cdat) &id, sizeof(oid)))
		return 1;

	_hnode(ctx, &_copy_node, id, SYNC_REQUEST_HANDLER);	// TODO object-handler
	ctx->state = 1;
	return 0;
}

static void _reg_syshandlers(struct _scanner_ctx* ctx, st_ptr pt) {
	struct _syshandler* syshdl;

	if (pt.pg == 0 || st_move(0, &pt, HEAD_HANDLER, HEAD_SIZE))
		return;

	if (st_get(0, &pt, (char*) &syshdl, sizeof(struct _syshandler*)) != -1)
		cle_panic(ctx->inst.t);

	do {
		if (syshdl->systype != SYNC_REQUEST_HANDLER || ctx->state == 0)
			_hnode(ctx, syshdl->handler, NOOID, syshdl->systype);
		// next in list...
	} while ((syshdl = syshdl->next_handler));
}

static uint _check_access(task* t, st_ptr allow, st_ptr roles) {
	it_ptr aitr, ritr;
	uint ret;

	if (st_move(t, &allow, HEAD_ROLES, HEAD_SIZE))
		return 0;

	it_create(t, &aitr, &allow);
	it_create(t, &ritr, &roles);

	while (1) {
		ret = it_next_eq(t, 0, &aitr, 0);
		if (ret != 1)
			break;

		it_load(t, &ritr, aitr.kdata, aitr.kused);

		ret = it_next_eq(t, 0, &ritr, 0);
		if (ret != 1)
			break;

		it_load(t, &aitr, ritr.kdata, ritr.kused);
	}

	it_dispose(t, &aitr);
	it_dispose(t, &ritr);

	return (ret == 2);
}

static void _check_boundry(struct _scanner_ctx* ctx) {
	_reg_syshandlers(ctx, ctx->sys);

	if (ctx->evt.pg != 0) {
		_reg_handlers(ctx, ctx->evt);

		if (ctx->allowed == 0)
			ctx->allowed = _check_access(ctx->inst.t, ctx->evt, ctx->user_roles);
	}
}

/**
 * static ref path.path
 * object ref path.path.@oid.path.path
 *
 */
static int _scanner(void* p, uchar* buffer, uint len) {
	struct _scanner_ctx* ctx = (struct _scanner_ctx*) p;

	if (buffer[0] == '@') {
		if (ctx->state == 1)
			return 1;

		if (_reg_objhandler(ctx, ctx->evt, buffer + 1, len - 1))
			return 1;
	} else {
		st_insert(ctx->inst.t, &ctx->event_name, buffer, len);

		if (ctx->evt.pg != 0 && st_move(ctx->inst.t, &ctx->evt, buffer, len)) {
			ctx->evt.pg = 0;

			// not found! scan end (or no possible grants)
			if (ctx->allowed == 0)
				return 1;
		}

		if (ctx->sys.pg != 0 && st_move(ctx->inst.t, &ctx->sys, buffer, len))
			ctx->sys.pg = 0;

		if (buffer[len - 1] == 0)
			_check_boundry(ctx);
	}

	return 0;
}

static void _init_scanner(struct _scanner_ctx* ctx, task* parent, st_ptr config, st_ptr user_roles, st_ptr userid) {
	ctx->inst.t = tk_clone_task(parent);
	tk_root_ptr(ctx->inst.t, &ctx->inst.root);

	ctx->evt = ctx->inst.root;
	if (st_move(ctx->inst.t, &ctx->evt, HEAD_NAMES, IHEAD_SIZE))
		ctx->evt.pg = 0;

	ctx->sys = config;

	ctx->userid = userid;
	ctx->user_roles = user_roles;

	st_empty(ctx->inst.t, &ctx->event_name);
	ctx->event_name_base = ctx->event_name;

	memset(ctx->hdltypes, 0, sizeof(ctx->hdltypes));

	ctx->allowed = st_is_empty(&userid);
	ctx->state = 0;
}

static struct task_common* _create_task_common(struct _scanner_ctx* ctx, cle_pipe_inst response) {
	struct task_common* cmn = (struct task_common*) tk_alloc(ctx->inst.t, sizeof(struct task_common), 0);
	cmn->response = response;
	cmn->inst = ctx->inst;

	cmn->event_name = ctx->event_name_base;
	cmn->user_roles = ctx->user_roles;
	cmn->userid = ctx->userid;

	st_empty(cmn->inst.t, &cmn->top);
	_push(cmn);

	return cmn;
}

static _ipt* _setup_handlers(struct _scanner_ctx* ctx, cle_pipe_inst response) {
	struct handler_node* hdl;
	struct task_common* cmn;
	_ipt* ipt = ctx->hdltypes[SYNC_REQUEST_HANDLER];

	if (ctx->allowed == 0 || ipt == 0)
		return 0;

	cmn = _create_task_common(ctx, response);

	ipt->next = ctx->hdltypes[PIPELINE_RESPONSE];
	hdl = ipt;

	// setup response-handler chain
	// in correct order (most specific handler comes first)
	do {
		_ready_node(hdl, cmn);

		hdl = hdl->next;
	} while (hdl != 0);

	hdl = ctx->hdltypes[PIPELINE_REQUEST];
	// setup request-handler chain
	// reverse order (most general handlers comes first)
	while (hdl != 0) {
		struct handler_node* tmp;

		_ready_node(hdl, cmn);

		tmp = hdl->next;
		hdl->next = ipt;
		ipt = hdl;
		hdl = tmp;
	}

	cmn->ipt = ipt;
	return ipt;
}

// input interface
_ipt* cle_open(task* parent, st_ptr config, st_ptr eventid, st_ptr userid, st_ptr user_roles, cle_pipe_inst response) {
	struct _scanner_ctx ctx;
	_ipt* ipt;

	_init_scanner(&ctx, parent, config, user_roles, userid);

	// before anything push response-node as last response-handler
	_hnode(&ctx, &_response_node, NOOID, PIPELINE_RESPONSE);

	_check_boundry(&ctx);

	if (cle_scan_validate(parent, &eventid, _scanner, &ctx)) {
		tk_drop_task(ctx.inst.t);
		return 0;
	}

	if ((ipt = _setup_handlers(&ctx, response)) == 0) {
		tk_drop_task(ctx.inst.t);
		return 0;
	}

	return ipt;
}

static state _need_start_call(struct handler_node* h) {
	state s = OK;
	if (h->flags == 0) {
		h->flags |= 2;
		s = h->handler.pipe->start(h);
	}
	return s;
}

static state _check_end_pipe(struct handler_node* h, state s) {
	cdat msg = (cdat) "";
	uint len = 0;

	if (s == LEAVE) {
		s = h->handler.pipe->end(h, msg, len);
		h->handler.pipe = &_copy_node;
	}

	if (s != OK) {
		if (s == FAILED) {
			h = h->cmn->ipt;
			h->flags |= 1;
			msg = (cdat) "pipe-line";
			len = 10;
		}

		do {
			if (h->handler.pipe->end(h, msg, len) == FAILED)
				s = FAILED;
			h->handler.pipe = &_ok_node;
			h = h->next;
		} while (h);
	}
	return s;
}

static state _check_handler(struct handler_node* h, state (*handler)(void*)) {
	state s = _need_start_call(h);

	if (s == OK)
		s = handler(h);

	return _check_end_pipe(h, s);
}

state cle_close(_ipt* ipt, cdat msg, uint len) {
	state s = (len == 0 && (ipt->flags & 1) == 0) ? DONE : FAILED;

	if (_check_end_pipe(ipt, s) == DONE) {
		tk_commit_task(ipt->cmn->inst.t);
	} else {
		tk_drop_task(ipt->cmn->inst.t);
	}
	return s;
}

state cle_next(_ipt* ipt) {
	return _check_handler(ipt, ipt->handler.pipe->next);
}

state cle_pop(_ipt* ipt) {
	return _check_handler(ipt, ipt->handler.pipe->pop);
}

state cle_push(_ipt* ipt) {
	return _check_handler(ipt, ipt->handler.pipe->push);
}

state cle_data(_ipt* ipt, cdat data, uint len) {
	state s = _need_start_call(ipt);
	if (s == OK)
		s = ipt->handler.pipe->data(ipt, data, len);

	return _check_end_pipe(ipt, s);
}

// response
void cle_handler_get_env(const void* p, struct handler_env* env) {
	struct handler_node* h = (struct handler_node*) p;

	env->event = h->cmn->event_name;
	env->event_rest = h->event_rest;
	env->roles = h->cmn->user_roles;
	env->data = h->handler.data;
	env->user = h->cmn->userid;
	env->inst = h->cmn->inst;
	env->id = h->oid;
}

void cle_handler_set_data(void* p, void* data) {
	struct handler_node* h = (struct handler_node*) p;
	h->handler.data = data;
}
void* cle_handler_get_data(const void* p) {
	struct handler_node* h = (struct handler_node*) p;
	return h->handler.data;
}

state resp_data(void* p, cdat c, uint l) {
	struct handler_node* h = (struct handler_node*) p;
	state s = _need_start_call(h->next);
	if (s == OK)
		s = h->next->handler.pipe->data(h->next, c, l);

	return _check_end_pipe(h->next, s);
}
state resp_next(void* p) {
	struct handler_node* h = (struct handler_node*) p;
	return _check_handler(h->next, h->next->handler.pipe->next);
}
state resp_push(void* p) {
	struct handler_node* h = (struct handler_node*) p;
	return _check_handler(h->next, h->next->handler.pipe->push);
}
state resp_pop(void* p) {
	struct handler_node* h = (struct handler_node*) p;
	return _check_handler(h->next, h->next->handler.pipe->pop);
}
state resp_serialize(void* v, st_ptr pt) {
	struct handler_node* h = (struct handler_node*) v;
	state s = _need_start_call(h);
	if (s == OK)
		s = st_map_st(h->cmn->inst.t, &pt, h->next->handler.pipe->data, h->next->handler.pipe->push, h->next->handler.pipe->pop,
				h->next);

	return _check_end_pipe(h->next, s);
}

// add handler to config
uint cle_config_handler(task* t, st_ptr config, const cle_pipe* handler, enum handler_type type) {
	struct _syshandler *next_hdl = 0, *hdl;

	if (st_insert(t, &config, HEAD_HANDLER, HEAD_SIZE) == 0) {
		st_ptr tmp = config;
		if (st_get(t, &tmp, (char*) &next_hdl, sizeof(struct _syshandler*)) != -1)
			return 1;
	}

	hdl = (struct _syshandler*) tk_alloc(t, sizeof(struct _syshandler), 0);

	hdl->systype = type;
	hdl->handler = handler;
	hdl->next_handler = next_hdl;

	return st_update(t, &config, (cdat) &hdl, sizeof(struct _syshandler*));
}

// basic handler implementation
static state _bh_push(void* v) {
	struct handler_node* h = (struct handler_node*) v;
	_push(h->cmn);
	return OK;
}

static state _bh_pop(void* v) {
	struct handler_node* h = (struct handler_node*) v;
	ptr_list* tmp = h->cmn->out;
	if (tmp->link == 0)
		return FAILED;

	h->cmn->out = tmp->link;

	tk_free_ptr(h->cmn->inst.t, &tmp->pt);

	tmp->link = h->cmn->free;
	h->cmn->free = tmp;

	h->cmn->top = tmp->pt;
	return OK;
}

static state _bh_data(void* v, cdat c, uint l) {
	struct handler_node* h = (struct handler_node*) v;
	return st_append(h->cmn->inst.t, &h->cmn->top, c, l) ? FAILED : OK;
}

static state _bh_next(void* v) {
	struct handler_node* h = (struct handler_node*) v;

	state s = h->handler.pipe->next_ptr(v, h->cmn->out->pt);

	st_empty(h->cmn->inst.t, &h->cmn->top);
	h->cmn->out->pt = h->cmn->top;
	return s;
}

cle_pipe cle_basic_handler(state (*start)(void*), state (*next)(void* p, st_ptr ptr), state (*end)(void* p, cdat msg, uint len)) {
	const cle_pipe p = { start, _bh_next, end, _bh_pop, _bh_push, _bh_data, next };
	return p;
}
