<?php

declare(strict_types=1);

use PHPUnit\Framework\TestCase;

class MsgPackTest extends TestCase
{
    use TestCaseCompat;

    protected static $tarantool;

    public static function doSetUpBeforeClass()
    {
        $port = TestHelpers::getTarantoolPort();
        self::$tarantool = new Tarantool('localhost', $port, 'test', 'test');
        self::$tarantool->ping();
    }

    public function test_00_msgpack_call() {
        $resp = self::$tarantool->call('test_4', [
            '4TL2tLIXqMqyGQm_kiE7mRrS96I5E8nqU', 'B627', 0, [
                'browser_stats_first_session_hits' => 1
            ]
        ], ['call_16' => true]);
        $this->assertEquals($resp[0][0], 2);
        $resp = self::$tarantool->call('test_4', [
            '4TL2tLIXqMqyGQm_kiE7mRrS96I5E8nqU', 'B627', 0, [
                'browser_stats_first_session_hit' => 1
            ]
        ], ['call_16' => true]);
        $this->assertEquals($resp[0][0], 2);

        $check_call_17 = self::$tarantool->call('tarantool_version_at_least',
                                                array(1,7,2,0));
        if ($check_call_17[0][0]) {
            $resp = self::$tarantool->call('test_4', [
                '4TL2tLIXqMqyGQm_kiE7mRrS96I5E8nqU', 'B627', 0, [
                    'browser_stats_first_session_hits' => 1
                ]
            ], ['call_16' => false]);
            $this->assertEquals($resp[0], 2);
            $resp = self::$tarantool->call('test_4', [
                '4TL2tLIXqMqyGQm_kiE7mRrS96I5E8nqU', 'B627', 0, [
                    'browser_stats_first_session_hit' => 1
                ]
            ], ['call_16' => false]);
            $this->assertEquals($resp[0], 2);
        }
    }

    public function test_01_msgpack_array_key() {
        $this->expectException(TarantoolException::class);
        $this->expectExceptionMessage('Bad key type for PHP Array');
        self::$tarantool->select("msgpack", array(2));
    }

    public function test_02_msgpack_float_key() {
        $this->expectException(TarantoolException::class);
        $this->expectExceptionMessage('Bad key type for PHP Array');
        self::$tarantool->select("msgpack", array(1));
    }

    public function test_03_msgpack_array_of_float_as_key() {
        $this->expectException(TarantoolException::class);
        $this->expectExceptionMessage('Bad key type for PHP Array');
        self::$tarantool->select("msgpack", array(3));
    }

    /**
     * @doesNotPerformAssertions
     */
    public function test_04_msgpack_integer_keys_arrays() {
        self::$tarantool->replace("msgpack", array(4,
                "Integer keys causing server to error",
                array(2 => 'maria', 5 => 'db')
            )
        );
    }

    public function test_05_msgpack_string_keys() {
        self::$tarantool->replace("msgpack", array(5,
                "String keys in response forbids client to take values by keys",
                array(2 => 'maria', 5 => 'db', 'lol' => 'lal')
            )
        );
        $resp = self::$tarantool->select("msgpack", array(5));
        $this->assertEquals($resp[0][2]['lol'], 'lal');
        $this->assertTrue(True);
        $resp = self::$tarantool->select("msgpack", array(6));
        $this->assertEquals($resp[0][2]['megusta'], array(1, 2, 3));
        $this->assertTrue(True);
    }

    /**
     * @doesNotPerformAssertions
     */
    public function test_06_msgpack_array_reference() {
        $data = array('key1' => 'value1');
        $link = &$data['key1'];
        self::$tarantool->call('test_4', [$data], ['call_16' => true]);
    }
}
