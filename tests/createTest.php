<?php
class CreateTest extends PHPUnit_Framework_TestCase
{
    public function test_00_create_basic()
    {
        $a = new Tarantool();
        $a->connect();
        $this->assertTrue($a->ping());
        $a->close();
        $b = new Tarantool('localhost');
        $b->connect();
        $this->assertTrue($b->ping());
        $b->close();
        $c = new Tarantool('localhost', 3301);
        $c->connect();
        $this->assertTrue($c->ping());
        $c->close();
    }
    public function test_01_create_test_ping_and_close()
    {
        $c = new Tarantool('localhost', 3301);
        $c->connect();
        $c->connect();
        $this->assertTrue($c->ping());
        $c->close();
        $this->assertTrue($c->ping());
        $c->close();
        $c->close();
    }

    /**
     * @expectedException Exception
     * @expectedExceptionMessage failed to connect.
     */
    public function test_02_create_error_host()
    {
        $a = new Tarantool('very_bad_host');
        $a->connect();
    }

    /**
     * @expectedException Exception
     * @expectedExceptionMessage failed to connect.
     */
    public function test_03_create_error_port()
    {
        $a = new Tarantool('localhost', 3300);
        $a->connect();
    }
}
