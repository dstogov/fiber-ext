--TEST--
tests Fiber::yield without assign
--SKIPIF--
<?php
if (!extension_loaded('fiber')) {
	echo 'skip';
}
?>
--FILE--
<?php
$f = new Fiber(function ($a) {
    Fiber::yield(1);
    Fiber::yield(1);
});

$f->resume(1);
$f->resume(1);
$f->resume(1);
?>
--EXPECTF--
