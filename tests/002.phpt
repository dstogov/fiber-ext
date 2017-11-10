--TEST--
tests Fiber::yield
--SKIPIF--
<?php
if (!extension_loaded('fiber')) {
	echo 'skip';
}
?>
--FILE--
<?php
$f = new Fiber(function () {
    echo "start\n";
    Fiber::yield();
    echo "end\n";
});
$f->resume();
echo "fiber\n";
$f->resume();
?>
--EXPECTF--
start
fiber
end
