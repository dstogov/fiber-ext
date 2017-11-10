--TEST--
Check if fiber is loaded
--SKIPIF--
<?php
if (!extension_loaded('fiber')) {
	echo 'skip';
}
?>
--FILE--
<?php 
echo 'The extension "fiber" is available';
?>
--EXPECT--
The extension "fiber" is available
