--TEST--
tests Fiber::yield out of fiber internal call
--SKIPIF--
<?php
if (!extension_loaded('fiber')) {
	echo 'skip';
}
?>
--FILE--
<?php
Fiber::yield();
?>
--EXPECTF--
Fatal error: Uncaught Error: Cannot call Fiber::yield out of Fiber in %s/tests/009.php:%d
Stack trace:
#0 %s/tests/009.php(%d): Fiber::yield()
#1 {main}
  thrown in %s/tests/009.php on line %d
