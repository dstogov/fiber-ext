--TEST--
tests Fiber::reset
--SKIPIF--
<?php
if (!extension_loaded('fiber')) {
	echo 'skip';
}
?>
--FILE--
<?php
function baz()
{
    echo "你\n";
    Fiber::yield();
    echo "好\n";
}

$foo = function () {
    echo "1\n";
    Fiber::yield();
    echo "2\n";
    Fiber::yield();
    baz();
};

$f = new Fiber($foo);
$f->reset();
?>
--EXPECTF--
Fatal error: Uncaught Error: Cannot reset unfinished Fiber in %s/011.php:18
Stack trace:
#0 %s/011.php(18): Fiber->reset()
#1 {main}
  thrown in %s/011.php on line 18
