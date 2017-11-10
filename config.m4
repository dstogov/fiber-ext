PHP_ARG_ENABLE(fiber, whether to enable fiber support,
[  --enable-fiber          Enable fiber support], no)

if test "$PHP_FIBER" != "no"; then
  AC_DEFINE(HAVE_FIBER, 1, [ Have fiber support ])
  PHP_NEW_EXTENSION(fiber, fiber.c, $ext_shared)
fi
