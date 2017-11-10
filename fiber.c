/* fiber extension for PHP (c) 2017 Haitao Lv <php@lvht.net> */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "php.h"
#include "ext/standard/info.h"
#include "php_fiber.h"
#include "zend.h"
#include "zend_API.h"
#include "zend_vm.h"
#include "zend_interfaces.h"
#include "zend_exceptions.h"
#include "zend_closures.h"

/* PHP 7.3 compatibility macro */
#ifndef GC_ADDREF
# define GC_ADDREF(ref) ++GC_REFCOUNT(ref)
# define GC_DELREF(ref) --GC_REFCOUNT(ref)
#endif

ZEND_API zend_class_entry *zend_ce_fiber;
static zend_object_handlers zend_fiber_handlers;

static zend_object *zend_fiber_create(zend_class_entry *ce);
static void zend_fiber_resume(zend_fiber *fiber);
static void zend_fiber_close(zend_fiber *fiber);
static int zend_fiber_call_function(zval *closure, zval *retval_ptr, uint32_t param_count, zval params[]);
static void fiber_interrupt_function(zend_execute_data *execute_data);

static void (*orig_interrupt_function)(zend_execute_data *execute_data);

ZEND_DECLARE_MODULE_GLOBALS(fiber)

/* {{{ PHP_INI
 */
PHP_INI_BEGIN()
    STD_PHP_INI_ENTRY("fiber.stack_size", "4096", PHP_INI_SYSTEM, OnUpdateLong, stack_size, zend_fiber_globals, fiber_globals)
PHP_INI_END()
/* }}} */

/* {{{ zend_fiber_init_globals
 */
static void zend_fiber_init_globals(zend_fiber_globals *fiber_globals)
{
	fiber_globals->current_fiber = NULL;
}
/* }}} */

/* {{{ PHP_RINIT_FUNCTION
 */
PHP_RINIT_FUNCTION(fiber)
{
#if defined(ZTS) && defined(COMPILE_DL_FIBER)
	ZEND_TSRMLS_CACHE_UPDATE();
#endif

	return SUCCESS;
}
/* }}} */

static void zend_fiber_cleanup_unfinished_execution(zend_execute_data *execute_data) /* {{{ */
{
	if (execute_data->opline != execute_data->func->op_array.opcodes) {
		/* -1 required because we want the last run opcode, not the next to-be-run one. */
		uint32_t op_num = execute_data->opline - execute_data->func->op_array.opcodes - 1;

		zend_cleanup_unfinished_execution(execute_data, op_num, 0);
	}
}
/* }}} */

static void zend_fiber_close(zend_fiber *fiber) /* {{{ */
{
	if (UNEXPECTED(fiber->status == ZEND_FIBER_STATUS_DEAD)) {
		return;
	}

	if (fiber->status == ZEND_FIBER_STATUS_FINISHED
		|| UNEXPECTED(fiber->status == ZEND_FIBER_STATUS_UNINITED)) {
		zend_vm_stack original_stack = EG(vm_stack);
		EG(vm_stack) = fiber->stack;

		zend_vm_stack_destroy();

		fiber->stack = NULL;
		fiber->stack_top = NULL;
		fiber->stack_end = NULL;
		EG(vm_stack) = original_stack;

		return;
	}

	zend_vm_stack original_stack = EG(vm_stack);
	original_stack->top = EG(vm_stack_top);
	original_stack->end = EG(vm_stack_end);

	EG(vm_stack_top) = fiber->stack->top;
	EG(vm_stack_end) = fiber->stack->end;
	EG(vm_stack) = fiber->stack;

	zend_execute_data *execute_data = fiber->execute_data;
	zend_execute_data *first_frame = (zend_execute_data*)fiber->first_frame;

	while (execute_data) {
		if (EX_CALL_INFO() & ZEND_CALL_HAS_SYMBOL_TABLE) {
			zend_clean_and_cache_symbol_table(execute_data->symbol_table);
		}

		zend_free_compiled_variables(execute_data);

		if (UNEXPECTED(EX_CALL_INFO() & ZEND_CALL_RELEASE_THIS)) {
			zend_object *object = Z_OBJ(execute_data->This);
			if (UNEXPECTED(EG(exception) != NULL) && (EX_CALL_INFO() & ZEND_CALL_CTOR)) {
				GC_DELREF(object);
				zend_object_store_ctor_failed(object);
			}
			OBJ_RELEASE(object);
		} else if (UNEXPECTED(EX_CALL_INFO() & ZEND_CALL_CLOSURE)) {
			OBJ_RELEASE((zend_object*)execute_data->func->op_array.prototype);
		}

		zend_vm_stack_free_extra_args(execute_data);

		/* A fatal error / die occurred during the fiber execution.
		 * Trying to clean up the stack may not be safe in this case. */
		if (UNEXPECTED(CG(unclean_shutdown))) {
			return;
		}

		/* Some cleanups are only necessary if the fiber was closed
		 * before it could finish execution (reach a return statement). */
		zend_fiber_cleanup_unfinished_execution(execute_data);

		zend_execute_data *call = execute_data;
		execute_data = execute_data->prev_execute_data;

		zend_vm_stack_free_call_frame(call);

		if (call == first_frame) {
			break;
		}
	}

	zend_vm_stack_destroy();

	fiber->stack = NULL;

	EG(vm_stack_top) = original_stack->top;
	EG(vm_stack_end) = original_stack->end;
	EG(vm_stack) = original_stack;
}
/* }}} */

static void zend_fiber_free_storage(zend_object *object) /* {{{ */
{
	zend_fiber *fiber = (zend_fiber*) object;

	zend_fiber_close(fiber);

	zval_ptr_dtor(&fiber->closure);

	zend_object_std_dtor(&fiber->std);
}
/* }}} */

static zend_object *zend_fiber_create(zend_class_entry *ce) /* {{{ */
{
	zend_fiber *fiber;

	fiber = emalloc(sizeof(zend_fiber));
	memset(fiber, 0, sizeof(zend_fiber));

	zend_object_std_init(&fiber->std, ce);
	fiber->std.handlers = &zend_fiber_handlers;

	return &fiber->std;
}
/* }}} */

static void zend_fiber_resume(zend_fiber *fiber) /* {{{ */
{
	/* Backup executor globals */
	zend_execute_data *original_execute_data = EG(current_execute_data);
	zend_vm_stack original_stack = EG(vm_stack);
	zval *original_stack_top = EG(vm_stack_top);
	zval *original_stack_end = EG(vm_stack_end);
	zend_fiber *original_fiber = FIBER_G(current_fiber);

	/* Set executor globals */
	EG(vm_stack_top) = fiber->stack_top;
	EG(vm_stack_end) = fiber->stack_end;
	EG(vm_stack) = fiber->stack;
	FIBER_G(current_fiber) = fiber;

	if (fiber->execute_data) {
		EG(current_execute_data) = fiber->execute_data;
	}

	/* Resume execution */
	fiber->status = ZEND_FIBER_STATUS_RUNNING;

	if (fiber->execute_data) {
		zend_execute_data *first_frame = (zend_execute_data*)fiber->first_frame;
		first_frame->prev_execute_data = original_execute_data;
		fiber->execute_data = NULL;

		if (fiber->send_target && fiber->n_vars) {
			ZVAL_COPY(fiber->send_target, fiber->vars);
		}

		fiber->n_vars = 0;
		zend_execute_ex(EG(current_execute_data));
	} else {
		int n_vars = fiber->n_vars;
		fiber->n_vars = 0;
		zend_fiber_call_function(&fiber->closure, &fiber->value, n_vars, fiber->vars);
	}

	/* If an exception was thrown in the fiber we have to internally
	 * rethrow it in the parent scope.*/
	if (UNEXPECTED(EG(exception) != NULL)) {
		fiber->status = ZEND_FIBER_STATUS_DEAD;

		zend_vm_stack_destroy();

		if (EG(current_execute_data) &&
				EG(current_execute_data)->func &&
				ZEND_USER_CODE(EG(current_execute_data)->func->common.type)) {
			zend_rethrow_exception(EG(current_execute_data));
		}
	} else if (fiber->status != ZEND_FIBER_STATUS_SUSPENDED) {
		fiber->status = ZEND_FIBER_STATUS_FINISHED;
		fiber->first_frame = NULL;
		EG(current_execute_data) = original_execute_data;
		fiber->stack = EG(vm_stack);
		fiber->stack_top = EG(vm_stack_top);
		fiber->stack_end = EG(vm_stack_end);
	}

	EG(vm_stack_top) = original_stack_top;
	EG(vm_stack_end) = original_stack_end;
	EG(vm_stack) = original_stack;
	FIBER_G(current_fiber) = original_fiber;
}
/* }}} */

static int zend_fiber_call_function(zval *closure, zval *retval, uint32_t param_count, zval params[]) /* {{{ */
{
	zend_execute_data *call;
	zend_fcall_info_cache fci_cache;
	zend_function *func;

	ZVAL_UNDEF(retval);

	char *error = NULL;
	zend_is_callable_ex(closure, NULL, IS_CALLABLE_CHECK_SILENT, NULL, &fci_cache, &error);

	func = fci_cache.function_handler;

	call = zend_vm_stack_push_call_frame(ZEND_CALL_TOP_FUNCTION | ZEND_CALL_DYNAMIC,
		func, param_count, fci_cache.called_scope, fci_cache.object);

	FIBER_G(current_fiber)->first_frame = (zval *)call;

	for (uint32_t i=0; i<param_count; i++) {
		zval *param;
		zval *arg = &params[i];

		if (ARG_SHOULD_BE_SENT_BY_REF(func, i + 1)) {
			if (UNEXPECTED(!Z_ISREF_P(arg))) {
				if (!ARG_MAY_BE_SENT_BY_REF(func, i + 1)) {
					/* By-value send is not allowed -- emit a warning,
					 * but still perform the call with a by-value send. */
					zend_error(E_WARNING,
						"Parameter %d to %s%s%s() expected to be a reference, value given", i+1,
						func->common.scope ? ZSTR_VAL(func->common.scope->name) : "",
						func->common.scope ? "::" : "",
						ZSTR_VAL(func->common.function_name));
					if (UNEXPECTED(EG(exception))) {
						ZEND_CALL_NUM_ARGS(call) = i;
						zend_vm_stack_free_args(call);
						zend_vm_stack_free_call_frame(call);
						return FAILURE;
					}
				}
			}
		} else {
			if (Z_ISREF_P(arg) &&
			    !(func->common.fn_flags & ZEND_ACC_CALL_VIA_TRAMPOLINE)) {
				/* don't separate references for __call */
				arg = Z_REFVAL_P(arg);
			}
		}

		param = ZEND_CALL_ARG(call, i+1);
		ZVAL_COPY(param, arg);
	}

	ZEND_ASSERT(GC_TYPE((zend_object*)func->op_array.prototype) == IS_OBJECT);
	GC_ADDREF((zend_object*)func->op_array.prototype);
	ZEND_ADD_CALL_FLAG(call, ZEND_CALL_CLOSURE);

	const zend_op *current_opline_before_exception = EG(opline_before_exception);
	zend_init_func_execute_data(call, &func->op_array, retval);
	zend_execute_ex(call);
	EG(opline_before_exception) = current_opline_before_exception;

	if (UNEXPECTED(EG(exception))) {
		if (UNEXPECTED(!EG(current_execute_data))) {
			zend_throw_exception_internal(NULL);
		} else if (EG(current_execute_data)->func &&
		           ZEND_USER_CODE(EG(current_execute_data)->func->common.type)) {
			zend_rethrow_exception(EG(current_execute_data));
		}
	}

	return SUCCESS;
}
/* }}} */

/* {{{ proto Fiber Fiber::__construct(Closure closure)
   Create a Fiber from a closure. */
ZEND_METHOD(Fiber, __construct)
{
	zval *closure = NULL;
	zend_fiber *fiber = NULL;

	ZEND_PARSE_PARAMETERS_START(0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_OBJECT_OF_CLASS(closure, zend_ce_closure)
	ZEND_PARSE_PARAMETERS_END();

	fiber = (zend_fiber *) Z_OBJ_P(getThis());
	fiber->status = ZEND_FIBER_STATUS_UNINITED;

	zend_vm_stack current_stack = EG(vm_stack);
	zval *current_stack_top = EG(vm_stack_top);
	zval *current_stack_end = EG(vm_stack_end);

	zend_vm_stack_init_ex(FIBER_G(stack_size));

	fiber->stack = EG(vm_stack);
	fiber->stack_top = EG(vm_stack_top);
	fiber->stack_end = EG(vm_stack_end);

	EG(vm_stack) = current_stack;
	EG(vm_stack_top) = current_stack_top;
	EG(vm_stack_end) = current_stack_end;

	if (closure) {
		ZVAL_COPY(&fiber->closure, closure);
		fiber->status = ZEND_FIBER_STATUS_SUSPENDED;
	}
}
/* }}} */

/* {{{ proto mixed Fiber::resume(vars...)
 * Resume and send a value to the fiber */
ZEND_METHOD(Fiber, resume)
{
	zend_fiber *fiber = (zend_fiber *) Z_OBJ_P(getThis());
	if (UNEXPECTED(fiber->status != ZEND_FIBER_STATUS_SUSPENDED)) {
		RETURN_NULL();
	}

	ZEND_PARSE_PARAMETERS_START(0, -1)
		Z_PARAM_VARIADIC('+', fiber->vars, fiber->n_vars)
	ZEND_PARSE_PARAMETERS_END();

	zend_fiber_resume(fiber);

	ZVAL_COPY_VALUE(return_value, &fiber->value);
}
/* }}} */

/* {{{ proto Fiber Fiber::reset(Closure closure)
   Create a Fiber from a closure. */
ZEND_METHOD(Fiber, reset)
{
	zval *closure = NULL;
	zend_fiber *fiber = NULL;

	fiber = (zend_fiber *) Z_OBJ_P(getThis());
	if (fiber->status != ZEND_FIBER_STATUS_FINISHED && fiber->status != ZEND_FIBER_STATUS_UNINITED) {
		zend_throw_error(NULL, "Cannot reset unfinished Fiber");
	}

	ZEND_PARSE_PARAMETERS_START(0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_OBJECT_OF_CLASS(closure, zend_ce_closure)
	ZEND_PARSE_PARAMETERS_END();

	if (closure) {
		zval_ptr_dtor(&fiber->closure);
		ZVAL_COPY(&fiber->closure, closure);
	}

	fiber->status = ZEND_FIBER_STATUS_SUSPENDED;
}
/* }}} */

static void fiber_interrupt_function(zend_execute_data *execute_data)
{
	const zend_op *opline;
	zend_fiber *fiber;

	if (!FIBER_G(pending_interrupt)) {
		if (orig_interrupt_function) {
			orig_interrupt_function(execute_data);
		}
		return;
	}
	FIBER_G(pending_interrupt) = 0;

	opline = EX(opline);

	fiber = FIBER_G(current_fiber);
	fiber->execute_data = execute_data;
	fiber->stack = EG(vm_stack);
	fiber->stack_top = EG(vm_stack_top);
	fiber->stack_end = EG(vm_stack_end);

	fiber->status = ZEND_FIBER_STATUS_SUSPENDED;

#define RETURN_VALUE_USED(opline) \
	((opline)->result_type != IS_UNUSED)

	if (RETURN_VALUE_USED(opline-1)) {
		fiber->send_target = EX_VAR((opline-1)->result.var);
		ZVAL_NULL(fiber->send_target);
	} else {
		fiber->send_target = NULL;
	}

	if (orig_interrupt_function) {
		orig_interrupt_function(execute_data);
	}
}

/* {{{ proto mixed Fiber::yield(vars...)
 * Pause and return a value to the fiber */
ZEND_METHOD(Fiber, yield)
{
	zend_fiber *fiber = FIBER_G(current_fiber);

	if (!fiber) {
		zend_throw_error(NULL, "Cannot call Fiber::yield out of Fiber");
		return;
	}

	if (fiber->n_vars) {
		zend_throw_error(NULL, "Cannot call Fiber::yield in internal call");
		return;
	}

	ZEND_PARSE_PARAMETERS_START(0, -1)
		Z_PARAM_VARIADIC('+', fiber->vars, fiber->n_vars)
	ZEND_PARSE_PARAMETERS_END();

	if (fiber->n_vars) {
		ZVAL_COPY(&fiber->value, fiber->vars);
	}

	FIBER_G(pending_interrupt) = 1;
	EG(vm_interrupt) = 2;
}
/* }}} */

/* {{{ proto void Fiber::status()
 * Get fiber's status */
ZEND_METHOD(Fiber, status)
{
	zend_fiber *fiber = (zend_fiber *) Z_OBJ_P(getThis());
	RETURN_LONG(fiber->status);
}
/* }}} */

/* {{{ proto void Fiber::__wakeup()
 * Throws an Exception as fibers can't be serialized */
ZEND_METHOD(Fiber, __wakeup)
{
	/* Just specifying the zend_class_unserialize_deny handler is not enough,
	 * because it is only invoked for C unserialization. For O the error has
	 * to be thrown in __wakeup. */

	if (zend_parse_parameters_none() == FAILURE) {
		return;
	}

	zend_throw_exception(NULL, "Unserialization of 'Fiber' is not allowed", 0);
}
/* }}} */

ZEND_BEGIN_ARG_INFO_EX(arginfo_fiber_create, 0, 0, 0)
	ZEND_ARG_OBJ_INFO(0, closure, Closure, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_fiber_void, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_fiber_resume, 0, 0, 0)
	ZEND_ARG_VARIADIC_INFO(0, vars)
ZEND_END_ARG_INFO()

static const zend_function_entry fiber_functions[] = {
	ZEND_ME(Fiber, __construct, arginfo_fiber_create, ZEND_ACC_PUBLIC)
	ZEND_ME(Fiber, reset,       arginfo_fiber_create, ZEND_ACC_PUBLIC)
	ZEND_ME(Fiber, resume,      arginfo_fiber_resume, ZEND_ACC_PUBLIC)
	ZEND_ME(Fiber, yield,       arginfo_fiber_resume, ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
	ZEND_ME(Fiber, status,      arginfo_fiber_void,   ZEND_ACC_PUBLIC)
	ZEND_ME(Fiber, __wakeup,    arginfo_fiber_void,   ZEND_ACC_PUBLIC)
	ZEND_FE_END
};

 /* {{{ PHP_MINIT_FUNCTION
  **/
PHP_MINIT_FUNCTION(fiber)
{
	zend_class_entry ce;

	INIT_CLASS_ENTRY(ce, "Fiber", fiber_functions);
	zend_ce_fiber = zend_register_internal_class(&ce);
	zend_ce_fiber->ce_flags |= ZEND_ACC_FINAL;
	zend_ce_fiber->create_object = zend_fiber_create;
	zend_ce_fiber->serialize = zend_class_serialize_deny;
	zend_ce_fiber->unserialize = zend_class_unserialize_deny;

	memcpy(&zend_fiber_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
	zend_fiber_handlers.free_obj = zend_fiber_free_storage;
	zend_fiber_handlers.clone_obj = NULL;

	REGISTER_FIBER_CLASS_CONST_LONG("STATUS_SUSPENDED", (zend_long)ZEND_FIBER_STATUS_SUSPENDED);
	REGISTER_FIBER_CLASS_CONST_LONG("STATUS_RUNNING", (zend_long)ZEND_FIBER_STATUS_RUNNING);
	REGISTER_FIBER_CLASS_CONST_LONG("STATUS_FINISHED", (zend_long)ZEND_FIBER_STATUS_FINISHED);
	REGISTER_FIBER_CLASS_CONST_LONG("STATUS_DEAD", (zend_long)ZEND_FIBER_STATUS_DEAD);

	ZEND_INIT_MODULE_GLOBALS(fiber, zend_fiber_init_globals, NULL);

	REGISTER_INI_ENTRIES();

	if (FIBER_G(stack_size) < 0) {
		FIBER_G(stack_size) = 4096;
	}

	orig_interrupt_function = zend_interrupt_function;
	zend_interrupt_function = fiber_interrupt_function;

	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MSHUTDOWN_FUNCTION
 */
PHP_MSHUTDOWN_FUNCTION(fiber)
{
	UNREGISTER_INI_ENTRIES();

	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MINFO_FUNCTION
 */
PHP_MINFO_FUNCTION(fiber)
{
	php_info_print_table_start();
	php_info_print_table_header(2, "fiber support", "enabled");
	php_info_print_table_end();

	DISPLAY_INI_ENTRIES();
}
/* }}} */

/* {{{ fiber_module_entry
 */
zend_module_entry fiber_module_entry = {
	STANDARD_MODULE_HEADER,
	"fiber",			/* Extension name */
	NULL,				/* zend_function_entry */
	PHP_MINIT(fiber),		/* PHP_MINIT - Module initialization */
	PHP_MSHUTDOWN(fiber),		/* PHP_MSHUTDOWN - Module shutdown */
	PHP_RINIT(fiber),		/* PHP_RINIT - Request initialization */
	NULL,				/* PHP_RSHUTDOWN - Request shutdown */
	PHP_MINFO(fiber),		/* PHP_MINFO - Module info */
	PHP_FIBER_VERSION,		/* Version */
	STANDARD_MODULE_PROPERTIES
};
/* }}} */

#ifdef COMPILE_DL_FIBER
# ifdef ZTS
ZEND_TSRMLS_CACHE_DEFINE()
# endif
ZEND_GET_MODULE(fiber)
#endif
