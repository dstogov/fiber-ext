--TEST--
tests Fiber stackful yield value
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
    Fiber::yield(1);
}

$f = new Fiber(function () {
    foo();
    Fiber::yield(2);
});
var_dump($f->resume());
var_dump($f->resume());
?>
--EXPECTF--
int(1)
int(2)
