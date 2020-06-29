<a href="http://tarantool.org">
   <img src="https://avatars2.githubusercontent.com/u/2344919?v=2&s=250" 
align="right">
</a>
<a href="https://travis-ci.org/tarantool/tarantool-php">
   <img src="https://travis-ci.org/tarantool/tarantool-php.png?branch=master" 
align="right">
</a>

# PHP driver for Tarantool 1.6 and 1.7

This is a PECL PHP driver for Tarantool 1.6 and 1.7.

If you need this driver for Tarantool 1.5, check out the `stable` branch.

# Table of contents

* [Installing and building](#installing-and-building)
  * [Installing from PEAR](#installing-from-pear)
  * [Building from source](#building-from-source)
  * [Testing](#testing)
  * [Building RPM/DEB/PECL packages](#building-rpmdebpecl-packages)
  * [IDE autocompletion](#ide-autocompletion)
* [Configuration reference](#configuration-reference)
* [API reference](#api-reference)
    * [Predefined constants](#predefined-constants)
    * [Class Tarantool](#class-tarantool)
      * [Tarantool::\_\_construct](#tarantool__construct)
    * [Connection manipulations](#connection-manipulations)
      * [Tarantool::disconnect](#tarantooldisconnect)
      * [Tarantool::flushSchema](#tarantoolflushschema)
      * [Tarantool::ping](#tarantoolping)
    * [Database queries](#database-queries)
      * [Tarantool::select](#tarantoolselect)
      * [Tarantool::insert, Tarantool::replace](#tarantoolinsert-tarantoolreplace)
      * [Tarantool::call](#tarantoolcall)
      * [Tarantool::evaluate](#tarantoolevaluate)
      * [Tarantool::delete](#tarantooldelete)
      * [Tarantool::update](#tarantoolupdate)
      * [Tarantool::upsert](#tarantoolupsert)
    * [Deprecated](#deprecated)

# Installing and building

## Installing from PEAR

The `tarantool-php` driver has its own
[PEAR repository](http://tarantool.github.io/tarantool-php/pecl/).
You can install it from PEAR with just a few commands:

```
pecl channel-discover tarantool.github.io/tarantool-php/pecl
pecl install Tarantool-PHP/Tarantool-beta
```

## Building from source

To build and install the `tarantool-php` driver from source, you need:
* PHP version 5 (no less than 5.3.0, but strictly under 6.0.0);
* `php5-dev` package (the package should contain a utility named `phpize`);
* `php-pear` package.

For example, to build and install the driver on Ubuntu, the commands may be as
follows:

```sh
$ sudo apt-get install php5-cli
$ sudo apt-get install php5-dev
$ sudo apt-get install php-pear
$ cd ~
$ git clone https://github.com/tarantool/tarantool-php.git
$ cd tarantool-php
$ phpize
$ ./configure
$ make
$ make install
```

At this point, there is a file named `~/tarantool-php/modules/tarantool.so`.
PHP will only find it if the PHP initialization file `php.ini` contains a line
like `extension=./tarantool.so`, or if PHP is started with the option
`-d extension=~/tarantool-php/modules/tarantool.so`.

## Testing

To run tests, the Tarantool server and PHP/PECL package are required.

```sh
$ ./test-run.py
```

The `test.run.py` program will automatically find and start Tarantool and 
then run `phpunit.phar` based tests.
If Tarantool is not defined in the `PATH` environment variable, you can 
define it in the `TARANTOOL_BOX_PATH` environment variable.

```sh
$ TARANTOOL_BOX_PATH=/path/to/tarantool/bin/tarantool ./test-run.py
```

## Building RPM/DEB/PECL packages

For building packages, please see
[README.PACK.md](https://github.com/tarantool/tarantool-php/blob/master/README.PACK.md).

## IDE autocompletion

Stubs are available at 
[tarantool/tarantool-php-stubs](https://github.com/tarantool/tarantool-php-stubs).
Place the contents in the project library path in your IDE.

# Configuration reference

In the configuration file, you can set the following parameters:

* `tarantool.persistent` - enable persistent connections (don't close 
connections between sessions) (default: True, **cannot be changed at 
runtime**);
* `tarantool.timeout` - connection timeout (default: 10.0 seconds, can be 
changed at runtime);
* `tarantool.retry_count` - count of retries for connecting (default: 1, 
can be changed at runtime);
* `tarantool.retry_sleep` - sleep between connecting retries (default: 
0.1 seconds, can be changed at runtime);
* `tarantool.request_timeout` - read/write timeout for requests 
(default: 10.0 seconds, can be changed at runtime).

# API reference

Inside this section:

* [Predefined constants](#predefined-constants)
* [Class Tarantool](#class-tarantool)
  * [Tarantool::__construct](#tarantool__construct)
* [Connection manipulations](#connection-manipulations)
  * [Tarantool::disconnect](#tarantooldisconnect)
  * [Tarantool::flushSchema](#tarantoolflushschema)
   * [Tarantool::ping](#tarantoolping)
* [Database queries](#database-queries)
   * [Tarantool::select](tarantool#select)
   * [Tarantool::insert, replace](#tarantoolinsert-tarantoolreplace)
   * [Tarantool::call](#tarantoolcall)
   * [Tarantool::evaluate](#tarantoolevaluate)
   * [Tarantool::delete](#tarantooldelete)
   * [Tarantool::update](#tarantoolupdate)
   * [Tarantool::upsert](#tarantoolupsert)
* [Deprecated](#deprecated)

## Predefined constants

_**Description**_: Available Tarantool constants.

* `Tarantool::ITERATOR_EQ` - "equality" iterator (ALL);
* `Tarantool::ITERATOR_REQ` - "reverse equality" iterator;
* `Tarantool::ITERATOR_ALL` - get all tuples;
* `Tarantool::ITERATOR_LT` - "less than" iterator;
* `Tarantool::ITERATOR_LE` - "less than or equal" iterator;
* `Tarantool::ITERATOR_GE` - "greater than or equal" iterator;
* `Tarantool::ITERATOR_GT` - "greater than" iterator;
* `Tarantool::ITERATOR_BITS_ALL_SET` - check if all given bits are set (BITSET only);
* `Tarantool::ITERATOR_BITS_ANY_SET` - check if any given bits are set (BITSET only);
* `Tarantool::ITERATOR_BITS_ALL_NOT_SET` - check if all given bits are not set (BITSET only);
* `Tarantool::ITERATOR_OVERLAPS` - find points in an n-dimension cube (RTREE only);
* `Tarantool::ITERATOR_NEIGHBOR` - find the nearest points (RTREE only).

## Class Tarantool

```php
Tarantool {
     public Tarantool::__construct ( [ string $host = 'localhost' 
[, int $port = 3301 [, string $user = "guest" [, string $password = NULL 
[, string $persistent_id = NULL ] ] ] ] ] )
     public bool Tarantool::connect ( void )
     public bool Tarantool::disconnect ( void )
     public bool Tarantool::flushSchema ( void )
     public bool Tarantool::ping ( void )
     public array Tarantool::select (mixed $space [, mixed $key = []
         [, mixed $index = 0 [, int $limit = PHP_INT_MAX [, int $offset = 0
         [, $iterator = Tarantool::ITERATOR_EQ ] ] ] ] ] )
     public array Tarantool::insert (mixed $space, array $tuple)
     public array Tarantool::replace (mixed $space, array $tuple)
     public array Tarantool::call (string $procedure [, mixed args] )
     public array Tarantool::evaluate (string $expression [, mixed args] )
     public array Tarantool::delete (mixed $space, mixed $key [, mixed $index] )
     public array Tarantool::update (mixed $space, mixed $key, array $ops [, number $index] )
     public array Tarantool::upsert (mixed $space, mixed $key, array $ops [, number $index] )
}
```

### Tarantool::__construct

```php
public Tarantool::__construct ( [ string $host = 'localhost' [, int 
$port = 3301 [, string $user = "guest" [, string $password = NULL [, 
string $persistent_id = NULL ] ] ] ] ] )
```

_**Description**_: Creates a Tarantool client.

_**Parameters**_:

* `host`: string, default: `'localhost'`;
* `port`: number, default : `3301`;
* `user`: string, default: `'guest'`;
* `password`: string;
* `persistent_id`: string (if set, then the connection will be persistent,
   unless the `tarantool.persistent` parameter is set in the configuration file).

_**Return value**_: Tarantool class instance

### Example

```php
$tnt = new Tarantool(); // -> new Tarantool('localhost', 3301);
$tnt = new Tarantool('tarantool.org'); // -> new Tarantool('tarantool.org', 3301);
$tnt = new Tarantool('localhost', 16847);
```

## Connection manipulations

Notice that `Tarantool::connect`, `Tarantool::open` (an alias for `connect`) and
`Tarantool::reconnect` are deprecated as any other connection-related
instructions now cause an automatic connect.

To initiate and/or test connection, please use [Tarantool::ping](#tarantoolping).

### Tarantool::disconnect

```php
public bool Tarantool::disconnect ( void )
```

_**Description**_: Explicitly close a connection to the Tarantool server.
If persistent connections are in use, then the connection will be saved in 
the connection pool.
You can also use an alias for this method, `Tarantool::close`.

_**Return value**_: **BOOL**: True

### Tarantool::flushSchema

```php
public bool Tarantool::flushSchema ( void )
```

_**Description**_: Remove the cached space/index schema that was queried from
the client.

_**Return value**_: **BOOL**: True

### Tarantool::ping

```php
public bool Tarantool::ping ( void )
```

_**Description**_: Ping the Tarantool server. Using `ping` is also the
recommended way to initiate and/or test a connection.

_**Return value**_: **BOOL**: True and raises `Exception` on error.

## Database queries

### Tarantool::select

```php
public array Tarantool::select(mixed $space [, mixed $key = [] 
[, mixed $index = 0 [, int $limit = PHP_INT_MAX [, int $offset = 0 
[, $iterator = Tarantool::ITERATOR_EQ ] ] ] ] ] )
```

_**Description**_: Execute a select query on the Tarantool server.

_**Parameters**_:

* `space`: String/Number, Space ID to select from (mandatory);
* `key`: String/Number or Array, key to select (default: `[]` i.e.
   an empty array which selects everything in the space);
* `index`: String/Number, Index ID to select from (default: 0);
* `limit`: Number, the maximum number of tuples to return (default: INT_MAX,
  a large value);
* `offset`: Number, offset to select from (default: 0);
* `iterator`: Constant, iterator type.
  See [Predefined constants](#predefined-constants) for more information
  (default: `Tarantool::ITERATOR_EQ`).
  You can also use strings `'eq'`, `'req'`, `'all'`, `'lt'`, `'le'`, `'ge'`,
  `'gt'`, `'bits_all_set'`, `'bits_any_set'`, `'bits_all_not_set'`,
  `'overlaps'`, `'neighbor'`, `'bits_all_set'`, `'bits_any_set'`, 
  `'bits_all_not_set'` (in either lowercase or uppercase) instead of constants.

_**Return value**_:

* **Array of arrays**: in case of success - a list of tuples that satisfy the
  request, or an empty array if nothing was found;
* **BOOL**: False and raises `Exception` on error.

#### Example

```php
// Selects everything from space 'test'
$tnt->select("test");
// Selects from space 'test' by primary key with id == 1
$tnt->select("test", 1);
// Same effect as the previous statement
$tnt->select("test", [1]);
// Selects from space 'test' by secondary key from index 'isec' and == {1, 'hello'}
$tnt->select("test", [1, "hello"], "isec");
// Selects 100 tuples from space 'test' after skipping 100 tuples
$tnt->select("test", null, null, 100, 100);
// Selects 100 tuples from space 'test' after skipping 100 tuples,
// in reverse equality order.
// Reverse searching goes backward from index end, so this means:
// select penultimate hundred tuples.
$tnt->select("test", null, null, 100, 100, Tarantool::ITERATOR_REQ);
```

### Tarantool::insert, Tarantool::replace

```php
public array Tarantool::insert(mixed $space, array $tuple)
public array Tarantool::replace(mixed $space, array $tuple)
```

_**Description**_: Insert (if there is no tuple with the same primary key) or 
Replace tuple.

_**Parameters**_:

* `space`: String/Number, Space ID to select from (mandatory);
* `tuple`: Array, Tuple to Insert/Replace (mandatory).

_**Return value**_:

* **Array** in case of success - the tuple that was inserted;
* **BOOL**: False and raises `Exception` on error.

#### Example

```php
// This will succeed, because no tuples with primary key == 1 are in space 'test'.
$tnt->insert("test", [1, 2, "something"]);
// This will fail, because we have just inserted a tuple with primary key == 1.
// The error will be ER_TUPLE_FOUND.
$tnt->insert("test", [1, 3, "something completely different"]);
// This will succeed, because Replace has no problem with duplicate keys.
$tnt->replace("test", [1, 3, "something completely different"]);
```

### Tarantool::call

```php
public array Tarantool::call(string $procedure [, mixed args])
```

_**Description**_: Call a stored procedure.

_**Parameters**_:

* `procedure`: String, procedure to call (mandatory);
* `args`: Any value to pass to the procedure as arguments (default: empty).

_**Return value**_:

* **Array of arrays** in case of success - tuples that were returned by the
  procedure;
* **BOOL**: False and raises `Exception` on error.

#### Example

```php
$tnt->call("test_2");
$tnt->call("test_3", [3, 4]);
```

### Tarantool::evaluate

```php
public array Tarantool::evaluate(string $expression [, mixed args])
```

_**Description**_: Evaluate the Lua code in $expression. The current 
user must have the `'execute'` privilege on `'universe'` in Tarantool.

_**Parameters**_:

* `expression`: String, Lua code to evaluate (mandatory);
* `args`: Any value to pass to the procedure as arguments (default: empty).

_**Return value**_:

* **Any value** that was returned from the evaluated code;
* **BOOL**: False and raises `Exception` on error.

#### Example

```php
$tnt->evaluate("return test_2()");
$tnt->evaluate("return test_3(...)", [3, 4]);
```

### Tarantool::delete

```php
public array Tarantool::delete(mixed $space, mixed $key [, mixed $index])
```

_**Description**_: Delete a tuple with a given key.

_**Parameters**_:

* `space`: String/Number, Space ID to delete from (mandatory);
* `key`: String/Number or Array, key of the tuple to be deleted (mandatory);
* `index`: String/Number, Index ID to delete from (default: 0).

_**Return value**_:

* **Array** in case of success - the tuple that was deleted;
* **BOOL**: False and raises `Exception` on error.

#### Example

```php
// The following code will delete all tuples from space `test`
$tuples = $tnt->select("test");
foreach($tuples as $value) {
     $tnt->delete("test", [$value[0]]);
}
```

### Tarantool::update

```php
public array Tarantool::update(mixed $space, mixed $key, array $ops [, number $index] )
```

_**Description**_: Update a tuple with a given key (in Tarantool, an update
can apply multiple operations to a tuple).

_**Parameters**_:

* `space`: String/Number, Space ID to select from (mandatory);
* `key`: Array/Scalar, Key to match the tuple with (mandatory);
* `ops`: Array of Arrays, Operations to execute if the tuple was found.

_**Operations**_:

`<serializable>` - any simple type which converts to MsgPack (scalar/array).

* Splice operation - take `field`'th field, replace `length` bytes from 
`offset` byte with 'list':
   ```php
   [
     "field" => <number>,
     "op" => ":",
     "offset"=> <number>,
     "length"=> <number>,
     "list" => <string>,
   ],
   ```
* Numeric operations:
   ```php
   [
     "field" => <number>,
     "op" => ("+"|"-"|"&"|"^"|"|"),
     "arg" => <number>,
   ],
   ```
   - "+" for addition
   - "-" for subtraction
   - "&" for bitwise AND
   - "^" for bitwise XOR
   - "|" for bitwise OR
* Delete `arg` fields from 'field':
   ```php
   [
     "field" => <number>,
     "op" => "#",
     "arg" => <number>,
   ]
   ```
* Replace/Insert before operations:
   ```php
   [
     "field" => <number>,
     "op" => ("="|"!"),
     "arg" => <serializable>,
   ]
   ```
   - "=" replace `field`'th field with 'arg'
   - "=" insert 'arg' before `field`'th field

```php
[
   [
     "field" => <number>,
     "op" => ":",
     "offset"=> <number>,
     "length"=> <number>,
     "list" => <string>,
   ],
   [
     "field" => <number>,
     "op" => ("+"|"-"|"&"|"^"|"|"),
     "arg" => <number>,
   ],
   [
     "field" => <number>,
     "op" => "#",
     "arg" => <number>,
   ],
   [
     "field" => <number>,
     "op" => ("="|"!"),
     "arg" => <serializable>,
   ],
]
```

_**Return value**_:

* **Array** in case of success - the tuple after it was updated;
* **BOOL**: False and raises `Exception` on error.

#### Example

```php
$tnt->update("test", 1, [
   [
     "field" => 1,
     "op" => "+",
     "arg" => 16,
   ],
   [
     "field" => 3,
     "op" => "=",
     "arg" => 98,
   ],
   [
     "field" => 4,
     "op" => "=",
     "arg" => 0x11111,
   ],
]);
$tnt->update("test", 1, [
   [
     "field" => 3,
     "op" => "-",
     "arg" => 10,
   ],
   [
     "field" => 4,
     "op" => "&",
     "arg" => 0x10101,
   ],
]);
$tnt->update("test", 1, [
   [
     "field" => 4,
     "op" => "^",
     "arg" => 0x11100,
   ],
]);
$tnt->update("test", 1, [
   [
     "field" => 4,
     "op" => "|",
     "arg" => 0x00010,
   ],
]);
$tnt->update("test", 1, [
   [
     "field" => 2,
     "op" => ":",
     "offset" => 2,
     "length" => 2,
     "list" => "rrance and phillipe show",
   ],
]);
```

### Tarantool::upsert

```php
public array Tarantool::upsert(mixed $space, array $tuple, array $ops [, number $index] )
```

_**Description**_: Update or Insert command (if a tuple with primary key 
== PK('tuple') exists, then the tuple will be updated with 'ops', otherwise
'tuple' will be inserted).

_**Parameters**_:

* `space`: String/Number, Space ID to select from (mandatory);
* `tuple`: Array, Tuple to Insert (mandatory);
* `ops`: Array of Arrays, Operations to execute if tuple was found. 
  Operations are described earlier, in [Tarantool::update](#tarantoolupdate).

_**Return value**_:

* In simple cases, even if there is an error, it will throw no error and will
  return nothing.
* In some cases, it will return something, see 
  [Tarantool documentation](http://tarantool.org/doc/book/box/box_space.html?highlight=upsert#lua-function.space_object.upsert).
* **BOOL**: False and raises `Exception` on error (in some cases).

#### Example

```php
$tnt->upsert("test", [124, 10, "new tuple"], [
   [
     "field" => 1,
     "op" => "+",
     "arg" => 10,
   ],
]);
```

## Deprecated

* Global constants, e.g. `TARANTOOL_ITER_<name>`;
* `Tarantool::authenticate` method, please provide credentials in the
  constructor instead;
* `Tarantool::connect`, `Tarantool::open` (an alias for `connect`) and
  `Tarantool::reconnect` methods as any other connection-related instructions 
  now cause an automatic connect;
* `Tarantool::eval` method, please use the `evaluate` method instead;
* `Tarantool::flush_schema` method, deprecated in favor of `flushSchema`;
* configuration parameter `tarantool.con_per_host`, deprecated and removed.
