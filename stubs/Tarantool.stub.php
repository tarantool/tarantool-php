<?php

/**
 * This is a stub file for various PHP IDEs
 */
class Tarantool
{
    /**
     * @param string $host tarantool hostname or ip
     * @param int $port iproto port
     */
    public function __construct($host, $port){}

    /**
     * @throws Exception
     */
    public function connect(){}

    /**
     * @param string $username
     * @param string $password
     */
    public function authenticate($username, $password){}

    /**
     * @return bool
     */
    public function ping(){}

    /**
     * @param string|int $space space number or name
     * @param array $tuple
     */
    public function replace($space, $tuple){}

    /**
     * @param string|int $space space number or name
     * @param array $tuple
     */
    public function insert($space, $tuple){}

    /**
     * @param string|int $space space number or name
     * @param mixed|array $key key can be scalar or php array (may be empty)
     * @param string|int $index index number or name
     * @param int $limit
     * @param int $offset
     * @param int $iterator
     * @return mixed
     */
    public function select($space, $key = null, $index = null, $limit = null, $offset = null, $iterator = null){}

    /**
     * @param string|int $space space number or name
     * @param array $key
     * @param array $ops
     *
     * <ops> ex.:
     * array(
     *   array(
     *     "field" => <number>,
     *     "op"    => ":",
     *     "offset"=> <number>,
     *     "length"=> <number>,
     *     "list"  => <string>
     *   ),
     *   array(
     *     "field" => <number>,
     *     "op" => ("+"|"-"|"&"|"^"|"#"),
     *     "arg" => <number>
     *   ),
     *   array(
     *     "field" => <number>,
     *     "op"    => ("="|"!"),
     *     "arg"   => <serializable>
     *   ),
     * )
     *
     * <serializable> - any simple type which converts to MsgPack (scalar/array).
     * ":" - command `splice` - replace "length" bites in "field" to "list" beginning from "offset".
     * "+" - add "arg" to "field"
     * "-" - sub "arg" from "field"
     * "&" - bitwise "OR" with "field" and "arg" and place result to "field"
     * "^" - bitwise "Exclusive OR" (XOR) with "field" and "arg" and place result to "field"
     * "=" - assign "arg" to "field"
     * "!" - assign "arg" before "field"
     * "#" - remove "arg" fields beginning from "field"
     */
    public function update($space, $key, $ops){}

    /**
     * @param string|int $space space number or name
     * @param array $key
     */
    public function delete($space, $key){}

    /**
     * @param string $proc_name stored procedure (function) name
     * @param array $args arguments
     */
    public function call($proc_name, $args = null){}

    /**
     *
     */
    public function flush_schema(){}
}
