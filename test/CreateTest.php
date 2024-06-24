<?php

use PHPUnit\Framework\TestCase;

class CreateTest extends TestCase
{
    use TestCaseCompat;

    protected static $port, $tm;

    public static function doSetUpBeforeClass() {
        self::$port = TestHelpers::getTarantoolPort();
        self::$tm = ini_get("tarantool.timeout");
        // error_log("before setting tarantool timeout");
        ini_set("tarantool.timeout", "0.1");
        // error_log("after setting tarantool timeout");
    }

    public static function doTearDownAfterClass() {
        ini_set("tarantool.timeout", self::$tm);
    }

    public function test_00_create_basic() {
        $c = new Tarantool('localhost', self::$port);
        $c->connect();
        $this->assertTrue($c->ping());
        $c->close();
    }

    public function test_01_create_test_ping_and_close() {
        $c = new Tarantool('localhost', self::$port);
        $c->connect();
        $c->connect();
        $this->assertTrue($c->ping());
        $c->close();
        $this->assertTrue($c->ping());
        $c->close();
        $c->close();
    }

    /**
     * @doesNotPerformAssertions
     */
    public function test_01_01_double_disconnect() {
        $a = new Tarantool('localhost', self::$port);
        $a->disconnect();
        $a->disconnect();
    }

    public function test_02_create_error_host() {
        $c = new Tarantool('very_bad_host');

        $this->expectException(TarantoolException::class);
        $this->expectExceptionMessageMatches(
            '/Name or service not known' .
            '|nodename nor servname provided' .
            '|Temporary failure in name resolution/');
        $c->connect();
    }

    public function test_03_00_create_error_port() {
        $c = new Tarantool('127.0.0.1', 65500);

        $this->expectException(TarantoolException::class);
        $this->expectExceptionMessageMatches(
            '/Connection refused|Network is unreachable/');
        $c->connect();
    }

    public function test_03_01_create_error_port() {
        $this->expectException(TarantoolException::class);
        $this->expectExceptionMessage('Invalid primary port value');
        new Tarantool('localhost', 123456);
    }

    public function test_04_create_many_conns() {
        $a = 1;
        while ($a < 10) {
            $this->assertTrue((new Tarantool('127.0.0.1', self::$port))->ping());
            $a++;
        }
    }

    public function test_05_flush_authenticate() {
        $c = new Tarantool('localhost', self::$port);
        $c->connect();
        $this->assertTrue($c->ping());
        $c->authenticate('test', 'test');
        $c->select("test");
        $c->flushSchema();
        $c->select("test");
        $c->flush_schema();
    }

    public function test_05_flush_construct() {
        $c = new Tarantool('localhost', self::$port, 'test', 'test');
        $this->assertTrue($c->ping());
        $c->select("test");
        $c->flushSchema();
        $c->select("test");
        $c->flush_schema();
    }

    public function test_06_bad_credentials() {
        $c = new Tarantool('localhost', self::$port);
        $c->connect();
        $this->assertTrue($c->ping());

        $this->expectException(TarantoolClientError::class);
        $this->expectExceptionMessageMatches(
            '/(Incorrect password supplied for user)|(User not found or supplied credentials are invalid)/');
        $c->authenticate('test', 'bad_password');
    }

    public function test_07_bad_guest_credentials() {
        $c = new Tarantool('localhost', self::$port);
        $c->connect();
        $this->assertTrue($c->ping());

        $this->expectException(TarantoolClientError::class);
        $this->expectExceptionMessageMatches(
            '/(Incorrect password supplied for user)|(User not found or supplied credentials are invalid)/');
        $c->authenticate('guest', 'guest');
    }

    /**
     * Comment this, since behaviour of authentication with 'empty password' has changed
    public function test_07_01_bad_guest_credentials() {
        $c = new Tarantool('localhost', self::$port);
        $c->connect();
        $this->assertTrue($c->ping());

        $this->expectException(TarantoolClientError::class);
        $this->expectExceptionMessage(
            'Incorrect password supplied for user');
        $c->authenticate('guest', '');
    }
    */

    /**
     * @dataProvider provideGoodCredentials
     */
    public function test_08_good_credentials_construct($username, $password = null) {
        if (func_num_args() === 1) {
            $c = new Tarantool('localhost', self::$port, $username);
        } else {
            $c = new Tarantool('localhost', self::$port, $username, $password);
        }
        $this->assertTrue($c->ping());
    }

    public function test_09_inheritance() {
        $c = new class ('localhost', self::$port) extends Tarantool {
            public $property = 42;
        };
        $this->assertSame(42, $c->property);
    }

    public static function provideGoodCredentials()
    {
        return [
            ['guest'],
            ['guest', null],
        ];
    }

    public function test_10_zero_retry_exception() {
        /* A connection to call iproto_connect_count(). */
        $tarantool = new Tarantool('localhost', self::$port, 'test', 'test');
        $iproto_connect_count_before =
            $tarantool->call('iproto_connect_count')[0][0];

        $saved_retry_count = ini_get('tarantool.retry_count');
        ini_set('tarantool.retry_count', 0);

        $exp_err = 'tarantool.retry_count should be 1 or above';
        try {
            $c = new Tarantool('localhost', self::$port);
            $c->connect();
            $this->assertFalse(true);
        } catch (Exception $e) {
            $this->assertInstanceOf(TarantoolException::class, $e);
            $this->assertEquals($exp_err, $e->getMessage());
        } finally {
            ini_set('tarantool.retry_count', $saved_retry_count);
        }

        /* Verify that no connection attempts were performed. */
        $iproto_connect_count_after =
            $tarantool->call('iproto_connect_count')[0][0];
        $this->assertEquals($iproto_connect_count_before,
                            $iproto_connect_count_after);
    }
}

