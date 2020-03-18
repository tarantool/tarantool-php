<?php

use PHPUnit\Framework\TestCase;

class MockTest extends TestCase
{
    public function testFoo()
    {
        $tnt = $this->createMock(Tarantool::class);
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

