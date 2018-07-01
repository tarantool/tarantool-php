<?php

declare(strict_types=1);

use PHPUnit\Framework\TestCase;

final class MockTest extends TestCase
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
            $port = testHelpers::getTarantoolPort();
            (new Tarantool('localhost', $port))->select('_vindex', [], 'name');
            $this->assertFalse(True);
        } catch (Exception $e) {
           $this->assertTrue(True);
        }
    }
}

