--TEST--
tests Fiber status
--SKIPIF--
<?php
if (!extension_loaded('fiber')) {
	echo 'skip';
}
?>
--FILE--
<?php
$f = new Fiber(function () {
    Fiber::yield();
});

var_dump($f->status() == Fiber::STATUS_INIT);
$f->resume();
var_dump($f->status() == Fiber::STATUS_SUSPENDED);
$f->resume();
var_dump($f->status() == Fiber::STATUS_FINISHED);
$f->resume();
var_dump($f->status() == Fiber::STATUS_FINISHED);

$f = new Fiber(function () {
    throw new Exception;
});
try {
    $f->resume();
} catch (Exception $e) {
}
var_dump($f->status() == Fiber::STATUS_DEAD);
?>
--EXPECTF--
bool(true)
bool(true)
bool(true)

Warning: Attempt to resume non suspended Fiber in %s006.php on line %d
bool(true)
bool(true)
