/*
 *  Function built-ins
 */

#include "duk_internal.h"

/* Needed even when Function built-in is disabled. */
DUK_INTERNAL duk_ret_t duk_bi_function_prototype(duk_context *ctx) {
	/* ignore arguments, return undefined (E5 Section 15.3.4) */
	DUK_UNREF(ctx);
	return 0;
}

#if defined(DUK_USE_FUNCTION_BUILTIN)
DUK_INTERNAL duk_ret_t duk_bi_function_constructor(duk_context *ctx) {
	duk_hthread *thr = (duk_hthread *) ctx;
	duk_hstring *h_sourcecode;
	duk_idx_t nargs;
	duk_idx_t i;
	duk_small_uint_t comp_flags;
	duk_hcompfunc *func;
	duk_hobject *outer_lex_env;
	duk_hobject *outer_var_env;

	/* normal and constructor calls have identical semantics */

	nargs = duk_get_top(ctx);
	for (i = 0; i < nargs; i++) {
		duk_to_string(ctx, i);  /* Rejects Symbols during coercion. */
	}

	if (nargs == 0) {
		duk_push_hstring_empty(ctx);
		duk_push_hstring_empty(ctx);
	} else if (nargs == 1) {
		/* XXX: cover this with the generic >1 case? */
		duk_push_hstring_empty(ctx);
	} else {
		duk_insert(ctx, 0);   /* [ arg1 ... argN-1 body] -> [body arg1 ... argN-1] */
		duk_push_string(ctx, ",");
		duk_insert(ctx, 1);
		duk_join(ctx, nargs - 1);
	}

	/* [ body formals ], formals is comma separated list that needs to be parsed */

	DUK_ASSERT_TOP(ctx, 2);

	/* XXX: this placeholder is not always correct, but use for now.
	 * It will fail in corner cases; see test-dev-func-cons-args.js.
	 */
	duk_push_string(ctx, "function(");
	duk_dup_1(ctx);
	duk_push_string(ctx, "){");
	duk_dup_0(ctx);
	duk_push_string(ctx, "}");
	duk_concat(ctx, 5);

	/* [ body formals source ] */

	DUK_ASSERT_TOP(ctx, 3);

	/* strictness is not inherited, intentional */
	comp_flags = DUK_COMPILE_FUNCEXPR;

	duk_push_hstring_stridx(ctx, DUK_STRIDX_COMPILE);  /* XXX: copy from caller? */  /* XXX: ignored now */
	h_sourcecode = duk_require_hstring(ctx, -2);  /* no symbol check needed; -2 is concat'd code */
	duk_js_compile(thr,
	               (const duk_uint8_t *) DUK_HSTRING_GET_DATA(h_sourcecode),
	               (duk_size_t) DUK_HSTRING_GET_BYTELEN(h_sourcecode),
	               comp_flags);

	/* Force .name to 'anonymous' (ES2015). */
	duk_push_string(ctx, "anonymous");
	duk_xdef_prop_stridx_short(ctx, -2, DUK_STRIDX_NAME, DUK_PROPDESC_FLAGS_C);

	func = (duk_hcompfunc *) duk_known_hobject(ctx, -1);
	DUK_ASSERT(DUK_HOBJECT_IS_COMPFUNC((duk_hobject *) func));
	DUK_ASSERT(DUK_HOBJECT_HAS_CONSTRUCTABLE((duk_hobject *) func));

	/* [ body formals source template ] */

	/* only outer_lex_env matters, as functions always get a new
	 * variable declaration environment.
	 */

	outer_lex_env = thr->builtins[DUK_BIDX_GLOBAL_ENV];
	outer_var_env = thr->builtins[DUK_BIDX_GLOBAL_ENV];

	duk_js_push_closure(thr, func, outer_var_env, outer_lex_env, 1 /*add_auto_proto*/);

	/* [ body formals source template closure ] */

	return 1;
}
#endif  /* DUK_USE_FUNCTION_BUILTIN */

#if defined(DUK_USE_FUNCTION_BUILTIN)
DUK_INTERNAL duk_ret_t duk_bi_function_prototype_to_string(duk_context *ctx) {
	duk_tval *tv;

	/*
	 *  E5 Section 15.3.4.2 places few requirements on the output of
	 *  this function: the result is implementation dependent, must
	 *  follow FunctionDeclaration syntax (in particular, must have a
	 *  name even for anonymous functions or functions with empty name).
	 *  The output does NOT need to compile into anything useful.
	 *
	 *  E6 Section 19.2.3.5 changes the requirements completely: the
	 *  result must either eval() to a functionally equivalent object
	 *  OR eval() to a SyntaxError.
	 *
	 *  We opt for the SyntaxError approach for now, with a syntax that
	 *  mimics V8's native function syntax:
	 *
	 *      'function cos() { [native code] }'
	 *
	 *  but extended with [ecmascript code], [bound code], and
	 *  [lightfunc code].
	 */

	duk_push_this(ctx);
	tv = DUK_GET_TVAL_NEGIDX(ctx, -1);
	DUK_ASSERT(tv != NULL);

	if (DUK_TVAL_IS_OBJECT(tv)) {
		duk_hobject *obj = DUK_TVAL_GET_OBJECT(tv);
		const char *func_name;

		/* Function name: missing/undefined is mapped to empty string,
		 * otherwise coerce to string.  No handling for invalid identifier
		 * characters or e.g. '{' in the function name.  This doesn't
		 * really matter as long as a SyntaxError results.  Technically
		 * if the name contained a suitable prefix followed by '//' it
		 * might cause the result to parse without error.
		 */
		duk_get_prop_stridx_short(ctx, -1, DUK_STRIDX_NAME);
		if (duk_is_undefined(ctx, -1)) {
			func_name = "";
		} else {
			func_name = duk_to_string(ctx, -1);
			DUK_ASSERT(func_name != NULL);
		}

		if (DUK_HOBJECT_IS_COMPFUNC(obj)) {
			duk_push_sprintf(ctx, "function %s() { [ecmascript code] }", (const char *) func_name);
		} else if (DUK_HOBJECT_IS_NATFUNC(obj)) {
			duk_push_sprintf(ctx, "function %s() { [native code] }", (const char *) func_name);
		} else if (DUK_HOBJECT_IS_BOUNDFUNC(obj)) {
			duk_push_sprintf(ctx, "function %s() { [bound code] }", (const char *) func_name);
		} else {
			goto type_error;
		}
	} else if (DUK_TVAL_IS_LIGHTFUNC(tv)) {
		duk_push_lightfunc_tostring(ctx, tv);
	} else {
		goto type_error;
	}

	return 1;

 type_error:
	DUK_DCERROR_TYPE_INVALID_ARGS((duk_hthread *) ctx);
}
#endif

/* Always present because the native function pointer is needed in call
 * handling.
 */
DUK_INTERNAL duk_ret_t duk_bi_function_prototype_call(duk_context *ctx) {
	/* .call() is dealt with in call handling by simulating its
	 * effects so this function is actually never called.
	 */
	DUK_UNREF(ctx);
	return DUK_RET_TYPE_ERROR;
}

DUK_INTERNAL duk_ret_t duk_bi_function_prototype_apply(duk_context *ctx) {
	/* Like .call(), never actually called. */
	DUK_UNREF(ctx);
	return DUK_RET_TYPE_ERROR;
}

DUK_INTERNAL duk_ret_t duk_bi_reflect_apply(duk_context *ctx) {
	/* Like .call(), never actually called. */
	DUK_UNREF(ctx);
	return DUK_RET_TYPE_ERROR;
}

DUK_INTERNAL duk_ret_t duk_bi_reflect_construct(duk_context *ctx) {
	/* Like .call(), never actually called. */
	DUK_UNREF(ctx);
	return DUK_RET_TYPE_ERROR;
}

#if defined(DUK_USE_FUNCTION_BUILTIN)
/* Create a bound function which points to a target function which may
 * be bound or non-bound.  If the target is bound, the argument lists
 * and 'this' binding of the functions are merged and the resulting
 * function points directly to the non-bound target.
 */
DUK_INTERNAL duk_ret_t duk_bi_function_prototype_bind(duk_context *ctx) {
	duk_hthread *thr = (duk_hthread *) ctx;
	duk_hboundfunc *h_bound;
	duk_idx_t nargs;  /* bound args, not counting 'this' binding */
	duk_idx_t bound_nargs;
	duk_int_t bound_len;
	duk_tval *tv_prevbound;
	duk_idx_t n_prevbound;
	duk_tval *tv_res;
	duk_tval *tv_tmp;

	/* XXX: C API call, e.g. duk_push_bound_function(ctx, target_idx, nargs); */

	DUK_UNREF(thr);

	/* Vararg function, careful arg handling, e.g. thisArg may not
	 * be present.
	 */
	nargs = duk_get_top(ctx) - 1;  /* actual args, not counting 'this' binding */
	if (nargs < 0) {
		nargs++;
		duk_push_undefined(ctx);
	}
	DUK_ASSERT(nargs >= 0);

	/* Limit 'nargs' for bound functions to guarantee arithmetic
	 * below will never wrap.
	 */
	if (nargs > (duk_idx_t) DUK_HBOUNDFUNC_MAX_ARGS) {
		DUK_DCERROR_RANGE_INVALID_COUNT(thr);
	}

	duk_push_this(ctx);
	duk_require_callable(ctx, -1);

	/* [ thisArg arg1 ... argN func ]  (thisArg+args == nargs+1 total) */
	DUK_ASSERT_TOP(ctx, nargs + 2);

	/* Create bound function object. */
	h_bound = duk_push_hboundfunc(ctx);
	DUK_ASSERT(DUK_TVAL_IS_UNDEFINED(&h_bound->target));
	DUK_ASSERT(DUK_TVAL_IS_UNDEFINED(&h_bound->this_binding));
	DUK_ASSERT(h_bound->args == NULL);
	DUK_ASSERT(h_bound->nargs == 0);
	DUK_ASSERT(DUK_HOBJECT_GET_PROTOTYPE(thr->heap, (duk_hobject *) h_bound) == NULL);

	/* [ thisArg arg1 ... argN func boundFunc ] */

	/* If the target is a bound function, argument lists must be
	 * merged.  The 'this' binding closest to the target function
	 * wins because in call handling the 'this' gets replaced over
	 * and over again until we call the non-bound function.
	 */
	tv_prevbound = NULL;
	n_prevbound = 0;
	tv_tmp = DUK_GET_TVAL_POSIDX(ctx, 0);
	DUK_TVAL_SET_TVAL(&h_bound->this_binding, tv_tmp);
	tv_tmp = DUK_GET_TVAL_NEGIDX(ctx, -2);
	DUK_TVAL_SET_TVAL(&h_bound->target, tv_tmp);

	if (DUK_TVAL_IS_OBJECT(tv_tmp)) {
		duk_hobject *h_target;
		duk_hobject *bound_proto;

		h_target = DUK_TVAL_GET_OBJECT(tv_tmp);
		DUK_ASSERT(DUK_HOBJECT_IS_CALLABLE(h_target));

		/* Internal prototype must be copied from the target.
		 * For lightfuncs Function.prototype is used and is already
		 * in place.
		 */
		bound_proto = DUK_HOBJECT_GET_PROTOTYPE(thr->heap, h_target);
		DUK_HOBJECT_SET_PROTOTYPE_INIT_INCREF(thr, (duk_hobject *) h_bound, bound_proto);

		/* The 'strict' flag is copied to get the special [[Get]] of E5.1
		 * Section 15.3.5.4 to apply when a 'caller' value is a strict bound
		 * function.  Not sure if this is correct, because the specification
		 * is a bit ambiguous on this point but it would make sense.
		 */
		/* Strictness is inherited from target. */
		if (DUK_HOBJECT_HAS_STRICT(h_target)) {
			DUK_HOBJECT_SET_STRICT((duk_hobject *) h_bound);
		}

		if (DUK_HOBJECT_HAS_BOUNDFUNC(h_target)) {
			duk_hboundfunc *h_boundtarget;

			h_boundtarget = (duk_hboundfunc *) h_target;

			/* The final function should always be non-bound, unless
			 * there's a bug in the internals.  Assert for it.
			 */
			DUK_ASSERT(DUK_TVAL_IS_LIGHTFUNC(&h_boundtarget->target) ||
			           (DUK_TVAL_IS_OBJECT(&h_boundtarget->target) &&
			            DUK_HOBJECT_IS_CALLABLE(DUK_TVAL_GET_OBJECT(&h_boundtarget->target)) &&
			            !DUK_HOBJECT_IS_BOUNDFUNC(DUK_TVAL_GET_OBJECT(&h_boundtarget->target))));

			DUK_TVAL_SET_TVAL(&h_bound->target, &h_boundtarget->target);
			DUK_TVAL_SET_TVAL(&h_bound->this_binding, &h_boundtarget->this_binding);

			tv_prevbound = h_boundtarget->args;
			n_prevbound = h_boundtarget->nargs;
		}
	} else {
		/* Lightfuncs are always strict. */
		duk_hobject *bound_proto;

		DUK_ASSERT(DUK_TVAL_IS_LIGHTFUNC(tv_tmp));
		DUK_HOBJECT_SET_STRICT((duk_hobject *) h_bound);
		bound_proto = thr->builtins[DUK_BIDX_FUNCTION_PROTOTYPE];
		DUK_HOBJECT_SET_PROTOTYPE_INIT_INCREF(thr, (duk_hobject *) h_bound, bound_proto);
	}

	DUK_TVAL_INCREF(thr, &h_bound->target);  /* old values undefined, no decref needed */
	DUK_TVAL_INCREF(thr, &h_bound->this_binding);

	bound_nargs = n_prevbound + nargs;
	if (bound_nargs > (duk_idx_t) DUK_HBOUNDFUNC_MAX_ARGS) {
		DUK_DCERROR_RANGE_INVALID_COUNT(thr);
	}
	tv_res = (duk_tval *) DUK_ALLOC_CHECKED(thr, ((duk_size_t) bound_nargs) * sizeof(duk_tval));
	DUK_ASSERT(tv_res != NULL);
	DUK_ASSERT(h_bound->args == NULL);
	DUK_ASSERT(h_bound->nargs == 0);
	h_bound->args = tv_res;
	h_bound->nargs = bound_nargs;

	duk_copy_tvals_incref(thr, tv_res, tv_prevbound, n_prevbound);
	duk_copy_tvals_incref(thr, tv_res + n_prevbound, DUK_GET_TVAL_POSIDX(ctx, 1), nargs);

	/* [ thisArg arg1 ... argN func boundFunc ] */

	/* Bound function 'length' property is interesting.
	 * For lightfuncs, simply read the virtual property.
	 */
	duk_get_prop_stridx_short(ctx, -2, DUK_STRIDX_LENGTH);
	bound_len = duk_get_int(ctx, -1);  /* ES2015: no coercion */
	if (bound_len < nargs) {
		bound_len = 0;
	} else {
		bound_len -= nargs;
	}
	if (sizeof(duk_int_t) > 4 && bound_len > (duk_int_t) DUK_UINT32_MAX) {
		bound_len = (duk_int_t) DUK_UINT32_MAX;
	}
	duk_pop(ctx);
	DUK_ASSERT(bound_len >= 0);
	tv_tmp = thr->valstack_top++;
	DUK_ASSERT(DUK_TVAL_IS_UNDEFINED(tv_tmp));
	DUK_ASSERT(!DUK_TVAL_NEEDS_REFCOUNT_UPDATE(tv_tmp));
	DUK_TVAL_SET_U32(tv_tmp, (duk_uint32_t) bound_len);  /* in-place update, fastint */
	duk_xdef_prop_stridx_short(ctx, -2, DUK_STRIDX_LENGTH, DUK_PROPDESC_FLAGS_C);  /* attrs in E6 Section 9.2.4 */

	/* XXX: could these be virtual? */
	/* Caller and arguments must use the same thrower, [[ThrowTypeError]]. */
	duk_xdef_prop_stridx_thrower(ctx, -1, DUK_STRIDX_CALLER);
	duk_xdef_prop_stridx_thrower(ctx, -1, DUK_STRIDX_LC_ARGUMENTS);

	/* Function name and fileName (non-standard). */
	duk_push_string(ctx, "bound ");  /* ES2015 19.2.3.2. */
	duk_get_prop_stridx(ctx, -3, DUK_STRIDX_NAME);
	if (!duk_is_string_notsymbol(ctx, -1)) {
		/* ES2015 has requirement to check that .name of target is a string
		 * (also must check for Symbol); if not, targetName should be the
		 * empty string.  ES2015 19.2.3.2.
		 */
		duk_pop(ctx);
		duk_push_hstring_empty(ctx);
	}
	duk_concat(ctx, 2);
	duk_xdef_prop_stridx_short(ctx, -2, DUK_STRIDX_NAME, DUK_PROPDESC_FLAGS_C);
#if defined(DUK_USE_FUNC_FILENAME_PROPERTY)
	duk_get_prop_stridx_short(ctx, -2, DUK_STRIDX_FILE_NAME);
	duk_xdef_prop_stridx_short(ctx, -2, DUK_STRIDX_FILE_NAME, DUK_PROPDESC_FLAGS_C);
#endif

	DUK_DDD(DUK_DDDPRINT("created bound function: %!iT", (duk_tval *) duk_get_tval(ctx, -1)));

	return 1;
}
#endif  /* DUK_USE_FUNCTION_BUILTIN */

/* %NativeFunctionPrototype% .length getter. */
DUK_INTERNAL duk_ret_t duk_bi_native_function_length(duk_context *ctx) {
	duk_tval *tv;
	duk_hnatfunc *h;
	duk_int16_t func_nargs;

	tv = duk_get_borrowed_this_tval(ctx);
	DUK_ASSERT(tv != NULL);

	if (DUK_TVAL_IS_OBJECT(tv)) {
		h = (duk_hnatfunc *) DUK_TVAL_GET_OBJECT(tv);
		DUK_ASSERT(h != NULL);
		if (!DUK_HOBJECT_IS_NATFUNC((duk_hobject *) h)) {
			goto fail_type;
		}
		func_nargs = h->nargs;
		duk_push_int(ctx, func_nargs == DUK_HNATFUNC_NARGS_VARARGS ? 0 : func_nargs);
	} else if (DUK_TVAL_IS_LIGHTFUNC(tv)) {
		duk_int_t lf_flags;
		duk_int_t lf_len;

		lf_flags = DUK_TVAL_GET_LIGHTFUNC_FLAGS(tv);
		lf_len = DUK_LFUNC_FLAGS_GET_LENGTH(lf_flags);
		duk_push_int(ctx, lf_len);
	} else {
		goto fail_type;
	}
	return 1;

 fail_type:
	DUK_DCERROR_TYPE_INVALID_ARGS((duk_hthread *) ctx);
}

/* %NativeFunctionPrototype% .name getter. */
DUK_INTERNAL duk_ret_t duk_bi_native_function_name(duk_context *ctx) {
	duk_tval *tv;
	duk_hnatfunc *h;

	tv = duk_get_borrowed_this_tval(ctx);
	DUK_ASSERT(tv != NULL);

	if (DUK_TVAL_IS_OBJECT(tv)) {
		h = (duk_hnatfunc *) DUK_TVAL_GET_OBJECT(tv);
		DUK_ASSERT(h != NULL);
		if (!DUK_HOBJECT_IS_NATFUNC((duk_hobject *) h)) {
			goto fail_type;
		}
#if 0
		duk_push_hnatfunc_name(ctx, h);
#endif
		duk_push_hstring_empty(ctx);
	} else if (DUK_TVAL_IS_LIGHTFUNC(tv)) {
		duk_push_lightfunc_name(ctx, tv);
	} else {
		goto fail_type;
	}
	return 1;

 fail_type:
	DUK_DCERROR_TYPE_INVALID_ARGS((duk_hthread *) ctx);
}
