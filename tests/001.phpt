--TEST--
tests Fiber class
--SKIPIF--
<?php
if (!extension_loaded('fiber')) {
	echo 'skip';
}
?>
--FILE--
<?php
echo class_exists('Fiber'), "\n";
$f = new Fiber(function () {});
var_dump($f);
?>
--EXPECTF--
1
object(Fiber)#%d (0) {
}
