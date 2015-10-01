<?php
const SPACE_INDEX = 289;
class MockTest extends \PHPUnit_Framework_TestCase
{
    public function testFoo()
    {
        $tnt = $this->getMock('Tarantool');
        $tnt->expects($this->once())->method('ping');
        $tnt->ping();
    }

    public function testDoo()
    {
        try {
            (new Tarantool('localhost', getenv('PRIMARY_PORT')))->select('_vindex', [], 'name');
            $this->assertFalse(True);
        } catch (Exception $e) {
            $this->assertTrue(True);
        }
    }
}

