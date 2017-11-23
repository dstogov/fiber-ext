# PHP-Fiber

Fibers are primitives for implementing light weight cooperative concurrency in PHP.
Basically they are a means of creating Closure that can be paused and resumed.
The scheduling of fiber must be done by the programmer and not the VM.

More details can be found at this [RFC](https://wiki.php.net/rfc/fiber),
and this [PR](https://github.com/php/php-src/pull/2902).

# Install
php-fiber only support PHP 7.2+. If you use PHP-7.2, you must compile your PHP from source and this [patch](zend_fiber.patch) is needed.
As that patch has been merged at [6780c74](https://github.com/php/php-src/commit/6780c746198e01f52affb86f998108419a8621ed), php-fiber will work with PHP-7.3(may release in 2018).

And then, you need do this
``` bash
patch -b -p1 < zend_fiber.patch # for PHP-7.2

phpize
./configure
make
sudo make install
```

# Usage
```php
<?php
function sub1()
{
    // yield from sub call
    return Fiber::yield(1);
}
$fiber = new Fiber(function ($a, $b) {
    $c = Fiber::yield($a + $b);

    $d = sub1();
    return $d.$c;
});

echo $fiber->resume(1, 2);     // echo 3
echo $fiber->resume("world");  // echo 1
echo $fiber->resume("hello "); // echo "hello world"
```

Each `Fiber` has a separate 4k stack.
You can use the `fiber.stack_size` ini option to change the default stack size.
You can also use the second argument of `Fiber::__construct` to set the stack
size on fly.

# Known issues
## Fiber::yield cannot be used in internal callback
The following code will cause a coredump.
```php
<?php
$f = new Fiber(function () {
    array_map(function ($i) {
        Fiber::yield($i);
    }, [1,2]);
});

$f->resume();
```

# Roadmap
