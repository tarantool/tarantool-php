<?php
class AssertTest extends PHPUnit_Framework_TestCase
{
    protected static $tarantool;

    public static function setUpBeforeClass()
    {
        self::$tarantool = new Tarantool('localhost', getenv('PRIMARY_PORT'));
        self::$tarantool->authenticate('test', 'test');
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
        try {
            sleep(1);
            self::$tarantool->select("test");
            $this->assertFalse(True);
        } catch (Exception $e) {
            /* check that we are closing connection */
            $this->assertTrue(strpos($e->getMessage(),
                "request sync") !== False);
        }
        /* We can reconnect and everything will be ok */
        self::$tarantool->select("test");
    }
}
