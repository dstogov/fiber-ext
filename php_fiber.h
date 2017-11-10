/* fiber extension for PHP (c) 2017 Haitao Lv <php@lvht.net> */

#ifndef PHP_FIBER_H
# define PHP_FIBER_H

extern zend_module_entry fiber_module_entry;
# define phpext_fiber_ptr &fiber_module_entry

# define PHP_FIBER_VERSION "0.1.0"

# if defined(ZTS) && defined(COMPILE_DL_FIBER)
ZEND_TSRMLS_CACHE_EXTERN()
# endif

extern ZEND_API zend_class_entry *zend_ce_fiber;

typedef struct _zend_fiber zend_fiber;

struct _zend_fiber {
	zend_object std;

	zval closure;

	/* The suspended execution context. */
	zend_execute_data *execute_data;

	/* The separate stack used by fiber */
	zend_vm_stack stack;
	zval *first_frame;
	zval *stack_top;
	zval *stack_end;

	zval value;
	zval *send_target;

	zval *vars;
	int n_vars;

	zend_uchar status;
};

static const zend_uchar ZEND_FIBER_STATUS_UNINITED  = 0;
static const zend_uchar ZEND_FIBER_STATUS_SUSPENDED = 1;
static const zend_uchar ZEND_FIBER_STATUS_RUNNING   = 2;
static const zend_uchar ZEND_FIBER_STATUS_FINISHED  = 3;
static const zend_uchar ZEND_FIBER_STATUS_DEAD      = 4;

#define REGISTER_FIBER_CLASS_CONST_LONG(const_name, value) \
	zend_declare_class_constant_long(zend_ce_fiber, const_name, sizeof(const_name)-1, (zend_long)value);

ZEND_BEGIN_MODULE_GLOBALS(fiber)
	zend_fiber *current_fiber;
	zend_long   stack_size;
	volatile zend_bool pending_interrupt;
ZEND_END_MODULE_GLOBALS(fiber)

#define FIBER_G(v) ZEND_MODULE_GLOBALS_ACCESSOR(fiber, v)

#endif	/* PHP_FIBER_H */
