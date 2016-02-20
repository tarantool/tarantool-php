<?php
class AssertTest extends PHPUnit_Framework_TestCase
{
    protected static $tarantool, $tm;

    public static function setUpBeforeClass()
    {
        self::$tm = ini_get("tarantool.request_timeout");
        ini_set("tarantool.request_timeout", "0.1");
        self::$tarantool = new Tarantool('localhost', getenv('PRIMARY_PORT'));
        self::$tarantool->authenticate('test', 'test');
    }

    public static function tearDownAfterClass() {
        ini_set("tarantool.request_timeout", self::$tm);
    }

    protected function tearDown()
    {
        $tuples = self::$tarantool->select("test");
        foreach($tuples as $value)
            self::$tarantool->delete("test", Array($value[0]));
    }

    public function test_00_timedout() {

        self::$tarantool->eval("
            function assertf()
                require('fiber').sleep(1)
                return 0
            end");
        try {
            self::$tarantool->call("assertf");
            $this->assertFalse(True);
        } catch (Exception $e) {
            $this->assertTrue(strpos($e->getMessage(),
                "Can't read query") !== False);
        }

        /* We can reconnect and everything will be ok */
        self::$tarantool->select("test");
    }
}
