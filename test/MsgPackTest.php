<?php
class MsgPackTest extends PHPUnit_Framework_TestCase
{
    /**
     * @beforeClass
     */
    public function setUp() {
        $this->tarantool = new Tarantool('localhost', getenv('PRIMARY_PORT'));
        $this->assertTrue($this->tarantool->ping());
        $this->tarantool->authenticate('test', 'test');
    }

    public function test_00_msgpack_float_key() {
        try {
            $this->tarantool->select("msgpack", array(1));
            $this->assertTrue(False);
        } catch (Exception $e) {
            $this->assertTrue(strpos($e->getMessage(), "Can't create Array") !== False);
        }
    }
    public function test_01_msgpack_array_key() {
        try {
            $this->tarantool->select("msgpack", array(2));
            $this->assertTrue(False);
        } catch (Exception $e) {
            $this->assertTrue(strpos($e->getMessage(), "Can't create Array") !== False);
        }
    }
    public function test_02_msgpack_array_of_float_as_key() {
        try {
            $this->tarantool->select("msgpack", array(3));
            $this->assertTrue(False);
        } catch (Exception $e) {
            $this->assertTrue(strpos($e->getMessage(), "Can't create Array") !== False);
        }
    }

    public function test_03_msgpack_integer_keys_arrays() {
        try {
            $this->tarantool->insert("msgpack", array(4,
                    "Integer keys causing server to error",
                    array(2 => 'maria', 5 => 'db')
                )
            );
            $this->assertTrue(True);
        } catch (Exception $e) {
            $this->assertTrue(False);
        }
    }

    public function test_04_msgpack_string_keys() {
        $this->tarantool->insert("msgpack", array(5,
                "String keys in response forbids client to take values by keys",
                array(2 => 'maria', 5 => 'db', 'lol' => 'lal')
            )
        );
        $resp = $this->tarantool->select("msgpack", array(5));
        $this->assertEquals($resp[0][2]['lol'], 'lal');
        $this->assertTrue(True);
        $resp = $this->tarantool->select("msgpack", array(6));
        $this->assertEquals($resp[0][2]['megusta'], array(1, 2, 3));
        $this->assertTrue(True);
    }
}
