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
function bar()
{
    $a = null;
    Fiber::yield();
    $a->foo();
}
function foo()
{
    bar();
}
$f = new Fiber(function () {
    foo();
});

$f->resume();
$f->resume();

?>
--EXPECTF--
Fatal error: Uncaught Error: Call to a member function foo() on null in %s/007.php:6
Stack trace:
#0 %s/007.php(10): bar()
#1 %s/007.php(13): foo()
#2 [internal function]: {closure}()
#3 %s/007.php(17): Fiber->resume()
#4 {main}
  thrown in %s/007.php on line 6
