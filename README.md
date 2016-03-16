<a href="http://tarantool.org">
  <img src="https://avatars2.githubusercontent.com/u/2344919?v=2&s=250" align="right">
</a>
<a href="https://travis-ci.org/tarantool/tarantool-php">
  <img src="https://travis-ci.org/tarantool/tarantool-php.png?branch=master" align="right">
</a>
PHP driver for Tarantool 1.6
============================

PECL PHP driver for Tarantool.

If you're looking for 1.5 version, check out branch 'stable'.


## Build

To build Tarantool PHP extenstion PHP-devel package is required. The
package should contain phpize utility.

```sh
$ phpize
$ ./configure
$ make
$ make install
```

## Test

To run tests Tarantool server and PHP/PECL package are requred.

```sh
$ ./test-run.py
```

It'll automaticly find and start Tarantool and, then, run `phpunit.phar` based tests.
If Tarantool doesn't defined in `PATH` variable, you may define it in `TARANTOOL_BOX_PATH` enviroment variable.

```sh
$ TARANTOOL_BOX_PATH=/path/to/tarantool/bin/tarantool ./test-run.py
```


## Installing from PEAR

Tarantool-PHP has its own [PEAR repository](https://tarantool.github.io/tarantool-php).
You may install it from PEAR with just a few commands:

```
pecl channel-discover tarantool.github.io/tarantool-php/pecl
pecl install Tarantool-PHP/Tarantool-beta
```


## Building RPM/DEB/PECL Packages

For building packages - please, read `README.PACK.md`


## IDE autocompletion

Stubs can be found at [tarantool/tarantool-php-stubs](https://github.com/tarantool/tarantool-php-stubs).
Place it into project library path in your IDE.

# API and Configuration

## Configuration file

* `tarantool.persistent` - Enable persistent connections (don't close connections between sessions) (defaults: True, **can't be changed in runtime**)
* `tarantool.con_per_host` - Count of open connections to every Tarantool server to store (defaults: 5, **can't be changed in runtime**)
* `tarantool.timeout` - Connection timeout (defaults: 10 seconds, can be changed in runtime)
* `tarantool.retry_count` - Count of retries for connecting (defaults: 1, can be changed in runtime)
* `tarantool.retry_sleep` - Sleep between connecting retries (defaults: 0.1 second, can be changed in runtime)
* `tarantool.request_timeout` - Read/write timeout for requests (defaults: 10 second, can be changed in runtime)

# Classes and Methods

## Usage

1. [Predefined Constants](#predefined-constants)
2. [Class Tarantool](#class-tarantool)
  * [Tarantool::__construct](#tarantool__construct)
3. [Manipulation connection](#manipulation-connection)
  * [Tarantool::connect](#tarantoolconnect)
  * [Tarantool::disconnect](#tarantooldisconnect)
  * [Tarantool::authenticate](#tarantoolauthenticate)
  * [Tarantool::flushSchema](#tarantoolflushschema)
  * [Tarantool::ping](#tarantoolping)
4. [Database queries](#database-queries)
  * [Tarantool::select](tarantool#select)
  * [Tarantool::insert, replace](#tarantoolinsert-tarantoolreplace)
  * [Tarantool::call](#tarantoolcall)
  * [Tarantool::evaluate](#tarantoolevaluate)
  * [Tarantool::delete](#tarantooldelete)
  * [Tarantool::update](#tarantoolupdate)
  * [Tarantool::upsert](#tarantoolupsert)

### Predefined Constants

_**Description**_: Available Tarantool Constants

* `TARANTOOL_ITER_EQ` - Equality iterator (ALL)
* `TARANTOOL_ITER_REQ` - Reverse equality iterator
* `TARANTOOL_ITER_ALL` - Get all rows
* `TARANTOOL_ITER_LT` - Less then iterator
* `TARANTOOL_ITER_LE` - Less and equal iterator
* `TARANTOOL_ITER_GE` - Greater and equal iterator
* `TARANTOOL_ITER_GT` - Gtreater then iterator
* `TARANTOOL_ITER_BITSET_ALL_SET` - check if all given bits are set (BITSET only)
* `TARANTOOL_ITER_BITSET_ANY_SET` - check if any given bits are set (BITSET only)
* `TARANTOOL_ITER_BITSET_ALL_NOT_SET` - check if all given bits are not set
  (BITSET only)
* `TARANTOOL_ITER_OVERLAPS` - find dots in the n-dimension cube (RTREE only)
* `TARANTOOL_ITER_NEIGHBOR` - find nearest dots (RTREE only)

### Class Tarantool

``` php
Tarantool {
    public Tarantool::__construct ( [ string $host = 'localhost' [, int $port = 3301 ] ] )
    public bool Tarantool::connect ( void )
    public bool Tarantool::disconnect ( void )
    public Tarantool::authenticate(string $login [, string $password = NULL ] )
    public bool Tarantool::flushSchema ( void )
    public bool Tarantool::ping ( void )
    public array Tarantool::select(mixed $space [, mixed $key = array() [, mixed $index = 0 [, int $limit = PHP_INT_MAX [, int $offset = 0 [, $iterator = TARANTOOL_ITER_EQ ] ] ] ] ] )
    public array Tarantool::insert(mixed $space, array $tuple)
    public array Tarantool::replace(mixed $space, array $tuple)
    public array Tarantool::call(string $procedure [, mixed args])
    public array Tarantool::evaluate(string $expression [, mixed args])
    public array Tarantool::delete(mixed $space, mixed $key [, mixed $index])
    public array Tarantool::update(mixed $space, mixed $key, array $ops [, number $index] )
    public array Tarantool::upsert(mixed $space, mixed $key, array $ops [, number $index] )
}
```

#### Tarantool::__construct

```
public Tarantool::__construct ( [ string $host = 'localhost' [, int $port = 3301 ] ] )
```

_**Description**_: Creates a Tarantool client

_**Parameters**_

* `host`: string, default is `'localhost'`
* `port`: number, default is `3301`

_**Return Value**_

Tarantool class instance

##### *Example*

``` php
$tnt = new Tarantool(); // -> new Tarantool('localhost', 3301);
$tnt = new Tarantool('tarantool.org'); // -> new Tarantool('tarantool.org', 3301);
$tnt = new Tarantool('localhost', 16847);
```

## Manipulation connection

### Tarantool::connect

``` php
public bool Tarantool::connect ( void )
```

_**Description**_: Explicit connect to Tarantool Server. If not used, then connection
will be opened on demand.

_**Return Value**_

**BOOL**: True on success
Raises `Exception` if can't connect to Tarantool.

### Tarantool::disconnect

``` php
public bool Tarantool::disconnect ( void )
```

_**Description**_: Explicitly close connection to Tarantool Server. If you're
using persistent connections, then it'll be saved to connection pool.

_**Return Value**_

**BOOL**: True

### Tarantool::authenticate

``` php
public Tarantool::authenticate(string $login [, string $password = NULL ] )
```

_**Description**_: Authenticate to Tarantool using given login/password

_**Parameters**_

* `login`: string - user login (mandatory)
* `password`: string - user password (mandatory, but ignored, if user is guest)

_**Return Value**_ NULL

#### *Example*

``` php
/**
 * - user is 'valdis'
 * - password is 'pelsh'
 */
$tnt->connect('valdis', 'pelsh')
/**
 * - user is 'guest'
 * - password is empty and ignored, anyway
 */
$tnt->connect('guest')
```

### Tarantool::flushSchema

``` php
public bool Tarantool::flushSchema ( void )
```

_**Description**_: Remove space/index schema from client.

_**Return Value**_

**BOOL**: True

### Tarantool::ping

``` php
public bool Tarantool::ping ( void )
```

_**Description**_: Ping Tarantool server.

_**Return Value**_

**BOOL**: True

Throws `Exception` on error.

## Database queries

### Tarantool::select

``` php
public array Tarantool::select(mixed $space [, mixed $key = array() [, mixed $index = 0 [, int $limit = PHP_INT_MAX [, int $offset = 0 [, $iterator = TARANTOOL_ITER_EQ ] ] ] ] ] )
```

_**Description**_: Execute select query from Tarantool server.

_**Parameters**_

* `space`: String/Number, Space id to select from (mandatory)
* `key`: String/Number or Array, key to select (`Array()` by default, selects
  everything from space)
* `index`: String/Number, Index id to select from (0 by default)
* `limit`: Number, limit number of rows to return from select (INT_MAX by default)
* `offset`: Number, offset to select from (0 by default)
* `iterator`: Constant, iterator type. See [Predefined Constants](#predefined-constants)
  for more information (`TARANTOOL_ITER_EQ` by default)

_**Return Value**_

**Array of arrays**: in case of success - list of tuples that satisfy your
request, or empty array, if nothing was found.

**BOOL**: False and raises `Exception` in case of error.

#### Example

``` php
// Select everything from space 'test'
$tnt->select("test");
// Selects from space 'test' by PK with id == 1
$tnt->select("test", 1);
// The same as previous
$tnt->select("test", array(1));
// Selects from space 'test' by secondary key from index 'isec' and == {1, 'hello'}
$tnt->select("test", array(1, "hello"), "isec");
// Selects second hundred of rows from space test
$tnt->select("test", null, null, 100, 100);
// Selects second hundred of rows from space test in reverse equality order
// It meanse: select penultimate hundred
$tnt->select("test", null, null, 100, 100, TARANTOOL_ITER_REQ);
```

### Tarantool::insert, Tarantool::replace

``` php
public array Tarantool::insert(mixed $space, array $tuple)
public array Tarantool::replace(mixed $space, array $tuple)
```

_**Description**_: Insert (if not exists query with same PK) or Replace tuple.

_**Parameters**_

* `space`: String/Number, Space id to select from (mandatory)
* `tuple`: Array, Tuple to Insert/Replace (mandatory)

_**Return Value**_

**Array** in case of success - tuple that was inserted into Tarantool.

**BOOL**: False and raises `Exception` in case of error.

#### Example

``` php
// It'll be processed OK, since no tuples with PK == 1 are in space 'test'
$tnt->insert("test", array(1, 2, "smth"));
// We've just inserted tuple with PK == 1, so it'll fail
// error will be ER_TUPLE_FOUND
$tnt->insert("test", array(1, 3, "smth completely different"));
// But it won't be a problem for replace
$tnt->replace("test", array(1, 3, "smth completely different"));
```

### Tarantool::call

``` php
public array Tarantool::call(string $procedure [, mixed args])
```

_**Description**_: Call stored procedure

_**Parameters**_
* `procedure`: String, procedure to call (mandatory)
* `args`: Any value to pass to procdure as arguments (empty by default)

_**Return Value**_

**Array of arrays** in case of success - tuples that were returned by stored
 procedure.

**BOOL**: False and raises `Exception` in case of error.

#### Example

``` php
$tnt->call("test_2");
$tnt->call("test_3", array(3, 4));
```

### Tarantool::evaluate

``` php
public array Tarantool::evaluate(string $expression [, mixed args])
```

_**Description**_: Evaluate given lua code (demands current user to have
`'execute'` rights for `'universe'` in Tarantool)

_**Parameters**_
* `expression`: String, Lua code to evaluate (mandatory)
* `args`: Any value to pass to procdure as arguments (empty by default)

_**Return Value**_

**Any value**, that was returned from evaluated code.

**BOOL**: False and raises `Exception` in case of error.

#### Example

``` php
$tnt->eval("return test_2()");
$tnt->eval("return test_3(...)", array(3, 4));
$tnt->evaluate("return test_3(...)", array(3, 4));
```

### Tarantool::delete

``` php
public array Tarantool::delete(mixed $space, mixed $key [, mixed $index])
```

_**Description**_: Delete record with given key.

_**Parameters**_
* `space`: String/Number, Space id to delete from (mandatory)
* `key`: String/Number or Array, key to delete row with (mandatory)
* `index`: String/Number, Index id to delete from (0 by default)

_**Return Value**_

**Array** in case of success - tuple that was deleted by query.

**BOOL**: False and raises `Exception` in case of error.

#### Example

``` php
// Following code will delete all tuples from space `test`
$tuples = $tnt->select("test");
foreach($tuples as $value) {
    $tnt->delete("test", array($value[0]));
}
```

### Tarantool::update

``` php
public array Tarantool::update(mixed $space, mixed $key, array $ops [, number $index] )
```

_**Description**_: Update record with given key (update in Tarantool is
apply multiple given operations to tuple)

_**Parameters**_

* `space`: String/Number, Space id to select from (mandatory)
* `key`: Array/Scalar, Key to match tuple with (mandatory)
* `ops`: Array of Arrays, Operations to execute if tuple was found

_**Operations**_

`<serializable>` - any simple type which converts to MsgPack (scalar/array).

* Splice operation - take `field`'th field, replace `length` bytes from `offset`
  byte with 'list':
  ```
  array(
    "field" => <number>,
    "op"    => ":",
    "offset"=> <number>,
    "length"=> <number>,
    "list"  => <string>
  ),
  ```
* Numeric operations:
  ```
  array(
    "field" => <number>,
    "op" => ("+"|"-"|"&"|"^"|"|"),
    "arg" => <number>
  ),
  ```
  - "+" for addition
  - "-" for substraction
  - "&" for bitwise AND
  - "^" for bitwise XOR
  - "|" for bitwise OR
* Delete `arg` fields from 'field':
  ```
  array(
    "field" => <number>,
    "op" => "#",
    "arg" => <number>
  )
  ```
* Replace/Insert before operations:
  ```
  array(
    "field" => <number>,
    "op"    => ("="|"!"),
    "arg"   => <serializable>
  )
  ```
  - "=" replace `field`'th field with 'arg'
  - "=" insert 'arg' before `field`'th field

```
array(
  array(
    "field" => <number>,
    "op"    => ":",
    "offset"=> <number>,
    "length"=> <number>,
    "list"  => <string>
  ),
  array(
    "field" => <number>,
    "op" => ("+"|"-"|"&"|"^"|"|"),
    "arg" => <number>
  ),
  array(
    "field" => <number>,
    "op" => "#",
    "arg" => <number>
  ),
  array(
    "field" => <number>,
    "op"    => ("="|"!"),
    "arg"   => <serializable>
  )
)
```


_**Return Value**_

**Array** in case of success - tuple after it was updated.

**BOOL**: False and raises `Exception` in case of error.

#### Example

``` php
$tnt->update("test", 1, array(
  array(
    "field" => 1,
    "op" => "+",
    "arg" => 16
  ),
  array(
    "field" => 3,
    "op" => "=",
    "arg" => 98
  ),
  array(
    "field" => 4,
    "op" => "=",
    "arg" => intval(0x11111)
  ),
));
$tnt->update("test", 1, array(
  array(
    "field" => 3,
    "op" => "-",
    "arg" => 10
  ),
  array(
    "field" => 4,
    "op" => "&",
    "arg" => intval(0x10101)
  )
));
$tnt->update("test", 1, array(
  array(
    "field" => 4,
    "op" => "^",
    "arg" => intval(0x11100)
  )
));
$tnt->update("test", 1, array(
  array(
    "field" => 4,
    "op" => "|",
    "arg" => intval(0x00010)
  )
));
$tnt->update("test", 1, array(
  array(
    "field" => 2,
    "op" => ":",
    "offset" => 2,
    "length" => 2,
    "list" => "rrance and phillipe show"
  )
));
```

### Tarantool::upsert

``` php
public array Tarantool::upsert(mixed $space, mixed $key, array $ops [, number $index] )
```

_**Description**_: Update or Insert command (If tuple with PK == PK('tuple') exists,
then it'll update this tuple with 'ops', otherwise 'tuple' will be inserted)

_**Parameters**_

* `space`: String/Number, Space id to select from (mandatory)
* `tuple`: Array, Tuple to Insert (mandatory)
* `ops`: Array of Arrays, Operations to execute if tuple was found. Operations
  are described in update section.

_**Return Value**_

Nothing. In simple cases - it mustn't throw errors and returns nothing, but
sometimes it'll, check out [documentation](http://tarantool.org/doc/book/box/box_space.html?highlight=upsert#lua-function.space_object.upsert)

**BOOL**: False and raises `Exception` in case of error.

#### Example

``` php
$tnt->upsert("test", array(124, 10, "new tuple"), array(
  array(
    "field" => 1,
    "op" => "+",
    "arg" => 10
  )
));
```
