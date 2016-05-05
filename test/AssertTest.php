<?php
class AssertTest extends PHPUnit_Framework_TestCase
{
    protected static $tarantool, $tm;

    public static function setUpBeforeClass()
    {
        self::$tarantool = new Tarantool('localhost', getenv('PRIMARY_PORT'), 'test', 'test');
    }

    protected function tearDown()
    {
        $tuples = self::$tarantool->select("test");
        foreach($tuples as $value)
            self::$tarantool->delete("test", Array($value[0]));
    }

    /**
     * @expectedException Exception
     * @expectedExceptionMessage Can't read query
     **/
    public function test_00_timedout() {
        self::$tarantool->eval("
            function assertf()
                require('fiber').sleep(1)
                return 0
            end");
        self::$tarantool->call("assertf");

        /* We can reconnect and everything will be ok */
        self::$tarantool->select("test");
    }
}
