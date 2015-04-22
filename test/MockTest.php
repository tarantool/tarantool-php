<?php
class FooTest extends \PHPUnit_Framework_TestCase
{
    public function testFoo()
    {
        $tnt = $this->getMock('Tarantool');
        $tnt->expects($this->once())->method('ping');
        $tnt->ping();
    }
}

