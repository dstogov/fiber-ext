--TEST--
tests Fiber stackful resume value
--SKIPIF--
<?php
if (!extension_loaded('fiber')) {
	echo 'skip';
}
?>
--FILE--
<?php
function foo()
{
    return Fiber::yield();
}
$f = new Fiber(function () {
    echo foo();
});
$f->resume();
$f->resume('foo');
?>
--EXPECTF--
foo
