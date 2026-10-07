// Copyright 2026 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Duktape API compatibility layer on top of QuickJS (ES2023) for NetSurf.

#include <assert.h>
#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "duktape.h"
#include <quickjs.h>

#define DUK_QJS_INITIAL_STACK_CAP 256
#define DUK_QJS_CSTR_RING_SIZE 64
#define DUK_QJS_FINALIZER_PROP "\x01\x01_Finalizer"
#define DUK_QJS_CFUNC_PROP "\x01\x01_CFunction"
#define DUK_QJS_PRIVATE_PROP "\x01\x01NETSURF_DUKTAPE_PRIVATE"
#define DUK_QJS_RAW_PRIVATE_PROP "\xFF\xFFNETSURF_DUKTAPE_PRIVATE"

extern duk_bool_t dukky_check_timeout(void *udata);
extern duk_ret_t dukky_create_prototypes(duk_context *ctx);

struct duk_qjs_catch_point {
	jmp_buf jb;
	struct duk_qjs_catch_point *prev;
};

struct duk_qjs_heap {
	JSRuntime *rt;
	duk_alloc_function alloc_func;
	duk_realloc_function realloc_func;
	duk_free_function free_func;
	void *heap_udata;
	duk_fatal_function fatal_handler;
	duk_context *main_ctx;
	duk_context *thread_list;
};

struct duk_hthread {
	struct duk_qjs_heap *heap;
	JSContext *qctx;
	JSValue global_obj;
	JSValue current_this;
	JSValue thrown_val;
	bool has_thrown;
	bool is_destroying;

	JSValue *stack;
	duk_idx_t top;
	duk_idx_t cap;
	duk_idx_t frame_base;

	const char *cstr_ring[DUK_QJS_CSTR_RING_SIZE];
	size_t cstr_ring_idx;

	struct duk_qjs_catch_point *catch_top;

	void *finalizing_private_ptr;
	duk_c_function global_finalizer;
	void *global_private_ptr;

	duk_context *next_thread;
};

struct duk_qjs_obj_meta {
	duk_context *ctx;
	duk_c_function finalizer;
	void *private_ptr;
};

static JSClassID g_duk_object_class_id = 0;
static JSClassID g_duk_pointer_class_id = 0;
static JSClassID g_duk_thread_class_id = 0;

static void duk_qjs_run_finalizer_with_ptr(duk_context *ctx,
					   duk_c_function finalizer,
					   void *private_ptr)
{
	if (ctx == NULL || finalizer == NULL || private_ptr == NULL)
		return;

	duk_idx_t saved_base = ctx->frame_base;
	duk_idx_t saved_top = ctx->top;
	void *saved_fin_ptr = ctx->finalizing_private_ptr;
	struct duk_qjs_catch_point cp;

	ctx->frame_base = saved_top;
	ctx->finalizing_private_ptr = private_ptr;
	duk_push_undefined(ctx);

	cp.prev = ctx->catch_top;
	ctx->catch_top = &cp;
	if (setjmp(cp.jb) == 0) {
		(void)finalizer(ctx);
	} else if (ctx->has_thrown) {
		JS_FreeValue(ctx->qctx, ctx->thrown_val);
		ctx->thrown_val = JS_UNDEFINED;
		ctx->has_thrown = false;
	}
	ctx->catch_top = cp.prev;

	while (ctx->top > saved_top) {
		ctx->top--;
		JS_FreeValue(ctx->qctx, ctx->stack[ctx->top]);
	}
	ctx->frame_base = saved_base;
	ctx->finalizing_private_ptr = saved_fin_ptr;
}

static void duk_qjs_object_finalizer(JSRuntime *rt, JSValue val)
{
	(void)rt;
	struct duk_qjs_obj_meta *meta =
		(struct duk_qjs_obj_meta *)JS_GetOpaque(val, g_duk_object_class_id);
	if (meta == NULL)
		return;

	if (meta->finalizer != NULL && meta->private_ptr != NULL &&
	    meta->ctx != NULL && !meta->ctx->is_destroying) {
		duk_qjs_run_finalizer_with_ptr(meta->ctx, meta->finalizer,
					       meta->private_ptr);
	}
	meta->private_ptr = NULL;
	free(meta);
}

static void duk_qjs_ensure_classes(JSRuntime *rt)
{
	if (g_duk_object_class_id == 0) {
		JS_NewClassID(&g_duk_object_class_id);
		JS_NewClassID(&g_duk_pointer_class_id);
		JS_NewClassID(&g_duk_thread_class_id);
	}
	if (!JS_IsRegisteredClass(rt, g_duk_object_class_id)) {
		JSClassDef obj_def = {
			.class_name = "DukObject",
			.finalizer = duk_qjs_object_finalizer,
		};
		JS_NewClass(rt, g_duk_object_class_id, &obj_def);
	}
	if (!JS_IsRegisteredClass(rt, g_duk_pointer_class_id)) {
		JSClassDef ptr_def = {
			.class_name = "DukPointer",
		};
		JS_NewClass(rt, g_duk_pointer_class_id, &ptr_def);
	}
	if (!JS_IsRegisteredClass(rt, g_duk_thread_class_id)) {
		JSClassDef thr_def = {
			.class_name = "DukThread",
		};
		JS_NewClass(rt, g_duk_thread_class_id, &thr_def);
	}
}

static int duk_qjs_interrupt_handler(JSRuntime *rt, void *opaque)
{
	(void)rt;
	if (opaque == NULL)
		return 0;
	return dukky_check_timeout(opaque) ? 1 : 0;
}

static void duk_qjs_ensure_stack(duk_context *ctx, duk_idx_t extra)
{
	if (extra <= 0)
		return;
	duk_idx_t needed = ctx->top + extra;
	if (needed <= ctx->cap)
		return;
	duk_idx_t new_cap = ctx->cap ? ctx->cap : DUK_QJS_INITIAL_STACK_CAP;
	while (new_cap < needed)
		new_cap *= 2;
	JSValue *new_stack =
		(JSValue *)realloc(ctx->stack, (size_t)new_cap * sizeof(JSValue));
	assert(new_stack != NULL);
	ctx->stack = new_stack;
	ctx->cap = new_cap;
}

static void duk_qjs_push_val(duk_context *ctx, JSValue val)
{
	duk_qjs_ensure_stack(ctx, 1);
	ctx->stack[ctx->top++] = val;
}

static duk_idx_t duk_qjs_abs_idx(duk_context *ctx, duk_idx_t idx)
{
	duk_idx_t frame_len = ctx->top - ctx->frame_base;
	if (idx >= 0) {
		if (idx >= frame_len)
			return -1;
		return ctx->frame_base + idx;
	} else {
		duk_idx_t rel = frame_len + idx;
		if (rel < 0 || rel >= frame_len)
			return -1;
		return ctx->frame_base + rel;
	}
}

static const char *duk_qjs_cache_cstr(duk_context *ctx, const char *cstr)
{
	if (cstr == NULL)
		return NULL;
	size_t slot = ctx->cstr_ring_idx;
	ctx->cstr_ring_idx = (slot + 1) % DUK_QJS_CSTR_RING_SIZE;
	if (ctx->cstr_ring[slot] != NULL)
		JS_FreeCString(ctx->qctx, ctx->cstr_ring[slot]);
	ctx->cstr_ring[slot] = cstr;
	return cstr;
}

static char *duk_qjs_normalize_key(const char *str, size_t len, bool *is_hidden)
{
	if (is_hidden != NULL)
		*is_hidden = false;
	char *buf = (char *)malloc(len + 1);
	if (buf == NULL)
		return NULL;
	memcpy(buf, str, len);
	buf[len] = '\0';
	size_t i = 0;
	while (i < len && (uint8_t)buf[i] == 0xFF) {
		buf[i] = '\x01';
		if (is_hidden != NULL)
			*is_hidden = true;
		i++;
	}
	return buf;
}

static JSAtom duk_qjs_key_to_atom(duk_context *ctx, const char *key,
				  size_t len, bool *is_hidden)
{
	if (key == NULL) {
		if (is_hidden != NULL)
			*is_hidden = false;
		return JS_NewAtomLen(ctx->qctx, "", 0);
	}
	char *norm = duk_qjs_normalize_key(key, len, is_hidden);
	if (norm == NULL)
		return JS_ATOM_NULL;
	JSAtom atom = JS_NewAtomLen(ctx->qctx, norm, len);
	free(norm);
	return atom;
}

static JSAtom duk_qjs_val_to_atom(duk_context *ctx, JSValueConst val,
				  bool *is_hidden)
{
	if (is_hidden != NULL)
		*is_hidden = false;
	if (JS_IsObject(val) && JS_GetClassID(val) == g_duk_pointer_class_id) {
		char ptr_buf[48];
		void *ptr = JS_GetOpaque(val, g_duk_pointer_class_id);
		int n = snprintf(ptr_buf, sizeof(ptr_buf), "\x01\x02ptr_%p", ptr);
		if (is_hidden != NULL)
			*is_hidden = true;
		return JS_NewAtomLen(ctx->qctx, ptr_buf, (size_t)n);
	}
	if (JS_IsString(val)) {
		size_t len = 0;
		const char *cstr = JS_ToCStringLen(ctx->qctx, &len, val);
		if (cstr != NULL) {
			if (len > 0 && (uint8_t)cstr[0] == 0x01 && is_hidden != NULL)
				*is_hidden = true;
			JSAtom atom = JS_NewAtomLen(ctx->qctx, cstr, len);
			JS_FreeCString(ctx->qctx, cstr);
			return atom;
		}
	}
	return JS_ValueToAtom(ctx->qctx, val);
}

static struct duk_qjs_obj_meta *duk_qjs_get_or_create_meta(duk_context *ctx,
							   JSValueConst obj)
{
	if (!JS_IsObject(obj) || JS_GetClassID(obj) != g_duk_object_class_id)
		return NULL;
	struct duk_qjs_obj_meta *meta =
		(struct duk_qjs_obj_meta *)JS_GetOpaque(obj, g_duk_object_class_id);
	if (meta == NULL) {
		meta = (struct duk_qjs_obj_meta *)calloc(1, sizeof(*meta));
		if (meta != NULL) {
			meta->ctx = ctx;
			JS_SetOpaque(obj, meta);
		}
	}
	return meta;
}

static void duk_qjs_init_context(duk_context *ctx, struct duk_qjs_heap *heap)
{
	ctx->heap = heap;
	ctx->qctx = JS_NewContext(heap->rt);
	JS_SetContextOpaque(ctx->qctx, ctx);

	JSValue tmp_obj = JS_NewObject(ctx->qctx);
	JSValue obj_proto = JS_GetPrototype(ctx->qctx, tmp_obj);
	JS_FreeValue(ctx->qctx, tmp_obj);
	JS_SetClassProto(ctx->qctx, g_duk_object_class_id, obj_proto);
	JS_SetClassProto(ctx->qctx, g_duk_pointer_class_id, JS_NULL);
	JS_SetClassProto(ctx->qctx, g_duk_thread_class_id, JS_NULL);

	ctx->global_obj = JS_GetGlobalObject(ctx->qctx);
	ctx->current_this = JS_UNDEFINED;
	ctx->thrown_val = JS_UNDEFINED;
	ctx->has_thrown = false;
	ctx->is_destroying = false;
	ctx->top = 0;
	ctx->cap = 0;
	ctx->frame_base = 0;
	ctx->stack = NULL;
	duk_qjs_ensure_stack(ctx, DUK_QJS_INITIAL_STACK_CAP);
}

static void duk_qjs_cleanup_context(duk_context *ctx)
{
	if (ctx == NULL || ctx->is_destroying)
		return;
	ctx->is_destroying = true;

	if (ctx->global_finalizer != NULL && ctx->global_private_ptr != NULL) {
		duk_c_function fin = ctx->global_finalizer;
		void *priv = ctx->global_private_ptr;
		ctx->global_finalizer = NULL;
		ctx->global_private_ptr = NULL;
		ctx->is_destroying = false;
		duk_qjs_run_finalizer_with_ptr(ctx, fin, priv);
		ctx->is_destroying = true;
	}

	for (size_t i = 0; i < DUK_QJS_CSTR_RING_SIZE; i++) {
		if (ctx->cstr_ring[i] != NULL) {
			JS_FreeCString(ctx->qctx, ctx->cstr_ring[i]);
			ctx->cstr_ring[i] = NULL;
		}
	}

	while (ctx->top > 0) {
		ctx->top--;
		JS_FreeValue(ctx->qctx, ctx->stack[ctx->top]);
	}
	free(ctx->stack);
	ctx->stack = NULL;

	if (ctx->has_thrown) {
		JS_FreeValue(ctx->qctx, ctx->thrown_val);
		ctx->thrown_val = JS_UNDEFINED;
		ctx->has_thrown = false;
	}
	JS_FreeValue(ctx->qctx, ctx->current_this);
	ctx->current_this = JS_UNDEFINED;
	JS_FreeValue(ctx->qctx, ctx->global_obj);
	ctx->global_obj = JS_UNDEFINED;

	JS_FreeContext(ctx->qctx);
	ctx->qctx = NULL;
}

/*
 *  Context and Heap Management
 */

DUK_EXTERNAL duk_context *duk_create_heap(duk_alloc_function alloc_func,
					  duk_realloc_function realloc_func,
					  duk_free_function free_func,
					  void *heap_udata,
					  duk_fatal_function fatal_handler)
{
	struct duk_qjs_heap *heap =
		(struct duk_qjs_heap *)calloc(1, sizeof(*heap));
	if (heap == NULL)
		return NULL;

	heap->rt = JS_NewRuntime();
	if (heap->rt == NULL) {
		free(heap);
		return NULL;
	}
	JS_SetMaxStackSize(heap->rt, 0);
	JS_SetInterruptHandler(heap->rt, duk_qjs_interrupt_handler, heap_udata);
	duk_qjs_ensure_classes(heap->rt);

	heap->alloc_func = alloc_func;
	heap->realloc_func = realloc_func;
	heap->free_func = free_func;
	heap->heap_udata = heap_udata;
	heap->fatal_handler = fatal_handler;

	duk_context *ctx = (duk_context *)calloc(1, sizeof(*ctx));
	if (ctx == NULL) {
		JS_FreeRuntime(heap->rt);
		free(heap);
		return NULL;
	}
	duk_qjs_init_context(ctx, heap);
	heap->main_ctx = ctx;
	return ctx;
}

DUK_EXTERNAL void duk_destroy_heap(duk_context *ctx)
{
	if (ctx == NULL)
		return;
	struct duk_qjs_heap *heap = ctx->heap;

	duk_context *thr = heap->thread_list;
	while (thr != NULL) {
		duk_context *next = thr->next_thread;
		duk_qjs_cleanup_context(thr);
		free(thr);
		thr = next;
	}
	heap->thread_list = NULL;

	duk_qjs_cleanup_context(heap->main_ctx);
	free(heap->main_ctx);
	heap->main_ctx = NULL;

	JS_FreeRuntime(heap->rt);
	free(heap);
}

DUK_EXTERNAL void duk_get_memory_functions(duk_context *ctx,
					   duk_memory_functions *out_funcs)
{
	if (out_funcs == NULL || ctx == NULL)
		return;
	out_funcs->alloc_func = ctx->heap->alloc_func;
	out_funcs->realloc_func = ctx->heap->realloc_func;
	out_funcs->free_func = ctx->heap->free_func;
	out_funcs->udata = ctx->heap->heap_udata;
}

DUK_EXTERNAL void duk_gc(duk_context *ctx, duk_uint_t flags)
{
	(void)flags;
	if (ctx != NULL && ctx->heap != NULL && ctx->heap->rt != NULL)
		JS_RunGC(ctx->heap->rt);
}

DUK_EXTERNAL duk_idx_t duk_push_thread_raw(duk_context *ctx, duk_uint_t flags)
{
	(void)flags;
	duk_context *thr = (duk_context *)calloc(1, sizeof(*thr));
	assert(thr != NULL);
	duk_qjs_init_context(thr, ctx->heap);
	thr->next_thread = ctx->heap->thread_list;
	ctx->heap->thread_list = thr;

	/* Initialize a per-thread prototype table before duk_set_global_object */
	JSValue real_glob = thr->global_obj;
	thr->global_obj = JS_NewObjectClass(thr->qctx, g_duk_object_class_id);
	JS_SetPrototype(thr->qctx, thr->global_obj, real_glob);
	JS_FreeValue(thr->qctx, real_glob);

	duk_push_global_object(thr);
	duk_push_boolean(thr, 1);
	duk_put_prop_string(thr, -2, "protos");
	duk_put_global_string(thr, "\xFF\xFFNETSURF_DUKTAPE_PROTOTYPES");
	dukky_create_prototypes(thr);

	JSValue thr_obj = JS_NewObjectClass(ctx->qctx, g_duk_thread_class_id);
	JS_SetOpaque(thr_obj, thr);
	duk_qjs_push_val(ctx, thr_obj);
	return (ctx->top - 1) - ctx->frame_base;
}

DUK_EXTERNAL duk_context *duk_get_context(duk_context *ctx, duk_idx_t idx)
{
	duk_idx_t abs_idx = duk_qjs_abs_idx(ctx, idx);
	if (abs_idx < 0)
		return NULL;
	JSValue val = ctx->stack[abs_idx];
	if (!JS_IsObject(val) || JS_GetClassID(val) != g_duk_thread_class_id)
		return NULL;
	return (duk_context *)JS_GetOpaque(val, g_duk_thread_class_id);
}

DUK_EXTERNAL duk_context *duk_require_context(duk_context *ctx, duk_idx_t idx)
{
	duk_context *res = duk_get_context(ctx, idx);
	if (res == NULL)
		duk_error_raw(ctx, DUK_ERR_TYPE_ERROR, __FILE__, __LINE__,
			      "context required");
	return res;
}

DUK_EXTERNAL void duk_set_global_object(duk_context *ctx)
{
	duk_idx_t abs_idx = duk_qjs_abs_idx(ctx, -1);
	if (abs_idx < 0)
		return;
	JSValue new_glob = ctx->stack[abs_idx];
	JSValue real_glob = JS_GetGlobalObject(ctx->qctx);

	if (JS_IsObject(new_glob)) {
		JSPropertyEnum *tab = NULL;
		uint32_t len = 0;
		if (JS_GetOwnPropertyNames(ctx->qctx, &tab, &len, new_glob,
					   JS_GPN_STRING_MASK |
					   JS_GPN_SYMBOL_MASK |
					   JS_GPN_SET_ENUM) == 0) {
			for (uint32_t i = 0; i < len; i++) {
				JSValue prop_val =
					JS_GetProperty(ctx->qctx, new_glob,
						       tab[i].atom);
				int prop_flags = JS_PROP_WRITABLE |
						 JS_PROP_CONFIGURABLE |
						 (tab[i].is_enumerable
							  ? JS_PROP_ENUMERABLE
							  : 0);
				JS_DefinePropertyValue(ctx->qctx, real_glob,
						       tab[i].atom, prop_val,
						       prop_flags);
			}
			JS_FreePropertyEnum(ctx->qctx, tab, len);
		}

		JSValue proto = JS_GetPrototype(ctx->qctx, new_glob);
		if (JS_IsObject(proto) || JS_IsNull(proto))
			JS_SetPrototype(ctx->qctx, real_glob, proto);
		JS_FreeValue(ctx->qctx, proto);

		if (JS_GetClassID(new_glob) == g_duk_object_class_id) {
			struct duk_qjs_obj_meta *meta =
				(struct duk_qjs_obj_meta *)JS_GetOpaque(
					new_glob, g_duk_object_class_id);
			if (meta != NULL) {
				ctx->global_finalizer = meta->finalizer;
				ctx->global_private_ptr = meta->private_ptr;
				meta->finalizer = NULL;
				meta->private_ptr = NULL;
			}
		}
	}

	if (!JS_SameValue(ctx->qctx, ctx->global_obj, real_glob) &&
	    JS_IsObject(ctx->global_obj)) {
		JS_SetPrototype(ctx->qctx, ctx->global_obj, JS_NULL);
	}
	JS_FreeValue(ctx->qctx, ctx->global_obj);
	ctx->global_obj = real_glob;
	duk_pop(ctx);
}

DUK_EXTERNAL void duk_push_global_object(duk_context *ctx)
{
	duk_qjs_push_val(ctx, JS_DupValue(ctx->qctx, ctx->global_obj));
}

DUK_EXTERNAL void duk_push_this(duk_context *ctx)
{
	if (JS_IsUndefined(ctx->current_this) || JS_IsNull(ctx->current_this)) {
		duk_qjs_push_val(ctx, JS_DupValue(ctx->qctx, ctx->global_obj));
	} else {
		duk_qjs_push_val(ctx, JS_DupValue(ctx->qctx, ctx->current_this));
	}
}

DUK_EXTERNAL void duk_push_context_dump(duk_context *ctx)
{
	char buf[128];
	snprintf(buf, sizeof(buf), "ctx: top=%d", (int)duk_get_top(ctx));
	duk_push_string(ctx, buf);
}

/*
 *  Error Handling
 */

static JSValue duk_qjs_make_error_va(duk_context *ctx, duk_errcode_t err_code,
				     const char *filename, duk_int_t line,
				     const char *fmt, va_list ap)
{
	char msg[512];
	if (fmt != NULL) {
		vsnprintf(msg, sizeof(msg), fmt, ap);
	} else {
		snprintf(msg, sizeof(msg), "error %d", (int)err_code);
	}

	const char *ctor_name = "Error";
	switch (err_code) {
	case DUK_ERR_EVAL_ERROR:
		ctor_name = "EvalError";
		break;
	case DUK_ERR_RANGE_ERROR:
		ctor_name = "RangeError";
		break;
	case DUK_ERR_REFERENCE_ERROR:
		ctor_name = "ReferenceError";
		break;
	case DUK_ERR_SYNTAX_ERROR:
		ctor_name = "SyntaxError";
		break;
	case DUK_ERR_TYPE_ERROR:
		ctor_name = "TypeError";
		break;
	case DUK_ERR_URI_ERROR:
		ctor_name = "URIError";
		break;
	default:
		ctor_name = "Error";
		break;
	}

	JSValue err_obj = JS_NewError(ctx->qctx);
	JS_DefinePropertyValueStr(ctx->qctx, err_obj, "name",
				  JS_NewString(ctx->qctx, ctor_name),
				  JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
	JS_DefinePropertyValueStr(ctx->qctx, err_obj, "message",
				  JS_NewString(ctx->qctx, msg),
				  JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
	if (filename != NULL) {
		JS_DefinePropertyValueStr(ctx->qctx, err_obj, "fileName",
					  JS_NewString(ctx->qctx, filename),
					  JS_PROP_WRITABLE |
						  JS_PROP_CONFIGURABLE);
	}
	JS_DefinePropertyValueStr(ctx->qctx, err_obj, "lineNumber",
				  JS_NewInt32(ctx->qctx, line),
				  JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
	return err_obj;
}

DUK_EXTERNAL void duk_throw_raw(duk_context *ctx)
{
	JSValue err_val = JS_UNDEFINED;
	if (ctx->top > ctx->frame_base) {
		ctx->top--;
		err_val = ctx->stack[ctx->top];
	}
	if (ctx->has_thrown)
		JS_FreeValue(ctx->qctx, ctx->thrown_val);
	ctx->thrown_val = err_val;
	ctx->has_thrown = true;

	if (ctx->catch_top != NULL)
		longjmp(ctx->catch_top->jb, 1);

	abort();
}

DUK_EXTERNAL void duk_fatal_raw(duk_context *ctx, const char *err_msg)
{
	if (ctx != NULL && ctx->heap != NULL &&
	    ctx->heap->fatal_handler != NULL) {
		ctx->heap->fatal_handler(ctx->heap->heap_udata, err_msg);
	}
	abort();
}

DUK_EXTERNAL void duk_error_va_raw(duk_context *ctx, duk_errcode_t err_code,
				   const char *filename, duk_int_t line,
				   const char *fmt, va_list ap)
{
	JSValue err_obj =
		duk_qjs_make_error_va(ctx, err_code, filename, line, fmt, ap);
	duk_qjs_push_val(ctx, err_obj);
	duk_throw_raw(ctx);
}

DUK_EXTERNAL void duk_error_raw(duk_context *ctx, duk_errcode_t err_code,
				const char *filename, duk_int_t line,
				const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	duk_error_va_raw(ctx, err_code, filename, line, fmt, ap);
	va_end(ap);
}

/*
 *  Stack Management
 */

DUK_EXTERNAL duk_idx_t duk_get_top(duk_context *ctx)
{
	return ctx->top - ctx->frame_base;
}

DUK_EXTERNAL duk_idx_t duk_get_top_index(duk_context *ctx)
{
	duk_idx_t top = duk_get_top(ctx);
	return top > 0 ? top - 1 : DUK_INVALID_INDEX;
}

DUK_EXTERNAL duk_idx_t duk_require_top_index(duk_context *ctx)
{
	duk_idx_t top = duk_get_top(ctx);
	if (top <= 0)
		duk_error_raw(ctx, DUK_ERR_TYPE_ERROR, __FILE__, __LINE__,
			      "empty stack");
	return top - 1;
}

DUK_EXTERNAL void duk_set_top(duk_context *ctx, duk_idx_t idx)
{
	duk_idx_t frame_len = ctx->top - ctx->frame_base;
	duk_idx_t target_len;
	if (idx >= 0) {
		target_len = idx;
	} else {
		target_len = frame_len + idx;
	}
	if (target_len < 0)
		target_len = 0;

	duk_idx_t target_top = ctx->frame_base + target_len;
	if (target_top < ctx->top) {
		while (ctx->top > target_top) {
			ctx->top--;
			JS_FreeValue(ctx->qctx, ctx->stack[ctx->top]);
		}
	} else if (target_top > ctx->top) {
		duk_qjs_ensure_stack(ctx, target_top - ctx->top);
		while (ctx->top < target_top)
			ctx->stack[ctx->top++] = JS_UNDEFINED;
	}
}

DUK_EXTERNAL duk_idx_t duk_normalize_index(duk_context *ctx, duk_idx_t idx)
{
	duk_idx_t abs_idx = duk_qjs_abs_idx(ctx, idx);
	if (abs_idx < 0)
		return DUK_INVALID_INDEX;
	return abs_idx - ctx->frame_base;
}

DUK_EXTERNAL duk_idx_t duk_require_normalize_index(duk_context *ctx,
						   duk_idx_t idx)
{
	duk_idx_t norm = duk_normalize_index(ctx, idx);
	if (norm == DUK_INVALID_INDEX)
		duk_error_raw(ctx, DUK_ERR_RANGE_ERROR, __FILE__, __LINE__,
			      "invalid stack index %d", (int)idx);
	return norm;
}

DUK_EXTERNAL duk_bool_t duk_is_valid_index(duk_context *ctx, duk_idx_t idx)
{
	return duk_qjs_abs_idx(ctx, idx) >= 0 ? 1 : 0;
}

DUK_EXTERNAL void duk_require_valid_index(duk_context *ctx, duk_idx_t idx)
{
	(void)duk_require_normalize_index(ctx, idx);
}

DUK_EXTERNAL duk_bool_t duk_check_stack(duk_context *ctx, duk_idx_t extra)
{
	duk_qjs_ensure_stack(ctx, extra);
	return 1;
}

DUK_EXTERNAL void duk_require_stack(duk_context *ctx, duk_idx_t extra)
{
	duk_qjs_ensure_stack(ctx, extra);
}

DUK_EXTERNAL duk_bool_t duk_check_stack_top(duk_context *ctx, duk_idx_t top)
{
	duk_idx_t cur = duk_get_top(ctx);
	if (top > cur)
		duk_qjs_ensure_stack(ctx, top - cur);
	return 1;
}

DUK_EXTERNAL void duk_require_stack_top(duk_context *ctx, duk_idx_t top)
{
	(void)duk_check_stack_top(ctx, top);
}

DUK_EXTERNAL void duk_swap(duk_context *ctx, duk_idx_t idx1, duk_idx_t idx2)
{
	duk_idx_t a1 = duk_qjs_abs_idx(ctx, idx1);
	duk_idx_t a2 = duk_qjs_abs_idx(ctx, idx2);
	if (a1 < 0 || a2 < 0 || a1 == a2)
		return;
	JSValue tmp = ctx->stack[a1];
	ctx->stack[a1] = ctx->stack[a2];
	ctx->stack[a2] = tmp;
}

DUK_EXTERNAL void duk_swap_top(duk_context *ctx, duk_idx_t idx)
{
	duk_swap(ctx, idx, -1);
}

DUK_EXTERNAL void duk_dup(duk_context *ctx, duk_idx_t from_idx)
{
	duk_idx_t abs_idx = duk_qjs_abs_idx(ctx, from_idx);
	if (abs_idx < 0) {
		duk_push_undefined(ctx);
		return;
	}
	duk_qjs_push_val(ctx, JS_DupValue(ctx->qctx, ctx->stack[abs_idx]));
}

DUK_EXTERNAL void duk_dup_top(duk_context *ctx)
{
	duk_dup(ctx, -1);
}

DUK_EXTERNAL void duk_insert(duk_context *ctx, duk_idx_t to_idx)
{
	duk_idx_t abs_to = duk_qjs_abs_idx(ctx, to_idx);
	if (abs_to < 0 || ctx->top <= ctx->frame_base || abs_to >= ctx->top - 1)
		return;
	JSValue top_val = ctx->stack[ctx->top - 1];
	memmove(&ctx->stack[abs_to + 1], &ctx->stack[abs_to],
		(size_t)(ctx->top - 1 - abs_to) * sizeof(JSValue));
	ctx->stack[abs_to] = top_val;
}

DUK_EXTERNAL void duk_pull(duk_context *ctx, duk_idx_t from_idx)
{
	duk_idx_t abs_from = duk_qjs_abs_idx(ctx, from_idx);
	if (abs_from < 0 || abs_from >= ctx->top - 1)
		return;
	JSValue val = ctx->stack[abs_from];
	memmove(&ctx->stack[abs_from], &ctx->stack[abs_from + 1],
		(size_t)(ctx->top - 1 - abs_from) * sizeof(JSValue));
	ctx->stack[ctx->top - 1] = val;
}

DUK_EXTERNAL void duk_replace(duk_context *ctx, duk_idx_t to_idx)
{
	if (ctx->top <= ctx->frame_base)
		return;
	duk_idx_t abs_to = duk_qjs_abs_idx(ctx, to_idx);
	ctx->top--;
	JSValue top_val = ctx->stack[ctx->top];
	if (abs_to < 0 || abs_to >= ctx->top) {
		JS_FreeValue(ctx->qctx, top_val);
		return;
	}
	JS_FreeValue(ctx->qctx, ctx->stack[abs_to]);
	ctx->stack[abs_to] = top_val;
}

DUK_EXTERNAL void duk_copy(duk_context *ctx, duk_idx_t from_idx,
			   duk_idx_t to_idx)
{
	duk_idx_t abs_from = duk_qjs_abs_idx(ctx, from_idx);
	duk_idx_t abs_to = duk_qjs_abs_idx(ctx, to_idx);
	if (abs_from < 0 || abs_to < 0 || abs_from == abs_to)
		return;
	JSValue dup = JS_DupValue(ctx->qctx, ctx->stack[abs_from]);
	JS_FreeValue(ctx->qctx, ctx->stack[abs_to]);
	ctx->stack[abs_to] = dup;
}

DUK_EXTERNAL void duk_remove(duk_context *ctx, duk_idx_t idx)
{
	duk_idx_t abs_idx = duk_qjs_abs_idx(ctx, idx);
	if (abs_idx < 0)
		return;
	JS_FreeValue(ctx->qctx, ctx->stack[abs_idx]);
	if (abs_idx < ctx->top - 1) {
		memmove(&ctx->stack[abs_idx], &ctx->stack[abs_idx + 1],
			(size_t)(ctx->top - 1 - abs_idx) * sizeof(JSValue));
	}
	ctx->top--;
}

DUK_EXTERNAL void duk_pop(duk_context *ctx)
{
	if (ctx->top > ctx->frame_base) {
		ctx->top--;
		JS_FreeValue(ctx->qctx, ctx->stack[ctx->top]);
	}
}

DUK_EXTERNAL void duk_pop_n(duk_context *ctx, duk_idx_t count)
{
	while (count-- > 0 && ctx->top > ctx->frame_base) {
		ctx->top--;
		JS_FreeValue(ctx->qctx, ctx->stack[ctx->top]);
	}
}

DUK_EXTERNAL void duk_pop_2(duk_context *ctx)
{
	duk_pop_n(ctx, 2);
}

DUK_EXTERNAL void duk_pop_3(duk_context *ctx)
{
	duk_pop_n(ctx, 3);
}

/*
 *  Push Operations
 */

DUK_EXTERNAL void duk_push_undefined(duk_context *ctx)
{
	duk_qjs_push_val(ctx, JS_UNDEFINED);
}

DUK_EXTERNAL void duk_push_null(duk_context *ctx)
{
	duk_qjs_push_val(ctx, JS_NULL);
}

DUK_EXTERNAL void duk_push_boolean(duk_context *ctx, duk_bool_t val)
{
	duk_qjs_push_val(ctx, JS_NewBool(ctx->qctx, val != 0));
}

DUK_EXTERNAL void duk_push_true(duk_context *ctx)
{
	duk_qjs_push_val(ctx, JS_TRUE);
}

DUK_EXTERNAL void duk_push_false(duk_context *ctx)
{
	duk_qjs_push_val(ctx, JS_FALSE);
}

DUK_EXTERNAL void duk_push_number(duk_context *ctx, duk_double_t val)
{
	duk_qjs_push_val(ctx, JS_NewFloat64(ctx->qctx, val));
}

DUK_EXTERNAL void duk_push_nan(duk_context *ctx)
{
	duk_qjs_push_val(ctx, JS_NAN);
}

DUK_EXTERNAL void duk_push_int(duk_context *ctx, duk_int_t val)
{
	duk_qjs_push_val(ctx, JS_NewInt32(ctx->qctx, val));
}

DUK_EXTERNAL void duk_push_uint(duk_context *ctx, duk_uint_t val)
{
	duk_qjs_push_val(ctx, JS_NewUint32(ctx->qctx, val));
}

DUK_EXTERNAL const char *duk_push_lstring(duk_context *ctx, const char *str,
					  duk_size_t len)
{
	if (str == NULL) {
		JSValue s = JS_NewStringLen(ctx->qctx, "", 0);
		duk_qjs_push_val(ctx, s);
		return duk_get_string(ctx, -1);
	}
	bool is_hidden = false;
	char *norm = duk_qjs_normalize_key(str, len, &is_hidden);
	JSValue s = JS_NewStringLen(ctx->qctx, norm ? norm : str, len);
	free(norm);
	duk_qjs_push_val(ctx, s);
	return duk_get_string(ctx, -1);
}

DUK_EXTERNAL const char *duk_push_string(duk_context *ctx, const char *str)
{
	if (str == NULL) {
		duk_push_null(ctx);
		return NULL;
	}
	return duk_push_lstring(ctx, str, strlen(str));
}

DUK_EXTERNAL const char *duk_push_literal_raw(duk_context *ctx, const char *str,
					      duk_size_t len)
{
	return duk_push_lstring(ctx, str, len);
}

DUK_EXTERNAL void duk_push_pointer(duk_context *ctx, void *p)
{
	JSValue obj = JS_NewObjectClass(ctx->qctx, g_duk_pointer_class_id);
	JS_SetOpaque(obj, p);
	duk_qjs_push_val(ctx, obj);
}

DUK_EXTERNAL const char *duk_push_vsprintf(duk_context *ctx, const char *fmt,
					   va_list ap)
{
	char buf[1024];
	int n = vsnprintf(buf, sizeof(buf), fmt ? fmt : "", ap);
	if (n < 0)
		n = 0;
	if ((size_t)n >= sizeof(buf))
		n = (int)(sizeof(buf) - 1);
	return duk_push_lstring(ctx, buf, (duk_size_t)n);
}

DUK_EXTERNAL const char *duk_push_sprintf(duk_context *ctx, const char *fmt,
					  ...)
{
	va_list ap;
	va_start(ap, fmt);
	const char *res = duk_push_vsprintf(ctx, fmt, ap);
	va_end(ap);
	return res;
}

DUK_EXTERNAL duk_idx_t duk_push_object(duk_context *ctx)
{
	JSValue obj = JS_NewObjectClass(ctx->qctx, g_duk_object_class_id);
	duk_qjs_push_val(ctx, obj);
	return (ctx->top - 1) - ctx->frame_base;
}

DUK_EXTERNAL duk_idx_t duk_push_bare_object(duk_context *ctx)
{
	JSValue obj =
		JS_NewObjectProtoClass(ctx->qctx, JS_NULL, g_duk_object_class_id);
	duk_qjs_push_val(ctx, obj);
	return (ctx->top - 1) - ctx->frame_base;
}

DUK_EXTERNAL duk_idx_t duk_push_array(duk_context *ctx)
{
	JSValue arr = JS_NewArray(ctx->qctx);
	duk_qjs_push_val(ctx, arr);
	return (ctx->top - 1) - ctx->frame_base;
}

static JSValue duk_qjs_cfunc_trampoline(JSContext *qctx, JSValueConst this_val,
					int argc, JSValueConst *argv,
					int magic, JSValue *func_data)
{
	(void)magic;
	duk_context *ctx = (duk_context *)JS_GetContextOpaque(qctx);
	duk_c_function cfunc = (duk_c_function)(uintptr_t)JS_GetOpaque(
		func_data[0], g_duk_pointer_class_id);
	int32_t duk_nargs = JS_VALUE_GET_INT(func_data[1]);

	int effective_argc = (duk_nargs == DUK_VARARGS || duk_nargs < 0)
				     ? argc
				     : (int)duk_nargs;

	duk_idx_t saved_base = ctx->frame_base;
	duk_idx_t saved_top = ctx->top;
	JSValue saved_this = ctx->current_this;

	ctx->frame_base = saved_top;
	ctx->current_this = JS_DupValue(qctx, this_val);

	duk_qjs_ensure_stack(ctx, effective_argc);
	for (int i = 0; i < effective_argc; i++) {
		if (i < argc) {
			ctx->stack[ctx->top++] = JS_DupValue(qctx, argv[i]);
		} else {
			ctx->stack[ctx->top++] = JS_UNDEFINED;
		}
	}

	struct duk_qjs_catch_point cp;
	cp.prev = ctx->catch_top;
	ctx->catch_top = &cp;

	JSValue ret_val = JS_UNDEFINED;
	if (setjmp(cp.jb) == 0) {
		duk_ret_t rc = cfunc(ctx);
		ctx->catch_top = cp.prev;
		if (rc >= 0 && JS_HasException(qctx)) {
			JS_FreeValue(qctx, JS_GetException(qctx));
		}
		if (rc == 1 && ctx->top > ctx->frame_base) {
			ret_val = JS_DupValue(qctx, ctx->stack[ctx->top - 1]);
		} else if (rc < 0) {
			va_list empty_ap;
			memset(&empty_ap, 0, sizeof(empty_ap));
			JSValue err_obj = duk_qjs_make_error_va(
				ctx, -rc, NULL, 0, NULL, empty_ap);
			ret_val = JS_Throw(qctx, err_obj);
		}
	} else {
		ctx->catch_top = cp.prev;
		if (ctx->has_thrown) {
			JSValue err = ctx->thrown_val;
			ctx->thrown_val = JS_UNDEFINED;
			ctx->has_thrown = false;
			ret_val = JS_Throw(qctx, err);
		} else {
			ret_val = JS_EXCEPTION;
		}
	}

	while (ctx->top > saved_top) {
		ctx->top--;
		JS_FreeValue(qctx, ctx->stack[ctx->top]);
	}
	JS_FreeValue(qctx, ctx->current_this);
	ctx->current_this = saved_this;
	ctx->frame_base = saved_base;
	return ret_val;
}

DUK_EXTERNAL duk_idx_t duk_push_c_function(duk_context *ctx,
					   duk_c_function func, duk_idx_t nargs)
{
	JSValue data[2];
	data[0] = JS_NewObjectClass(ctx->qctx, g_duk_pointer_class_id);
	JS_SetOpaque(data[0], (void *)(uintptr_t)func);
	data[1] = JS_NewInt32(ctx->qctx, (int32_t)nargs);

	int len = (nargs > 0) ? (int)nargs : 0;
	JSValue fn = JS_NewCFunctionData(ctx->qctx, duk_qjs_cfunc_trampoline,
					 len, 0, 2, data);
	JS_SetConstructorBit(ctx->qctx, fn, 1);
	JS_DefinePropertyValueStr(ctx->qctx, fn, "name",
				  JS_NewString(ctx->qctx, ""),
				  JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);

	JSAtom cfunc_atom =
		JS_NewAtomLen(ctx->qctx, DUK_QJS_CFUNC_PROP,
			      sizeof(DUK_QJS_CFUNC_PROP) - 1);
	JS_DefinePropertyValue(ctx->qctx, fn, cfunc_atom,
			       JS_DupValue(ctx->qctx, data[0]),
			       JS_PROP_CONFIGURABLE);
	JS_FreeAtom(ctx->qctx, cfunc_atom);

	JS_FreeValue(ctx->qctx, data[0]);
	JS_FreeValue(ctx->qctx, data[1]);

	duk_qjs_push_val(ctx, fn);
	return (ctx->top - 1) - ctx->frame_base;
}

DUK_EXTERNAL duk_idx_t duk_push_error_object_va_raw(duk_context *ctx,
						    duk_errcode_t err_code,
						    const char *filename,
						    duk_int_t line,
						    const char *fmt, va_list ap)
{
	JSValue err_obj =
		duk_qjs_make_error_va(ctx, err_code, filename, line, fmt, ap);
	duk_qjs_push_val(ctx, err_obj);
	return (ctx->top - 1) - ctx->frame_base;
}

DUK_EXTERNAL duk_idx_t duk_push_error_object_raw(duk_context *ctx,
						 duk_errcode_t err_code,
						 const char *filename,
						 duk_int_t line,
						 const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	duk_idx_t ret = duk_push_error_object_va_raw(ctx, err_code, filename,
						     line, fmt, ap);
	va_end(ap);
	return ret;
}

static void duk_qjs_free_array_buffer(JSRuntime *rt, void *opaque, void *ptr)
{
	(void)rt;
	(void)opaque;
	free(ptr);
}

DUK_EXTERNAL void *duk_push_buffer_raw(duk_context *ctx, duk_size_t size,
				       duk_small_uint_t flags)
{
	(void)flags;
	size_t alloc_sz = size > 0 ? size : 1;
	uint8_t *buf = (uint8_t *)calloc(1, alloc_sz);
	if (buf == NULL)
		return NULL;
	JSValue ab = JS_NewArrayBuffer(ctx->qctx, buf, size,
				       duk_qjs_free_array_buffer, NULL, 0);
	duk_qjs_push_val(ctx, ab);
	return buf;
}

DUK_EXTERNAL void duk_push_buffer_object(duk_context *ctx, duk_idx_t idx_buffer,
					 duk_size_t byte_offset,
					 duk_size_t byte_length,
					 duk_uint_t flags)
{
	duk_idx_t abs_idx = duk_qjs_abs_idx(ctx, idx_buffer);
	if (abs_idx < 0) {
		duk_push_undefined(ctx);
		return;
	}
	JSValue ab = ctx->stack[abs_idx];
	if (flags == DUK_BUFOBJ_ARRAYBUFFER) {
		duk_qjs_push_val(ctx, JS_DupValue(ctx->qctx, ab));
		return;
	}

	JSTypedArrayEnum ta_type = JS_TYPED_ARRAY_UINT8;
	size_t elem_size = 1;
	switch (flags) {
	case DUK_BUFOBJ_INT8ARRAY:
		ta_type = JS_TYPED_ARRAY_INT8;
		elem_size = 1;
		break;
	case DUK_BUFOBJ_UINT8ARRAY:
	case DUK_BUFOBJ_NODEJS_BUFFER:
		ta_type = JS_TYPED_ARRAY_UINT8;
		elem_size = 1;
		break;
	case DUK_BUFOBJ_UINT8CLAMPEDARRAY:
		ta_type = JS_TYPED_ARRAY_UINT8C;
		elem_size = 1;
		break;
	case DUK_BUFOBJ_INT16ARRAY:
		ta_type = JS_TYPED_ARRAY_INT16;
		elem_size = 2;
		break;
	case DUK_BUFOBJ_UINT16ARRAY:
		ta_type = JS_TYPED_ARRAY_UINT16;
		elem_size = 2;
		break;
	case DUK_BUFOBJ_INT32ARRAY:
		ta_type = JS_TYPED_ARRAY_INT32;
		elem_size = 4;
		break;
	case DUK_BUFOBJ_UINT32ARRAY:
		ta_type = JS_TYPED_ARRAY_UINT32;
		elem_size = 4;
		break;
	case DUK_BUFOBJ_FLOAT32ARRAY:
		ta_type = JS_TYPED_ARRAY_FLOAT32;
		elem_size = 4;
		break;
	case DUK_BUFOBJ_FLOAT64ARRAY:
		ta_type = JS_TYPED_ARRAY_FLOAT64;
		elem_size = 8;
		break;
	default:
		ta_type = JS_TYPED_ARRAY_UINT8;
		elem_size = 1;
		break;
	}

	JSValue argv[3];
	argv[0] = ab;
	argv[1] = JS_NewInt64(ctx->qctx, (int64_t)byte_offset);
	argv[2] = JS_NewInt64(ctx->qctx, (int64_t)(byte_length / elem_size));
	JSValue ta = JS_NewTypedArray(ctx->qctx, 3, argv, ta_type);
	JS_FreeValue(ctx->qctx, argv[1]);
	JS_FreeValue(ctx->qctx, argv[2]);
	duk_qjs_push_val(ctx, ta);
}

/*
 *  Type Checks and Getters
 */

DUK_EXTERNAL duk_int_t duk_get_type(duk_context *ctx, duk_idx_t idx)
{
	duk_idx_t abs_idx = duk_qjs_abs_idx(ctx, idx);
	if (abs_idx < 0)
		return DUK_TYPE_NONE;
	JSValue v = ctx->stack[abs_idx];
	if (JS_IsUndefined(v))
		return DUK_TYPE_UNDEFINED;
	if (JS_IsNull(v))
		return DUK_TYPE_NULL;
	if (JS_IsBool(v))
		return DUK_TYPE_BOOLEAN;
	if (JS_IsNumber(v))
		return DUK_TYPE_NUMBER;
	if (JS_IsString(v) || JS_IsSymbol(v))
		return DUK_TYPE_STRING;
	if (JS_IsObject(v)) {
		JSClassID cid = JS_GetClassID(v);
		if (cid == g_duk_pointer_class_id)
			return DUK_TYPE_POINTER;
		if (cid == g_duk_object_class_id ||
		    cid == g_duk_thread_class_id ||
		    JS_IsFunction(ctx->qctx, v) ||
		    JS_IsArray(ctx->qctx, v) > 0)
			return DUK_TYPE_OBJECT;
		size_t sz = 0;
		bool had_exc = JS_HasException(ctx->qctx);
		uint8_t *buf = JS_GetArrayBuffer(ctx->qctx, &sz, v);
		if (!had_exc && JS_HasException(ctx->qctx))
			JS_FreeValue(ctx->qctx, JS_GetException(ctx->qctx));
		if (buf != NULL)
			return DUK_TYPE_BUFFER;
		return DUK_TYPE_OBJECT;
	}
	if (JS_VALUE_GET_TAG(v) == JS_TAG_FUNCTION_BYTECODE)
		return DUK_TYPE_OBJECT;
	return DUK_TYPE_NONE;
}

DUK_EXTERNAL duk_bool_t duk_check_type(duk_context *ctx, duk_idx_t idx,
				       duk_int_t type)
{
	return duk_get_type(ctx, idx) == type ? 1 : 0;
}

DUK_EXTERNAL duk_uint_t duk_get_type_mask(duk_context *ctx, duk_idx_t idx)
{
	return 1U << duk_get_type(ctx, idx);
}

DUK_EXTERNAL duk_bool_t duk_check_type_mask(duk_context *ctx, duk_idx_t idx,
					    duk_uint_t mask)
{
	duk_uint_t m = duk_get_type_mask(ctx, idx);
	if (m & mask)
		return 1;
	if (mask & DUK_TYPE_MASK_THROW) {
		duk_error_raw(ctx, DUK_ERR_TYPE_ERROR, __FILE__, __LINE__,
			      "unexpected type (mask=0x%x, got=0x%x)",
			      (unsigned)mask, (unsigned)m);
	}
	return 0;
}

DUK_EXTERNAL duk_bool_t duk_is_undefined(duk_context *ctx, duk_idx_t idx)
{
	duk_idx_t abs_idx = duk_qjs_abs_idx(ctx, idx);
	return (abs_idx >= 0 && JS_IsUndefined(ctx->stack[abs_idx])) ? 1 : 0;
}

DUK_EXTERNAL duk_bool_t duk_is_null(duk_context *ctx, duk_idx_t idx)
{
	duk_idx_t abs_idx = duk_qjs_abs_idx(ctx, idx);
	return (abs_idx >= 0 && JS_IsNull(ctx->stack[abs_idx])) ? 1 : 0;
}

DUK_EXTERNAL duk_bool_t duk_is_boolean(duk_context *ctx, duk_idx_t idx)
{
	duk_idx_t abs_idx = duk_qjs_abs_idx(ctx, idx);
	return (abs_idx >= 0 && JS_IsBool(ctx->stack[abs_idx])) ? 1 : 0;
}

DUK_EXTERNAL duk_bool_t duk_is_number(duk_context *ctx, duk_idx_t idx)
{
	duk_idx_t abs_idx = duk_qjs_abs_idx(ctx, idx);
	return (abs_idx >= 0 && JS_IsNumber(ctx->stack[abs_idx])) ? 1 : 0;
}

DUK_EXTERNAL duk_bool_t duk_is_nan(duk_context *ctx, duk_idx_t idx)
{
	if (!duk_is_number(ctx, idx))
		return 0;
	return isnan(duk_get_number(ctx, idx)) ? 1 : 0;
}

DUK_EXTERNAL duk_bool_t duk_is_string(duk_context *ctx, duk_idx_t idx)
{
	duk_idx_t abs_idx = duk_qjs_abs_idx(ctx, idx);
	if (abs_idx < 0)
		return 0;
	JSValue v = ctx->stack[abs_idx];
	return (JS_IsString(v) || JS_IsSymbol(v)) ? 1 : 0;
}

DUK_EXTERNAL duk_bool_t duk_is_object(duk_context *ctx, duk_idx_t idx)
{
	duk_idx_t abs_idx = duk_qjs_abs_idx(ctx, idx);
	if (abs_idx < 0)
		return 0;
	JSValue v = ctx->stack[abs_idx];
	if (JS_IsObject(v))
		return JS_GetClassID(v) != g_duk_pointer_class_id ? 1 : 0;
	return JS_VALUE_GET_TAG(v) == JS_TAG_FUNCTION_BYTECODE ? 1 : 0;
}

DUK_EXTERNAL duk_bool_t duk_is_buffer(duk_context *ctx, duk_idx_t idx)
{
	return duk_get_type(ctx, idx) == DUK_TYPE_BUFFER ? 1 : 0;
}

DUK_EXTERNAL duk_bool_t duk_is_buffer_data(duk_context *ctx, duk_idx_t idx)
{
	duk_size_t sz = 0;
	return duk_get_buffer_data(ctx, idx, &sz) != NULL ? 1 : 0;
}

DUK_EXTERNAL duk_bool_t duk_is_pointer(duk_context *ctx, duk_idx_t idx)
{
	duk_idx_t abs_idx = duk_qjs_abs_idx(ctx, idx);
	if (abs_idx < 0)
		return 0;
	JSValue v = ctx->stack[abs_idx];
	return (JS_IsObject(v) && JS_GetClassID(v) == g_duk_pointer_class_id)
		       ? 1
		       : 0;
}

DUK_EXTERNAL duk_bool_t duk_is_function(duk_context *ctx, duk_idx_t idx)
{
	duk_idx_t abs_idx = duk_qjs_abs_idx(ctx, idx);
	if (abs_idx < 0)
		return 0;
	JSValue v = ctx->stack[abs_idx];
	if (JS_VALUE_GET_TAG(v) == JS_TAG_FUNCTION_BYTECODE)
		return 1;
	return JS_IsFunction(ctx->qctx, v) ? 1 : 0;
}

DUK_EXTERNAL duk_bool_t duk_is_array(duk_context *ctx, duk_idx_t idx)
{
	duk_idx_t abs_idx = duk_qjs_abs_idx(ctx, idx);
	if (abs_idx < 0)
		return 0;
	return JS_IsArray(ctx->qctx, ctx->stack[abs_idx]) > 0 ? 1 : 0;
}

DUK_EXTERNAL duk_errcode_t duk_get_error_code(duk_context *ctx, duk_idx_t idx)
{
	duk_idx_t abs_idx = duk_qjs_abs_idx(ctx, idx);
	if (abs_idx < 0)
		return DUK_ERR_NONE;
	JSValue v = ctx->stack[abs_idx];
	if (!JS_IsError(ctx->qctx, v))
		return DUK_ERR_NONE;
	return DUK_ERR_ERROR;
}

DUK_EXTERNAL duk_bool_t duk_get_boolean(duk_context *ctx, duk_idx_t idx)
{
	duk_idx_t abs_idx = duk_qjs_abs_idx(ctx, idx);
	if (abs_idx < 0 || !JS_IsBool(ctx->stack[abs_idx]))
		return 0;
	return JS_VALUE_GET_BOOL(ctx->stack[abs_idx]) ? 1 : 0;
}

DUK_EXTERNAL duk_double_t duk_get_number(duk_context *ctx, duk_idx_t idx)
{
	duk_idx_t abs_idx = duk_qjs_abs_idx(ctx, idx);
	if (abs_idx < 0 || !JS_IsNumber(ctx->stack[abs_idx]))
		return (duk_double_t)NAN;
	double d = 0.0;
	JS_ToFloat64(ctx->qctx, &d, ctx->stack[abs_idx]);
	return d;
}

DUK_EXTERNAL duk_int_t duk_get_int(duk_context *ctx, duk_idx_t idx)
{
	duk_idx_t abs_idx = duk_qjs_abs_idx(ctx, idx);
	if (abs_idx < 0 || !JS_IsNumber(ctx->stack[abs_idx]))
		return 0;
	int32_t v = 0;
	JS_ToInt32(ctx->qctx, &v, ctx->stack[abs_idx]);
	return v;
}

DUK_EXTERNAL duk_uint_t duk_get_uint(duk_context *ctx, duk_idx_t idx)
{
	duk_idx_t abs_idx = duk_qjs_abs_idx(ctx, idx);
	if (abs_idx < 0 || !JS_IsNumber(ctx->stack[abs_idx]))
		return 0;
	uint32_t v = 0;
	JS_ToUint32(ctx->qctx, &v, ctx->stack[abs_idx]);
	return v;
}

DUK_EXTERNAL const char *duk_get_lstring(duk_context *ctx, duk_idx_t idx,
					 duk_size_t *out_len)
{
	if (out_len != NULL)
		*out_len = 0;
	duk_idx_t abs_idx = duk_qjs_abs_idx(ctx, idx);
	if (abs_idx < 0 || !JS_IsString(ctx->stack[abs_idx]))
		return NULL;
	size_t len = 0;
	const char *cstr =
		JS_ToCStringLen(ctx->qctx, &len, ctx->stack[abs_idx]);
	if (out_len != NULL)
		*out_len = len;
	return duk_qjs_cache_cstr(ctx, cstr);
}

DUK_EXTERNAL const char *duk_get_string(duk_context *ctx, duk_idx_t idx)
{
	return duk_get_lstring(ctx, idx, NULL);
}

DUK_EXTERNAL void *duk_get_buffer(duk_context *ctx, duk_idx_t idx,
				  duk_size_t *out_size)
{
	if (out_size != NULL)
		*out_size = 0;
	duk_idx_t abs_idx = duk_qjs_abs_idx(ctx, idx);
	if (abs_idx < 0 || !JS_IsObject(ctx->stack[abs_idx]))
		return NULL;
	JSClassID cid = JS_GetClassID(ctx->stack[abs_idx]);
	if (cid == g_duk_object_class_id || cid == g_duk_pointer_class_id ||
	    cid == g_duk_thread_class_id)
		return NULL;
	size_t sz = 0;
	bool had_exc = JS_HasException(ctx->qctx);
	uint8_t *buf = JS_GetArrayBuffer(ctx->qctx, &sz, ctx->stack[abs_idx]);
	if (!had_exc && JS_HasException(ctx->qctx))
		JS_FreeValue(ctx->qctx, JS_GetException(ctx->qctx));
	if (buf != NULL && out_size != NULL)
		*out_size = sz;
	return buf;
}

DUK_EXTERNAL void *duk_get_buffer_data(duk_context *ctx, duk_idx_t idx,
				       duk_size_t *out_size)
{
	void *buf = duk_get_buffer(ctx, idx, out_size);
	if (buf != NULL)
		return buf;
	duk_idx_t abs_idx = duk_qjs_abs_idx(ctx, idx);
	if (abs_idx < 0 || !JS_IsObject(ctx->stack[abs_idx]))
		return NULL;
	size_t byte_offset = 0, byte_len = 0, bpe = 0;
	JSValue ab = JS_GetTypedArrayBuffer(ctx->qctx, ctx->stack[abs_idx],
					    &byte_offset, &byte_len, &bpe);
	if (JS_IsException(ab)) {
		JS_FreeValue(ctx->qctx, JS_GetException(ctx->qctx));
		return NULL;
	}
	size_t ab_sz = 0;
	uint8_t *ab_ptr = JS_GetArrayBuffer(ctx->qctx, &ab_sz, ab);
	JS_FreeValue(ctx->qctx, ab);
	if (ab_ptr == NULL)
		return NULL;
	if (out_size != NULL)
		*out_size = byte_len;
	return ab_ptr + byte_offset;
}

DUK_EXTERNAL void *duk_get_pointer(duk_context *ctx, duk_idx_t idx)
{
	duk_idx_t abs_idx = duk_qjs_abs_idx(ctx, idx);
	if (abs_idx < 0)
		return NULL;
	JSValue v = ctx->stack[abs_idx];
	if (!JS_IsObject(v) || JS_GetClassID(v) != g_duk_pointer_class_id)
		return NULL;
	return JS_GetOpaque(v, g_duk_pointer_class_id);
}

DUK_EXTERNAL duk_c_function duk_get_c_function(duk_context *ctx, duk_idx_t idx)
{
	duk_idx_t abs_idx = duk_qjs_abs_idx(ctx, idx);
	if (abs_idx < 0 || !JS_IsObject(ctx->stack[abs_idx]))
		return NULL;
	JSAtom cfunc_atom =
		JS_NewAtomLen(ctx->qctx, DUK_QJS_CFUNC_PROP,
			      sizeof(DUK_QJS_CFUNC_PROP) - 1);
	JSValue ptr_val =
		JS_GetProperty(ctx->qctx, ctx->stack[abs_idx], cfunc_atom);
	JS_FreeAtom(ctx->qctx, cfunc_atom);
	if (!JS_IsObject(ptr_val) ||
	    JS_GetClassID(ptr_val) != g_duk_pointer_class_id) {
		JS_FreeValue(ctx->qctx, ptr_val);
		return NULL;
	}
	duk_c_function fn = (duk_c_function)(uintptr_t)JS_GetOpaque(
		ptr_val, g_duk_pointer_class_id);
	JS_FreeValue(ctx->qctx, ptr_val);
	return fn;
}

DUK_EXTERNAL void *duk_get_heapptr(duk_context *ctx, duk_idx_t idx)
{
	duk_idx_t abs_idx = duk_qjs_abs_idx(ctx, idx);
	if (abs_idx < 0)
		return NULL;
	return JS_VALUE_GET_PTR(ctx->stack[abs_idx]);
}

/*
 *  Opt and Require Operations
 */

DUK_EXTERNAL duk_bool_t duk_opt_boolean(duk_context *ctx, duk_idx_t idx,
					duk_bool_t def_value)
{
	if (duk_is_null_or_undefined(ctx, idx))
		return def_value;
	return duk_require_boolean(ctx, idx);
}

DUK_EXTERNAL duk_double_t duk_opt_number(duk_context *ctx, duk_idx_t idx,
					 duk_double_t def_value)
{
	if (duk_is_null_or_undefined(ctx, idx))
		return def_value;
	return duk_require_number(ctx, idx);
}

DUK_EXTERNAL duk_int_t duk_opt_int(duk_context *ctx, duk_idx_t idx,
				   duk_int_t def_value)
{
	if (duk_is_null_or_undefined(ctx, idx))
		return def_value;
	return duk_require_int(ctx, idx);
}

DUK_EXTERNAL const char *duk_opt_string(duk_context *ctx, duk_idx_t idx,
					const char *def_ptr)
{
	if (duk_is_null_or_undefined(ctx, idx))
		return def_ptr;
	return duk_require_string(ctx, idx);
}

DUK_EXTERNAL duk_bool_t duk_require_boolean(duk_context *ctx, duk_idx_t idx)
{
	if (!duk_is_boolean(ctx, idx))
		duk_error_raw(ctx, DUK_ERR_TYPE_ERROR, __FILE__, __LINE__,
			      "boolean required");
	return duk_get_boolean(ctx, idx);
}

DUK_EXTERNAL duk_double_t duk_require_number(duk_context *ctx, duk_idx_t idx)
{
	if (!duk_is_number(ctx, idx))
		duk_error_raw(ctx, DUK_ERR_TYPE_ERROR, __FILE__, __LINE__,
			      "number required");
	return duk_get_number(ctx, idx);
}

DUK_EXTERNAL duk_int_t duk_require_int(duk_context *ctx, duk_idx_t idx)
{
	if (!duk_is_number(ctx, idx))
		duk_error_raw(ctx, DUK_ERR_TYPE_ERROR, __FILE__, __LINE__,
			      "number required");
	return duk_get_int(ctx, idx);
}

DUK_EXTERNAL duk_uint_t duk_require_uint(duk_context *ctx, duk_idx_t idx)
{
	if (!duk_is_number(ctx, idx))
		duk_error_raw(ctx, DUK_ERR_TYPE_ERROR, __FILE__, __LINE__,
			      "number required");
	return duk_get_uint(ctx, idx);
}

DUK_EXTERNAL const char *duk_require_lstring(duk_context *ctx, duk_idx_t idx,
					     duk_size_t *out_len)
{
	if (!duk_is_string(ctx, idx))
		duk_error_raw(ctx, DUK_ERR_TYPE_ERROR, __FILE__, __LINE__,
			      "string required");
	return duk_get_lstring(ctx, idx, out_len);
}

DUK_EXTERNAL const char *duk_require_string(duk_context *ctx, duk_idx_t idx)
{
	return duk_require_lstring(ctx, idx, NULL);
}

DUK_EXTERNAL void duk_require_object(duk_context *ctx, duk_idx_t idx)
{
	if (!duk_is_object(ctx, idx))
		duk_error_raw(ctx, DUK_ERR_TYPE_ERROR, __FILE__, __LINE__,
			      "object required");
}

DUK_EXTERNAL void duk_require_function(duk_context *ctx, duk_idx_t idx)
{
	if (!duk_is_function(ctx, idx))
		duk_error_raw(ctx, DUK_ERR_TYPE_ERROR, __FILE__, __LINE__,
			      "function required");
}

DUK_EXTERNAL void *duk_require_pointer(duk_context *ctx, duk_idx_t idx)
{
	if (!duk_is_pointer(ctx, idx))
		duk_error_raw(ctx, DUK_ERR_TYPE_ERROR, __FILE__, __LINE__,
			      "pointer required");
	return duk_get_pointer(ctx, idx);
}

/*
 *  Coercions
 */

DUK_EXTERNAL void duk_to_undefined(duk_context *ctx, duk_idx_t idx)
{
	duk_idx_t abs_idx = duk_qjs_abs_idx(ctx, idx);
	if (abs_idx < 0)
		return;
	JS_FreeValue(ctx->qctx, ctx->stack[abs_idx]);
	ctx->stack[abs_idx] = JS_UNDEFINED;
}

DUK_EXTERNAL void duk_to_null(duk_context *ctx, duk_idx_t idx)
{
	duk_idx_t abs_idx = duk_qjs_abs_idx(ctx, idx);
	if (abs_idx < 0)
		return;
	JS_FreeValue(ctx->qctx, ctx->stack[abs_idx]);
	ctx->stack[abs_idx] = JS_NULL;
}

DUK_EXTERNAL duk_bool_t duk_to_boolean(duk_context *ctx, duk_idx_t idx)
{
	duk_idx_t abs_idx = duk_qjs_abs_idx(ctx, idx);
	if (abs_idx < 0)
		return 0;
	JSValue v = ctx->stack[abs_idx];
	int b = 0;
	if (JS_IsObject(v) && JS_GetClassID(v) == g_duk_pointer_class_id) {
		b = JS_GetOpaque(v, g_duk_pointer_class_id) != NULL ? 1 : 0;
	} else {
		b = JS_ToBool(ctx->qctx, v) > 0 ? 1 : 0;
	}
	JS_FreeValue(ctx->qctx, v);
	ctx->stack[abs_idx] = JS_NewBool(ctx->qctx, b);
	return (duk_bool_t)b;
}

DUK_EXTERNAL duk_double_t duk_to_number(duk_context *ctx, duk_idx_t idx)
{
	duk_idx_t abs_idx = duk_qjs_abs_idx(ctx, idx);
	if (abs_idx < 0)
		return (duk_double_t)NAN;
	double d = 0.0;
	if (JS_ToFloat64(ctx->qctx, &d, ctx->stack[abs_idx]) < 0) {
		JS_FreeValue(ctx->qctx, JS_GetException(ctx->qctx));
		d = (double)NAN;
	}
	JS_FreeValue(ctx->qctx, ctx->stack[abs_idx]);
	ctx->stack[abs_idx] = JS_NewFloat64(ctx->qctx, d);
	return d;
}

DUK_EXTERNAL duk_int_t duk_to_int(duk_context *ctx, duk_idx_t idx)
{
	duk_idx_t abs_idx = duk_qjs_abs_idx(ctx, idx);
	if (abs_idx < 0)
		return 0;
	int32_t v = 0;
	if (JS_ToInt32(ctx->qctx, &v, ctx->stack[abs_idx]) < 0) {
		JS_FreeValue(ctx->qctx, JS_GetException(ctx->qctx));
		v = 0;
	}
	JS_FreeValue(ctx->qctx, ctx->stack[abs_idx]);
	ctx->stack[abs_idx] = JS_NewInt32(ctx->qctx, v);
	return v;
}

DUK_EXTERNAL duk_uint_t duk_to_uint(duk_context *ctx, duk_idx_t idx)
{
	duk_idx_t abs_idx = duk_qjs_abs_idx(ctx, idx);
	if (abs_idx < 0)
		return 0;
	uint32_t v = 0;
	if (JS_ToUint32(ctx->qctx, &v, ctx->stack[abs_idx]) < 0) {
		JS_FreeValue(ctx->qctx, JS_GetException(ctx->qctx));
		v = 0;
	}
	JS_FreeValue(ctx->qctx, ctx->stack[abs_idx]);
	ctx->stack[abs_idx] = JS_NewUint32(ctx->qctx, v);
	return v;
}

DUK_EXTERNAL duk_int32_t duk_to_int32(duk_context *ctx, duk_idx_t idx)
{
	return duk_to_int(ctx, idx);
}

DUK_EXTERNAL duk_uint32_t duk_to_uint32(duk_context *ctx, duk_idx_t idx)
{
	return duk_to_uint(ctx, idx);
}

DUK_EXTERNAL duk_uint16_t duk_to_uint16(duk_context *ctx, duk_idx_t idx)
{
	duk_uint32_t v = duk_to_uint32(ctx, idx) & 0xFFFFU;
	duk_idx_t abs_idx = duk_qjs_abs_idx(ctx, idx);
	if (abs_idx >= 0) {
		JS_FreeValue(ctx->qctx, ctx->stack[abs_idx]);
		ctx->stack[abs_idx] = JS_NewUint32(ctx->qctx, v);
	}
	return (duk_uint16_t)v;
}

DUK_EXTERNAL const char *duk_safe_to_lstring(duk_context *ctx, duk_idx_t idx,
					     duk_size_t *out_len)
{
	if (out_len != NULL)
		*out_len = 0;
	duk_idx_t abs_idx = duk_qjs_abs_idx(ctx, idx);
	if (abs_idx < 0)
		return NULL;
	JSValue v = ctx->stack[abs_idx];
	JSValue str_val;
	if (JS_IsObject(v) && JS_GetClassID(v) == g_duk_pointer_class_id) {
		char ptr_buf[48];
		snprintf(ptr_buf, sizeof(ptr_buf), "%p",
			 JS_GetOpaque(v, g_duk_pointer_class_id));
		str_val = JS_NewString(ctx->qctx, ptr_buf);
	} else {
		str_val = JS_ToString(ctx->qctx, v);
		if (JS_IsException(str_val)) {
			JS_FreeValue(ctx->qctx, JS_GetException(ctx->qctx));
			str_val = JS_NewString(ctx->qctx, "[object]");
		}
	}
	JS_FreeValue(ctx->qctx, ctx->stack[abs_idx]);
	ctx->stack[abs_idx] = str_val;
	return duk_get_lstring(ctx, idx, out_len);
}

DUK_EXTERNAL const char *duk_to_lstring(duk_context *ctx, duk_idx_t idx,
					duk_size_t *out_len)
{
	return duk_safe_to_lstring(ctx, idx, out_len);
}

DUK_EXTERNAL const char *duk_to_string(duk_context *ctx, duk_idx_t idx)
{
	return duk_safe_to_lstring(ctx, idx, NULL);
}

DUK_EXTERNAL const char *duk_safe_to_stacktrace(duk_context *ctx, duk_idx_t idx)
{
	duk_idx_t abs_idx = duk_qjs_abs_idx(ctx, idx);
	if (abs_idx < 0)
		return "";
	JSValue v = ctx->stack[abs_idx];
	if (JS_IsObject(v)) {
		JSValue stack_val = JS_GetPropertyStr(ctx->qctx, v, "stack");
		if (JS_IsString(stack_val)) {
			size_t msg_len = 0, st_len = 0;
			JSValue str_v = JS_ToString(ctx->qctx, v);
			if (!JS_IsException(str_v)) {
				const char *msg_c = JS_ToCStringLen(
					ctx->qctx, &msg_len, str_v);
				const char *st_c = JS_ToCStringLen(
					ctx->qctx, &st_len, stack_val);
				if (msg_c != NULL && st_c != NULL) {
					size_t tot = msg_len + 1 + st_len;
					char *comb = (char *)malloc(tot + 1);
					if (comb != NULL) {
						memcpy(comb, msg_c, msg_len);
						comb[msg_len] = '\n';
						memcpy(comb + msg_len + 1, st_c,
						       st_len);
						comb[tot] = '\0';
						JS_FreeValue(ctx->qctx,
							     ctx->stack[abs_idx]);
						ctx->stack[abs_idx] =
							JS_NewStringLen(
								ctx->qctx, comb,
								tot);
						free(comb);
					}
				}
				if (msg_c != NULL)
					JS_FreeCString(ctx->qctx, msg_c);
				if (st_c != NULL)
					JS_FreeCString(ctx->qctx, st_c);
			}
			JS_FreeValue(ctx->qctx, str_v);
			JS_FreeValue(ctx->qctx, stack_val);
			return duk_get_string(ctx, idx);
		}
		JS_FreeValue(ctx->qctx, stack_val);
	}
	return duk_safe_to_lstring(ctx, idx, NULL);
}

DUK_EXTERNAL const char *duk_to_stacktrace(duk_context *ctx, duk_idx_t idx)
{
	return duk_safe_to_stacktrace(ctx, idx);
}

DUK_EXTERNAL void *duk_to_pointer(duk_context *ctx, duk_idx_t idx)
{
	return duk_get_pointer(ctx, idx);
}

DUK_EXTERNAL void duk_to_object(duk_context *ctx, duk_idx_t idx)
{
	duk_idx_t abs_idx = duk_qjs_abs_idx(ctx, idx);
	if (abs_idx < 0)
		return;
	JSValue val = ctx->stack[abs_idx];
	if (JS_IsObject(val))
		return;
	if (JS_IsNull(val) || JS_IsUndefined(val)) {
		JS_ThrowTypeError(ctx->qctx, "cannot convert null or undefined to object");
		duk_qjs_push_val(ctx, JS_GetException(ctx->qctx));
		duk_throw_raw(ctx);
	}
	JSValue global = JS_GetGlobalObject(ctx->qctx);
	JSValue obj_ctor = JS_GetPropertyStr(ctx->qctx, global, "Object");
	JS_FreeValue(ctx->qctx, global);
	JSValue obj = JS_Call(ctx->qctx, obj_ctor, JS_UNDEFINED, 1, &val);
	JS_FreeValue(ctx->qctx, obj_ctor);
	if (JS_IsException(obj)) {
		duk_qjs_push_val(ctx, JS_GetException(ctx->qctx));
		duk_throw_raw(ctx);
	}
	JS_FreeValue(ctx->qctx, ctx->stack[abs_idx]);
	ctx->stack[abs_idx] = obj;
}

/*
 *  Length, Concat, Comparisons
 */

DUK_EXTERNAL duk_size_t duk_get_length(duk_context *ctx, duk_idx_t idx)
{
	duk_idx_t abs_idx = duk_qjs_abs_idx(ctx, idx);
	if (abs_idx < 0)
		return 0;
	JSValue v = ctx->stack[abs_idx];
	if (JS_IsString(v)) {
		size_t len = 0;
		const char *s = JS_ToCStringLen(ctx->qctx, &len, v);
		if (s != NULL)
			JS_FreeCString(ctx->qctx, s);
		return len;
	}
	if (JS_IsObject(v)) {
		JSValue len_val = JS_GetPropertyStr(ctx->qctx, v, "length");
		if (JS_IsException(len_val)) {
			JS_FreeValue(ctx->qctx, JS_GetException(ctx->qctx));
			return 0;
		}
		uint32_t len = 0;
		JS_ToUint32(ctx->qctx, &len, len_val);
		JS_FreeValue(ctx->qctx, len_val);
		return len;
	}
	return 0;
}

DUK_EXTERNAL void duk_set_length(duk_context *ctx, duk_idx_t idx,
				 duk_size_t len)
{
	duk_idx_t abs_idx = duk_qjs_abs_idx(ctx, idx);
	if (abs_idx < 0 || !JS_IsObject(ctx->stack[abs_idx]))
		return;
	JS_SetPropertyStr(ctx->qctx, ctx->stack[abs_idx], "length",
			  JS_NewUint32(ctx->qctx, (uint32_t)len));
}

DUK_EXTERNAL void duk_concat(duk_context *ctx, duk_idx_t count)
{
	if (count <= 0) {
		duk_push_lstring(ctx, "", 0);
		return;
	}
	if (count == 1) {
		duk_to_string(ctx, -1);
		return;
	}
	duk_idx_t start = ctx->top - count;
	if (start < ctx->frame_base)
		start = ctx->frame_base;
	duk_idx_t actual_count = ctx->top - start;

	size_t total_len = 0;
	for (duk_idx_t i = 0; i < actual_count; i++) {
		duk_size_t part_len = 0;
		duk_safe_to_lstring(ctx, -(actual_count - i), &part_len);
		total_len += part_len;
	}

	char *buf = (char *)malloc(total_len + 1);
	size_t pos = 0;
	for (duk_idx_t i = 0; i < actual_count; i++) {
		size_t part_len = 0;
		const char *s =
			JS_ToCStringLen(ctx->qctx, &part_len,
					ctx->stack[start + i]);
		if (s != NULL) {
			memcpy(buf + pos, s, part_len);
			pos += part_len;
			JS_FreeCString(ctx->qctx, s);
		}
	}
	buf[pos] = '\0';

	duk_pop_n(ctx, actual_count);
	duk_push_lstring(ctx, buf, pos);
	free(buf);
}

DUK_EXTERNAL duk_bool_t duk_strict_equals(duk_context *ctx, duk_idx_t idx1,
					  duk_idx_t idx2)
{
	duk_idx_t a1 = duk_qjs_abs_idx(ctx, idx1);
	duk_idx_t a2 = duk_qjs_abs_idx(ctx, idx2);
	if (a1 < 0 || a2 < 0)
		return 0;
	JSValue v1 = ctx->stack[a1];
	JSValue v2 = ctx->stack[a2];
	if (JS_IsObject(v1) && JS_IsObject(v2)) {
		if (JS_GetClassID(v1) == g_duk_pointer_class_id &&
		    JS_GetClassID(v2) == g_duk_pointer_class_id) {
			return JS_GetOpaque(v1, g_duk_pointer_class_id) ==
				       JS_GetOpaque(v2, g_duk_pointer_class_id)
				       ? 1
				       : 0;
		}
		return JS_VALUE_GET_PTR(v1) == JS_VALUE_GET_PTR(v2) ? 1 : 0;
	}
	if (JS_VALUE_GET_TAG(v1) != JS_VALUE_GET_TAG(v2)) {
		if (JS_IsNumber(v1) && JS_IsNumber(v2)) {
			double d1 = 0, d2 = 0;
			JS_ToFloat64(ctx->qctx, &d1, v1);
			JS_ToFloat64(ctx->qctx, &d2, v2);
			return d1 == d2 ? 1 : 0;
		}
		return 0;
	}
	if (JS_IsUndefined(v1) || JS_IsNull(v1))
		return 1;
	if (JS_IsBool(v1))
		return JS_VALUE_GET_BOOL(v1) == JS_VALUE_GET_BOOL(v2) ? 1 : 0;
	if (JS_IsNumber(v1)) {
		double d1 = 0, d2 = 0;
		JS_ToFloat64(ctx->qctx, &d1, v1);
		JS_ToFloat64(ctx->qctx, &d2, v2);
		return d1 == d2 ? 1 : 0;
	}
	if (JS_IsString(v1)) {
		size_t l1 = 0, l2 = 0;
		const char *s1 = JS_ToCStringLen(ctx->qctx, &l1, v1);
		const char *s2 = JS_ToCStringLen(ctx->qctx, &l2, v2);
		bool eq = (l1 == l2) && (s1 != NULL && s2 != NULL) &&
			  (memcmp(s1, s2, l1) == 0);
		if (s1 != NULL)
			JS_FreeCString(ctx->qctx, s1);
		if (s2 != NULL)
			JS_FreeCString(ctx->qctx, s2);
		return eq ? 1 : 0;
	}
	return JS_VALUE_GET_PTR(v1) == JS_VALUE_GET_PTR(v2) ? 1 : 0;
}

DUK_EXTERNAL duk_bool_t duk_equals(duk_context *ctx, duk_idx_t idx1,
				   duk_idx_t idx2)
{
	if (duk_strict_equals(ctx, idx1, idx2))
		return 1;
	if (duk_is_null_or_undefined(ctx, idx1) &&
	    duk_is_null_or_undefined(ctx, idx2))
		return 1;
	return 0;
}

DUK_EXTERNAL duk_bool_t duk_instanceof(duk_context *ctx, duk_idx_t idx1,
				       duk_idx_t idx2)
{
	duk_idx_t a1 = duk_qjs_abs_idx(ctx, idx1);
	duk_idx_t a2 = duk_qjs_abs_idx(ctx, idx2);
	if (a1 < 0 || a2 < 0)
		return 0;
	int r = JS_IsInstanceOf(ctx->qctx, ctx->stack[a1], ctx->stack[a2]);
	if (r < 0) {
		JS_FreeValue(ctx->qctx, JS_GetException(ctx->qctx));
		return 0;
	}
	return r ? 1 : 0;
}

/*
 *  Property Access, Prototypes, Finalizers
 */

static duk_bool_t duk_qjs_get_prop_atom(duk_context *ctx, JSValueConst obj,
					JSAtom atom)
{
	if (!JS_IsObject(obj)) {
		duk_push_undefined(ctx);
		return 0;
	}
	JSValue val = JS_GetProperty(ctx->qctx, obj, atom);
	if (JS_IsException(val)) {
		JS_FreeValue(ctx->qctx, JS_GetException(ctx->qctx));
		duk_push_undefined(ctx);
		return 0;
	}
	duk_bool_t present = !JS_IsUndefined(val);
	duk_qjs_push_val(ctx, val);
	return present;
}

static duk_bool_t duk_qjs_put_prop_atom(duk_context *ctx, JSValueConst obj,
					JSAtom atom, JSValue val,
					bool is_hidden)
{
	if (!JS_IsObject(obj)) {
		JS_FreeValue(ctx->qctx, val);
		return 0;
	}
	if (is_hidden) {
		int r = JS_DefinePropertyValue(
			ctx->qctx, obj, atom, val,
			JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
		if (r < 0)
			JS_FreeValue(ctx->qctx, JS_GetException(ctx->qctx));
		return r >= 0 ? 1 : 0;
	}
	int r = JS_SetProperty(ctx->qctx, obj, atom, val);
	if (r < 0)
		JS_FreeValue(ctx->qctx, JS_GetException(ctx->qctx));
	return r >= 0 ? 1 : 0;
}

DUK_EXTERNAL duk_bool_t duk_get_prop(duk_context *ctx, duk_idx_t obj_idx)
{
	duk_idx_t abs_obj = duk_qjs_abs_idx(ctx, obj_idx);
	duk_idx_t abs_key = duk_qjs_abs_idx(ctx, -1);
	if (abs_obj < 0 || abs_key < 0)
		return 0;
	JSValue obj = JS_DupValue(ctx->qctx, ctx->stack[abs_obj]);
	bool is_hidden = false;
	JSAtom atom = duk_qjs_val_to_atom(ctx, ctx->stack[abs_key], &is_hidden);
	duk_pop(ctx);
	duk_bool_t ret = duk_qjs_get_prop_atom(ctx, obj, atom);
	JS_FreeAtom(ctx->qctx, atom);
	JS_FreeValue(ctx->qctx, obj);
	return ret;
}

DUK_EXTERNAL duk_bool_t duk_get_prop_lstring(duk_context *ctx,
					     duk_idx_t obj_idx, const char *key,
					     duk_size_t key_len)
{
	duk_idx_t abs_obj = duk_qjs_abs_idx(ctx, obj_idx);
	if (abs_obj < 0) {
		duk_push_undefined(ctx);
		return 0;
	}
	if (ctx->finalizing_private_ptr != NULL && abs_obj == ctx->frame_base &&
	    key != NULL &&
	    key_len == sizeof(DUK_QJS_RAW_PRIVATE_PROP) - 1 &&
	    memcmp(key, DUK_QJS_RAW_PRIVATE_PROP, key_len) == 0) {
		duk_push_pointer(ctx, ctx->finalizing_private_ptr);
		return 1;
	}
	bool is_hidden = false;
	JSAtom atom = duk_qjs_key_to_atom(ctx, key, key_len, &is_hidden);
	duk_bool_t ret = duk_qjs_get_prop_atom(ctx, ctx->stack[abs_obj], atom);
	JS_FreeAtom(ctx->qctx, atom);
	return ret;
}

DUK_EXTERNAL duk_bool_t duk_get_prop_string(duk_context *ctx, duk_idx_t obj_idx,
					    const char *key)
{
	return duk_get_prop_lstring(ctx, obj_idx, key,
				    key ? strlen(key) : 0);
}

DUK_EXTERNAL duk_bool_t duk_get_prop_literal_raw(duk_context *ctx,
						 duk_idx_t obj_idx,
						 const char *key,
						 duk_size_t key_len)
{
	return duk_get_prop_lstring(ctx, obj_idx, key, key_len);
}

DUK_EXTERNAL duk_bool_t duk_get_prop_index(duk_context *ctx, duk_idx_t obj_idx,
					   duk_uarridx_t arr_idx)
{
	duk_idx_t abs_obj = duk_qjs_abs_idx(ctx, obj_idx);
	if (abs_obj < 0 || !JS_IsObject(ctx->stack[abs_obj])) {
		duk_push_undefined(ctx);
		return 0;
	}
	JSValue val =
		JS_GetPropertyUint32(ctx->qctx, ctx->stack[abs_obj], arr_idx);
	if (JS_IsException(val)) {
		JS_FreeValue(ctx->qctx, JS_GetException(ctx->qctx));
		duk_push_undefined(ctx);
		return 0;
	}
	duk_bool_t present = !JS_IsUndefined(val);
	duk_qjs_push_val(ctx, val);
	return present;
}

DUK_EXTERNAL duk_bool_t duk_put_prop(duk_context *ctx, duk_idx_t obj_idx)
{
	duk_idx_t abs_obj = duk_qjs_abs_idx(ctx, obj_idx);
	if (abs_obj < 0 || ctx->top - ctx->frame_base < 2)
		return 0;
	JSValue obj = JS_DupValue(ctx->qctx, ctx->stack[abs_obj]);
	JSValue val = JS_DupValue(ctx->qctx, ctx->stack[ctx->top - 1]);
	bool is_hidden = false;
	JSAtom atom =
		duk_qjs_val_to_atom(ctx, ctx->stack[ctx->top - 2], &is_hidden);
	duk_pop_2(ctx);
	duk_bool_t ret = duk_qjs_put_prop_atom(ctx, obj, atom, val, is_hidden);
	JS_FreeAtom(ctx->qctx, atom);
	JS_FreeValue(ctx->qctx, obj);
	return ret;
}

DUK_EXTERNAL duk_bool_t duk_put_prop_lstring(duk_context *ctx,
					     duk_idx_t obj_idx, const char *key,
					     duk_size_t key_len)
{
	duk_idx_t abs_obj = duk_qjs_abs_idx(ctx, obj_idx);
	if (abs_obj < 0 || ctx->top <= ctx->frame_base)
		return 0;
	JSValue obj = JS_DupValue(ctx->qctx, ctx->stack[abs_obj]);
	ctx->top--;
	JSValue val = ctx->stack[ctx->top];

	if (key != NULL && key_len == sizeof(DUK_QJS_RAW_PRIVATE_PROP) - 1 &&
	    memcmp(key, DUK_QJS_RAW_PRIVATE_PROP, key_len) == 0 &&
	    JS_IsObject(val) &&
	    JS_GetClassID(val) == g_duk_pointer_class_id) {
		void *priv = JS_GetOpaque(val, g_duk_pointer_class_id);
		struct duk_qjs_obj_meta *meta =
			duk_qjs_get_or_create_meta(ctx, obj);
		if (meta != NULL) {
			meta->private_ptr = priv;
			meta->ctx = ctx;
		}
	} else if (key != NULL && key_len == 28 &&
		   memcmp(key, "\xFF\xFFNETSURF_DUKTAPE_klass_name", 28) == 0 &&
		   JS_IsObject(obj) && JS_IsString(val)) {
		JSValue real_glob = JS_GetGlobalObject(ctx->qctx);
		JSValue sym_ctor = JS_GetPropertyStr(ctx->qctx, real_glob, "Symbol");
		if (JS_IsObject(sym_ctor)) {
			JSValue tag_sym = JS_GetPropertyStr(ctx->qctx, sym_ctor, "toStringTag");
			if (JS_IsSymbol(tag_sym)) {
				JSAtom tag_atom = JS_ValueToAtom(ctx->qctx, tag_sym);
				if (tag_atom != JS_ATOM_NULL) {
					if (JS_DefinePropertyValue(
						    ctx->qctx, obj, tag_atom,
						    JS_DupValue(ctx->qctx, val),
						    JS_PROP_CONFIGURABLE) < 0) {
						JS_FreeValue(ctx->qctx,
							     JS_GetException(ctx->qctx));
					}
					JS_FreeAtom(ctx->qctx, tag_atom);
				}
			}
			JS_FreeValue(ctx->qctx, tag_sym);
		}
		JS_FreeValue(ctx->qctx, sym_ctor);
		JS_FreeValue(ctx->qctx, real_glob);
	}

	bool is_hidden = false;
	JSAtom atom = duk_qjs_key_to_atom(ctx, key, key_len, &is_hidden);
	duk_bool_t ret = duk_qjs_put_prop_atom(ctx, obj, atom, val, is_hidden);
	JS_FreeAtom(ctx->qctx, atom);
	JS_FreeValue(ctx->qctx, obj);
	return ret;
}

DUK_EXTERNAL duk_bool_t duk_put_prop_string(duk_context *ctx, duk_idx_t obj_idx,
					    const char *key)
{
	return duk_put_prop_lstring(ctx, obj_idx, key,
				    key ? strlen(key) : 0);
}

DUK_EXTERNAL duk_bool_t duk_put_prop_literal_raw(duk_context *ctx,
						 duk_idx_t obj_idx,
						 const char *key,
						 duk_size_t key_len)
{
	return duk_put_prop_lstring(ctx, obj_idx, key, key_len);
}

DUK_EXTERNAL duk_bool_t duk_put_prop_index(duk_context *ctx, duk_idx_t obj_idx,
					   duk_uarridx_t arr_idx)
{
	duk_idx_t abs_obj = duk_qjs_abs_idx(ctx, obj_idx);
	if (abs_obj < 0 || ctx->top <= ctx->frame_base)
		return 0;
	JSValue obj = JS_DupValue(ctx->qctx, ctx->stack[abs_obj]);
	ctx->top--;
	JSValue val = ctx->stack[ctx->top];
	int r = JS_SetPropertyUint32(ctx->qctx, obj, arr_idx, val);
	if (r < 0)
		JS_FreeValue(ctx->qctx, JS_GetException(ctx->qctx));
	JS_FreeValue(ctx->qctx, obj);
	return r >= 0 ? 1 : 0;
}

DUK_EXTERNAL duk_bool_t duk_del_prop(duk_context *ctx, duk_idx_t obj_idx)
{
	duk_idx_t abs_obj = duk_qjs_abs_idx(ctx, obj_idx);
	if (abs_obj < 0 || ctx->top <= ctx->frame_base)
		return 0;
	JSValue obj = JS_DupValue(ctx->qctx, ctx->stack[abs_obj]);
	bool is_hidden = false;
	JSAtom atom =
		duk_qjs_val_to_atom(ctx, ctx->stack[ctx->top - 1], &is_hidden);
	duk_pop(ctx);
	int r = JS_DeleteProperty(ctx->qctx, obj, atom, 0);
	if (r < 0)
		JS_FreeValue(ctx->qctx, JS_GetException(ctx->qctx));
	JS_FreeAtom(ctx->qctx, atom);
	JS_FreeValue(ctx->qctx, obj);
	return r >= 0 ? 1 : 0;
}

DUK_EXTERNAL duk_bool_t duk_del_prop_lstring(duk_context *ctx,
					     duk_idx_t obj_idx, const char *key,
					     duk_size_t key_len)
{
	duk_idx_t abs_obj = duk_qjs_abs_idx(ctx, obj_idx);
	if (abs_obj < 0 || !JS_IsObject(ctx->stack[abs_obj]))
		return 0;
	bool is_hidden = false;
	JSAtom atom = duk_qjs_key_to_atom(ctx, key, key_len, &is_hidden);
	int r = JS_DeleteProperty(ctx->qctx, ctx->stack[abs_obj], atom, 0);
	if (r < 0)
		JS_FreeValue(ctx->qctx, JS_GetException(ctx->qctx));
	JS_FreeAtom(ctx->qctx, atom);
	return r >= 0 ? 1 : 0;
}

DUK_EXTERNAL duk_bool_t duk_del_prop_string(duk_context *ctx, duk_idx_t obj_idx,
					    const char *key)
{
	return duk_del_prop_lstring(ctx, obj_idx, key,
				    key ? strlen(key) : 0);
}

DUK_EXTERNAL duk_bool_t duk_del_prop_literal_raw(duk_context *ctx,
						 duk_idx_t obj_idx,
						 const char *key,
						 duk_size_t key_len)
{
	return duk_del_prop_lstring(ctx, obj_idx, key, key_len);
}

DUK_EXTERNAL duk_bool_t duk_del_prop_index(duk_context *ctx, duk_idx_t obj_idx,
					   duk_uarridx_t arr_idx)
{
	duk_idx_t abs_obj = duk_qjs_abs_idx(ctx, obj_idx);
	if (abs_obj < 0 || !JS_IsObject(ctx->stack[abs_obj]))
		return 0;
	JSAtom atom = JS_NewAtomUInt32(ctx->qctx, arr_idx);
	int r = JS_DeleteProperty(ctx->qctx, ctx->stack[abs_obj], atom, 0);
	if (r < 0)
		JS_FreeValue(ctx->qctx, JS_GetException(ctx->qctx));
	JS_FreeAtom(ctx->qctx, atom);
	return r >= 0 ? 1 : 0;
}

DUK_EXTERNAL duk_bool_t duk_has_prop(duk_context *ctx, duk_idx_t obj_idx)
{
	duk_idx_t abs_obj = duk_qjs_abs_idx(ctx, obj_idx);
	if (abs_obj < 0 || ctx->top <= ctx->frame_base)
		return 0;
	JSValue obj = JS_DupValue(ctx->qctx, ctx->stack[abs_obj]);
	bool is_hidden = false;
	JSAtom atom =
		duk_qjs_val_to_atom(ctx, ctx->stack[ctx->top - 1], &is_hidden);
	duk_pop(ctx);
	int r = JS_IsObject(obj) ? JS_HasProperty(ctx->qctx, obj, atom) : 0;
	if (r < 0) {
		JS_FreeValue(ctx->qctx, JS_GetException(ctx->qctx));
		r = 0;
	}
	JS_FreeAtom(ctx->qctx, atom);
	JS_FreeValue(ctx->qctx, obj);
	return r ? 1 : 0;
}

DUK_EXTERNAL duk_bool_t duk_has_prop_lstring(duk_context *ctx,
					     duk_idx_t obj_idx, const char *key,
					     duk_size_t key_len)
{
	duk_idx_t abs_obj = duk_qjs_abs_idx(ctx, obj_idx);
	if (abs_obj < 0 || !JS_IsObject(ctx->stack[abs_obj]))
		return 0;
	bool is_hidden = false;
	JSAtom atom = duk_qjs_key_to_atom(ctx, key, key_len, &is_hidden);
	int r = JS_HasProperty(ctx->qctx, ctx->stack[abs_obj], atom);
	if (r < 0) {
		JS_FreeValue(ctx->qctx, JS_GetException(ctx->qctx));
		r = 0;
	}
	JS_FreeAtom(ctx->qctx, atom);
	return r ? 1 : 0;
}

DUK_EXTERNAL duk_bool_t duk_has_prop_string(duk_context *ctx, duk_idx_t obj_idx,
					    const char *key)
{
	return duk_has_prop_lstring(ctx, obj_idx, key,
				    key ? strlen(key) : 0);
}

DUK_EXTERNAL duk_bool_t duk_has_prop_literal_raw(duk_context *ctx,
						 duk_idx_t obj_idx,
						 const char *key,
						 duk_size_t key_len)
{
	return duk_has_prop_lstring(ctx, obj_idx, key, key_len);
}

DUK_EXTERNAL duk_bool_t duk_has_prop_index(duk_context *ctx, duk_idx_t obj_idx,
					   duk_uarridx_t arr_idx)
{
	duk_idx_t abs_obj = duk_qjs_abs_idx(ctx, obj_idx);
	if (abs_obj < 0 || !JS_IsObject(ctx->stack[abs_obj]))
		return 0;
	JSAtom atom = JS_NewAtomUInt32(ctx->qctx, arr_idx);
	int r = JS_HasProperty(ctx->qctx, ctx->stack[abs_obj], atom);
	if (r < 0) {
		JS_FreeValue(ctx->qctx, JS_GetException(ctx->qctx));
		r = 0;
	}
	JS_FreeAtom(ctx->qctx, atom);
	return r ? 1 : 0;
}

DUK_EXTERNAL void duk_def_prop(duk_context *ctx, duk_idx_t obj_idx,
			       duk_uint_t flags)
{
	duk_idx_t abs_obj = duk_qjs_abs_idx(ctx, obj_idx);
	JSValue obj = abs_obj >= 0 ? JS_DupValue(ctx->qctx, ctx->stack[abs_obj])
				   : JS_UNDEFINED;

	JSValue setter = JS_UNDEFINED;
	JSValue getter = JS_UNDEFINED;
	JSValue val = JS_UNDEFINED;

	if (flags & DUK_DEFPROP_HAVE_SETTER) {
		setter = JS_DupValue(ctx->qctx, ctx->stack[ctx->top - 1]);
		duk_pop(ctx);
	}
	if (flags & DUK_DEFPROP_HAVE_GETTER) {
		getter = JS_DupValue(ctx->qctx, ctx->stack[ctx->top - 1]);
		duk_pop(ctx);
	}
	if (flags & DUK_DEFPROP_HAVE_VALUE) {
		val = JS_DupValue(ctx->qctx, ctx->stack[ctx->top - 1]);
		duk_pop(ctx);
	}

	bool is_hidden = false;
	JSAtom prop =
		duk_qjs_val_to_atom(ctx, ctx->stack[ctx->top - 1], &is_hidden);
	duk_pop(ctx);

	if (JS_IsObject(obj)) {
		int qflags = 0;
		if (flags & DUK_DEFPROP_HAVE_CONFIGURABLE) {
			qflags |= JS_PROP_HAS_CONFIGURABLE;
			if (flags & DUK_DEFPROP_CONFIGURABLE)
				qflags |= JS_PROP_CONFIGURABLE;
		}
		if (flags & DUK_DEFPROP_HAVE_WRITABLE) {
			qflags |= JS_PROP_HAS_WRITABLE;
			if (flags & DUK_DEFPROP_WRITABLE)
				qflags |= JS_PROP_WRITABLE;
		}
		if (flags & DUK_DEFPROP_HAVE_ENUMERABLE) {
			qflags |= JS_PROP_HAS_ENUMERABLE;
			if (flags & DUK_DEFPROP_ENUMERABLE)
				qflags |= JS_PROP_ENUMERABLE;
		}
		if (flags & DUK_DEFPROP_HAVE_VALUE)
			qflags |= JS_PROP_HAS_VALUE;
		if (flags & DUK_DEFPROP_HAVE_GETTER)
			qflags |= JS_PROP_HAS_GET;
		if (flags & DUK_DEFPROP_HAVE_SETTER)
			qflags |= JS_PROP_HAS_SET;

		if (JS_DefineProperty(ctx->qctx, obj, prop, val, getter, setter,
				      qflags) < 0) {
			JS_FreeValue(ctx->qctx, JS_GetException(ctx->qctx));
		}
	}

	JS_FreeAtom(ctx->qctx, prop);
	JS_FreeValue(ctx->qctx, val);
	JS_FreeValue(ctx->qctx, getter);
	JS_FreeValue(ctx->qctx, setter);
	JS_FreeValue(ctx->qctx, obj);
}

DUK_EXTERNAL duk_bool_t duk_get_global_lstring(duk_context *ctx,
					       const char *key,
					       duk_size_t key_len)
{
	bool is_hidden = false;
	JSAtom atom = duk_qjs_key_to_atom(ctx, key, key_len, &is_hidden);
	duk_bool_t ret = duk_qjs_get_prop_atom(ctx, ctx->global_obj, atom);
	JS_FreeAtom(ctx->qctx, atom);
	return ret;
}

DUK_EXTERNAL duk_bool_t duk_get_global_string(duk_context *ctx, const char *key)
{
	return duk_get_global_lstring(ctx, key, key ? strlen(key) : 0);
}

DUK_EXTERNAL duk_bool_t duk_get_global_literal_raw(duk_context *ctx,
						   const char *key,
						   duk_size_t key_len)
{
	return duk_get_global_lstring(ctx, key, key_len);
}

DUK_EXTERNAL duk_bool_t duk_put_global_lstring(duk_context *ctx,
					       const char *key,
					       duk_size_t key_len)
{
	if (ctx->top <= ctx->frame_base)
		return 0;
	ctx->top--;
	JSValue val = ctx->stack[ctx->top];
	bool is_hidden = false;
	JSAtom atom = duk_qjs_key_to_atom(ctx, key, key_len, &is_hidden);
	duk_bool_t ret =
		duk_qjs_put_prop_atom(ctx, ctx->global_obj, atom, val,
				      is_hidden);
	JS_FreeAtom(ctx->qctx, atom);
	return ret;
}

DUK_EXTERNAL duk_bool_t duk_put_global_string(duk_context *ctx, const char *key)
{
	return duk_put_global_lstring(ctx, key, key ? strlen(key) : 0);
}

DUK_EXTERNAL duk_bool_t duk_put_global_literal_raw(duk_context *ctx,
						   const char *key,
						   duk_size_t key_len)
{
	return duk_put_global_lstring(ctx, key, key_len);
}

DUK_EXTERNAL void duk_get_prototype(duk_context *ctx, duk_idx_t idx)
{
	duk_idx_t abs_idx = duk_qjs_abs_idx(ctx, idx);
	if (abs_idx < 0 || !JS_IsObject(ctx->stack[abs_idx])) {
		duk_push_undefined(ctx);
		return;
	}
	JSValue proto = JS_GetPrototype(ctx->qctx, ctx->stack[abs_idx]);
	if (JS_IsNull(proto) || JS_IsException(proto)) {
		if (JS_IsException(proto))
			JS_FreeValue(ctx->qctx, JS_GetException(ctx->qctx));
		JS_FreeValue(ctx->qctx, proto);
		duk_push_undefined(ctx);
		return;
	}
	duk_qjs_push_val(ctx, proto);
}

static duk_c_function duk_qjs_find_finalizer_in_chain(JSContext *qctx,
						      JSValueConst proto)
{
	JSValue cur = JS_DupValue(qctx, proto);
	duk_c_function found = NULL;
	while (JS_IsObject(cur)) {
		if (JS_GetClassID(cur) == g_duk_object_class_id) {
			struct duk_qjs_obj_meta *m =
				(struct duk_qjs_obj_meta *)JS_GetOpaque(
					cur, g_duk_object_class_id);
			if (m != NULL && m->finalizer != NULL) {
				found = m->finalizer;
				break;
			}
		}
		JSValue next = JS_GetPrototype(qctx, cur);
		JS_FreeValue(qctx, cur);
		cur = next;
	}
	JS_FreeValue(qctx, cur);
	return found;
}

DUK_EXTERNAL void duk_set_prototype(duk_context *ctx, duk_idx_t idx)
{
	duk_idx_t abs_obj = duk_qjs_abs_idx(ctx, idx);
	if (abs_obj < 0 || ctx->top <= ctx->frame_base)
		return;
	JSValue obj = JS_DupValue(ctx->qctx, ctx->stack[abs_obj]);
	ctx->top--;
	JSValue proto = ctx->stack[ctx->top];
	if (JS_IsUndefined(proto))
		proto = JS_NULL;
	if (JS_IsObject(obj)) {
		JS_SetPrototype(ctx->qctx, obj, proto);
		if (JS_GetClassID(obj) == g_duk_object_class_id &&
		    JS_IsObject(proto)) {
			duk_c_function fin =
				duk_qjs_find_finalizer_in_chain(ctx->qctx, proto);
			if (fin != NULL) {
				struct duk_qjs_obj_meta *meta =
					duk_qjs_get_or_create_meta(ctx, obj);
				if (meta != NULL && meta->finalizer == NULL)
					meta->finalizer = fin;
			}
		}
	}
	JS_FreeValue(ctx->qctx, proto);
	JS_FreeValue(ctx->qctx, obj);
}

DUK_EXTERNAL void duk_get_finalizer(duk_context *ctx, duk_idx_t idx)
{
	duk_get_prop_lstring(ctx, idx, DUK_QJS_FINALIZER_PROP,
			     sizeof(DUK_QJS_FINALIZER_PROP) - 1);
}

DUK_EXTERNAL void duk_set_finalizer(duk_context *ctx, duk_idx_t idx)
{
	duk_idx_t abs_obj = duk_qjs_abs_idx(ctx, idx);
	if (abs_obj < 0 || ctx->top <= ctx->frame_base)
		return;
	duk_c_function fin_fn = duk_get_c_function(ctx, -1);
	JSValue obj = ctx->stack[abs_obj];
	if (JS_IsObject(obj) && JS_GetClassID(obj) == g_duk_object_class_id) {
		struct duk_qjs_obj_meta *meta =
			duk_qjs_get_or_create_meta(ctx, obj);
		if (meta != NULL)
			meta->finalizer = fin_fn;
	}
	duk_put_prop_lstring(ctx, idx, DUK_QJS_FINALIZER_PROP,
			     sizeof(DUK_QJS_FINALIZER_PROP) - 1);
}

/*
 *  Function Calls, Safe Calls, Compilation, and Evaluation
 */

static void duk_qjs_drain_jobs(duk_context *ctx)
{
	JSContext *job_ctx = NULL;
	while (JS_IsJobPending(ctx->heap->rt)) {
		if (JS_ExecutePendingJob(ctx->heap->rt, &job_ctx) < 0) {
			if (job_ctx != NULL) {
				JS_FreeValue(job_ctx,
					     JS_GetException(job_ctx));
			}
		}
	}
}

static duk_int_t duk_qjs_invoke_internal(duk_context *ctx, duk_idx_t nargs,
					 bool has_this, bool is_new,
					 bool is_safe)
{
	duk_idx_t total_pop = nargs + 1 + (has_this ? 1 : 0);
	duk_idx_t base_idx = ctx->top - total_pop;
	if (base_idx < ctx->frame_base)
		base_idx = ctx->frame_base;

	JSValue func = JS_DupValue(ctx->qctx, ctx->stack[base_idx]);
	JSValue this_val =
		has_this ? JS_DupValue(ctx->qctx, ctx->stack[base_idx + 1])
			 : JS_UNDEFINED;
	duk_idx_t arg_start = base_idx + 1 + (has_this ? 1 : 0);

	JSValue *argv = NULL;
	if (nargs > 0) {
		argv = (JSValue *)malloc((size_t)nargs * sizeof(JSValue));
		for (duk_idx_t i = 0; i < nargs; i++)
			argv[i] = JS_DupValue(ctx->qctx,
					      ctx->stack[arg_start + i]);
	}

	duk_pop_n(ctx, ctx->top - base_idx);

	JSValue res;
	if (JS_VALUE_GET_TAG(func) == JS_TAG_FUNCTION_BYTECODE) {
		res = JS_EvalFunction(ctx->qctx, JS_DupValue(ctx->qctx, func));
	} else if (is_new) {
		res = JS_CallConstructor(ctx->qctx, func, nargs, argv);
	} else {
		res = JS_Call(ctx->qctx, func, this_val, nargs, argv);
	}

	if (argv != NULL) {
		for (duk_idx_t i = 0; i < nargs; i++)
			JS_FreeValue(ctx->qctx, argv[i]);
		free(argv);
	}
	JS_FreeValue(ctx->qctx, this_val);
	JS_FreeValue(ctx->qctx, func);

	if (JS_IsException(res)) {
		JSValue exc = JS_GetException(ctx->qctx);
		duk_qjs_push_val(ctx, exc);
		if (is_safe)
			return DUK_EXEC_ERROR;
		duk_throw_raw(ctx);
	}

	if (ctx->catch_top == NULL)
		duk_qjs_drain_jobs(ctx);
	duk_qjs_push_val(ctx, res);
	return DUK_EXEC_SUCCESS;
}

DUK_EXTERNAL void duk_call(duk_context *ctx, duk_idx_t nargs)
{
	(void)duk_qjs_invoke_internal(ctx, nargs, false, false, false);
}

DUK_EXTERNAL void duk_call_method(duk_context *ctx, duk_idx_t nargs)
{
	(void)duk_qjs_invoke_internal(ctx, nargs, true, false, false);
}

DUK_EXTERNAL void duk_call_prop(duk_context *ctx, duk_idx_t obj_idx,
				duk_idx_t nargs)
{
	if (duk_pcall_prop(ctx, obj_idx, nargs) != DUK_EXEC_SUCCESS)
		duk_throw_raw(ctx);
}

DUK_EXTERNAL duk_int_t duk_pcall(duk_context *ctx, duk_idx_t nargs)
{
	return duk_qjs_invoke_internal(ctx, nargs, false, false, true);
}

DUK_EXTERNAL duk_int_t duk_pcall_method(duk_context *ctx, duk_idx_t nargs)
{
	return duk_qjs_invoke_internal(ctx, nargs, true, false, true);
}

DUK_EXTERNAL duk_int_t duk_pcall_prop(duk_context *ctx, duk_idx_t obj_idx,
				      duk_idx_t nargs)
{
	duk_idx_t abs_obj = duk_qjs_abs_idx(ctx, obj_idx);
	duk_idx_t key_pos = ctx->top - nargs - 1;
	if (abs_obj < 0 || key_pos < ctx->frame_base)
		return DUK_EXEC_ERROR;

	JSValue obj = JS_DupValue(ctx->qctx, ctx->stack[abs_obj]);
	bool is_hidden = false;
	JSAtom atom =
		duk_qjs_val_to_atom(ctx, ctx->stack[key_pos], &is_hidden);
	JSValue fn = JS_GetProperty(ctx->qctx, obj, atom);
	JS_FreeAtom(ctx->qctx, atom);

	if (JS_IsException(fn)) {
		JS_FreeValue(ctx->qctx, obj);
		duk_pop_n(ctx, nargs + 1);
		duk_qjs_push_val(ctx, JS_GetException(ctx->qctx));
		return DUK_EXEC_ERROR;
	}

	/* Replace [key, args...] with [fn, obj, args...] */
	JS_FreeValue(ctx->qctx, ctx->stack[key_pos]);
	ctx->stack[key_pos] = fn;
	duk_qjs_push_val(ctx, obj);
	if (nargs > 0) {
		JSValue top_obj = ctx->stack[ctx->top - 1];
		memmove(&ctx->stack[key_pos + 2], &ctx->stack[key_pos + 1],
			(size_t)nargs * sizeof(JSValue));
		ctx->stack[key_pos + 1] = top_obj;
	}
	return duk_qjs_invoke_internal(ctx, nargs, true, false, true);
}

DUK_EXTERNAL void duk_new(duk_context *ctx, duk_idx_t nargs)
{
	(void)duk_qjs_invoke_internal(ctx, nargs, false, true, false);
}

DUK_EXTERNAL duk_int_t duk_pnew(duk_context *ctx, duk_idx_t nargs)
{
	return duk_qjs_invoke_internal(ctx, nargs, false, true, true);
}

DUK_EXTERNAL duk_int_t duk_safe_call(duk_context *ctx,
				     duk_safe_call_function func, void *udata,
				     duk_idx_t nargs, duk_idx_t nrets)
{
	duk_idx_t saved_base = ctx->frame_base;
	duk_idx_t retbase = ctx->top - nargs;
	if (retbase < ctx->frame_base)
		retbase = ctx->frame_base;

	struct duk_qjs_catch_point cp;
	cp.prev = ctx->catch_top;
	ctx->catch_top = &cp;

	if (setjmp(cp.jb) == 0) {
		duk_ret_t rc = func(ctx, udata);
		ctx->catch_top = cp.prev;
		ctx->frame_base = saved_base;

		if (rc < 0) {
			va_list empty_ap;
			memset(&empty_ap, 0, sizeof(empty_ap));
			JSValue err_obj = duk_qjs_make_error_va(
				ctx, -rc, NULL, 0, NULL, empty_ap);
			while (ctx->top > retbase) {
				ctx->top--;
				JS_FreeValue(ctx->qctx, ctx->stack[ctx->top]);
			}
			if (nrets > 0) {
				duk_qjs_push_val(ctx, err_obj);
				for (duk_idx_t i = 1; i < nrets; i++)
					duk_push_undefined(ctx);
			} else {
				JS_FreeValue(ctx->qctx, err_obj);
			}
			return DUK_EXEC_ERROR;
		}

		duk_idx_t actual_rets = rc;
		if (ctx->top - retbase < actual_rets)
			actual_rets = ctx->top - retbase;

		duk_idx_t keep_rets = actual_rets < nrets ? actual_rets : nrets;
		duk_idx_t src_start = ctx->top - actual_rets;

		for (duk_idx_t i = retbase; i < src_start; i++)
			JS_FreeValue(ctx->qctx, ctx->stack[i]);
		for (duk_idx_t i = src_start + keep_rets; i < ctx->top; i++)
			JS_FreeValue(ctx->qctx, ctx->stack[i]);

		if (keep_rets > 0 && src_start != retbase) {
			memmove(&ctx->stack[retbase], &ctx->stack[src_start],
				(size_t)keep_rets * sizeof(JSValue));
		}
		ctx->top = retbase + keep_rets;
		while (ctx->top < retbase + nrets)
			duk_push_undefined(ctx);

		return DUK_EXEC_SUCCESS;
	} else {
		ctx->catch_top = cp.prev;
		ctx->frame_base = saved_base;

		JSValue err_val = JS_UNDEFINED;
		if (ctx->has_thrown) {
			err_val = ctx->thrown_val;
			ctx->thrown_val = JS_UNDEFINED;
			ctx->has_thrown = false;
		} else {
			err_val = JS_GetException(ctx->qctx);
		}

		while (ctx->top > retbase) {
			ctx->top--;
			JS_FreeValue(ctx->qctx, ctx->stack[ctx->top]);
		}
		if (nrets > 0) {
			duk_qjs_push_val(ctx, err_val);
			for (duk_idx_t i = 1; i < nrets; i++)
				duk_push_undefined(ctx);
		} else {
			JS_FreeValue(ctx->qctx, err_val);
		}
		return DUK_EXEC_ERROR;
	}
}

static duk_int_t duk_qjs_compile_or_eval(duk_context *ctx,
					 const char *src_buffer,
					 duk_size_t src_length,
					 duk_uint_t flags, bool is_eval_call)
{
	duk_idx_t nargs = (duk_idx_t)(flags & 0x07U);
	bool no_source = (flags & DUK_COMPILE_NOSOURCE) != 0;
	bool no_filename = (flags & DUK_COMPILE_NOFILENAME) != 0;
	bool use_strlen = (flags & DUK_COMPILE_STRLEN) != 0;
	bool is_func = (flags & DUK_COMPILE_FUNCTION) != 0;
	bool is_safe = (flags & DUK_COMPILE_SAFE) != 0;
	bool no_result = (flags & DUK_COMPILE_NORESULT) != 0;

	char *filename_copy = NULL;
	if (!no_filename && nargs >= 1 && ctx->top > ctx->frame_base) {
		size_t fn_len = 0;
		const char *fn_str = JS_ToCStringLen(
			ctx->qctx, &fn_len, ctx->stack[ctx->top - 1]);
		if (fn_str != NULL) {
			filename_copy = (char *)malloc(fn_len + 1);
			if (filename_copy != NULL) {
				memcpy(filename_copy, fn_str, fn_len);
				filename_copy[fn_len] = '\0';
			}
			JS_FreeCString(ctx->qctx, fn_str);
		}
	}
	const char *filename = filename_copy ? filename_copy : "input";

	char *src_copy = NULL;
	size_t src_len = 0;
	if (no_source) {
		if (src_buffer != NULL) {
			src_len = use_strlen ? strlen(src_buffer) : src_length;
			src_copy = (char *)malloc(src_len + 1);
			if (src_copy != NULL) {
				memcpy(src_copy, src_buffer, src_len);
				src_copy[src_len] = '\0';
			}
		}
	} else {
		duk_idx_t src_idx = ctx->top - nargs;
		if (src_idx >= ctx->frame_base && src_idx < ctx->top) {
			const char *s = JS_ToCStringLen(ctx->qctx, &src_len,
							ctx->stack[src_idx]);
			if (s != NULL) {
				src_copy = (char *)malloc(src_len + 1);
				if (src_copy != NULL) {
					memcpy(src_copy, s, src_len);
					src_copy[src_len] = '\0';
				}
				JS_FreeCString(ctx->qctx, s);
			}
		}
	}

	if (nargs > 0)
		duk_pop_n(ctx, nargs);

	if (src_copy == NULL) {
		src_copy = (char *)calloc(1, 1);
		src_len = 0;
	}

	JSValue res;
	if (is_func) {
		size_t wrapped_len = src_len + 2;
		char *wrapped = (char *)malloc(wrapped_len + 1);
		wrapped[0] = '(';
		memcpy(wrapped + 1, src_copy, src_len);
		wrapped[1 + src_len] = ')';
		wrapped[wrapped_len] = '\0';
		res = JS_Eval(ctx->qctx, wrapped, wrapped_len, filename,
			      JS_EVAL_TYPE_GLOBAL);
		free(wrapped);
	} else if (is_eval_call) {
		res = JS_Eval(ctx->qctx, src_copy, src_len, filename,
			      JS_EVAL_TYPE_GLOBAL);
		if (!JS_IsException(res))
			duk_qjs_drain_jobs(ctx);
	} else {
		res = JS_Eval(ctx->qctx, src_copy, src_len, filename,
			      JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_COMPILE_ONLY);
	}

	free(src_copy);
	free(filename_copy);

	if (JS_IsException(res)) {
		JSValue exc = JS_GetException(ctx->qctx);
		duk_qjs_push_val(ctx, exc);
		if (is_safe)
			return DUK_EXEC_ERROR;
		duk_throw_raw(ctx);
	}

	if (no_result) {
		JS_FreeValue(ctx->qctx, res);
	} else {
		duk_qjs_push_val(ctx, res);
	}
	return DUK_EXEC_SUCCESS;
}

DUK_EXTERNAL duk_int_t duk_compile_raw(duk_context *ctx, const char *src_buffer,
				       duk_size_t src_length, duk_uint_t flags)
{
	return duk_qjs_compile_or_eval(ctx, src_buffer, src_length, flags,
				       false);
}

DUK_EXTERNAL duk_int_t duk_eval_raw(duk_context *ctx, const char *src_buffer,
				    duk_size_t src_length, duk_uint_t flags)
{
	return duk_qjs_compile_or_eval(ctx, src_buffer, src_length, flags,
				       true);
}
