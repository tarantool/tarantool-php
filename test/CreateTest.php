<?php
class CreateTest extends PHPUnit_Framework_TestCase
{
    /**
     * @beforeClass
     */
    public function setUp()
    {
        $this->port = getenv('LISTEN');
    }


    public function test_00_create_basic()
    {
        $c = new Tarantool('localhost', $this->port);
        $c->connect();
        $this->assertTrue($c->ping());
        $c->close();
    }
    public function test_01_create_test_ping_and_close()
    {
        $c = new Tarantool('localhost', $this->port);
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
     * @expectedExceptionMessage Failed to connect.
     */
    public function test_02_create_error_host()
    {
        $a = new Tarantool('very_bad_host');
        $a->connect();
    }

    /**
     * @expectedException Exception
     * @expectedExceptionMessage Failed to connect.
     */
    public function test_03_create_error_port()
    {
        $a = new Tarantool('localhost', 65500);
        $a->connect();
    }
}
