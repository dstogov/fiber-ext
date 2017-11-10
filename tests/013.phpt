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

$bar = function () {
    echo "a\n";
    Fiber::yield();
    echo "b\n";
    Fiber::yield();
    baz();
};

$f = new Fiber(function ($a) {
    Fiber::yield(1);
    Fiber::yield(1);
});

$f = new Fiber($foo);
$f->resume();
$f->resume();
$f->resume();
$f->resume();
$f->reset($bar);
$f->resume();
$f->resume();
$f->resume();
$f->resume();
?>
--EXPECTF--
1
2
你
好
a
b
你
好
