<?php

use PHPUnit\Framework\TestCase;

final class MockTest extends TestCase
{
	static function connectTarantool($user = null, $pass = null) {
		$port = getenv('PRIMARY_PORT');
		$uri = null;
		if ($port[0] == '/') {
			$uri = sprintf('%s%s%s%sunix/:%s',
						  $user ? $user : '',
						  ($user and $pass) ? ':' : '',
						  ($user and $pass) ? $pass : '',
						  $user ? '@' : '',
						  $port);
		} else {
			$uri = sprintf('tcp://%s%s%s%slocalhost:%d',
						  $user ? $user : '',
						  ($user and $pass) ? ':' : '',
						  ($user and $pass) ? $pass : '',
						  $user ? '@' : '',
						  $port);
		}
		return new Tarantool($uri);
	}

	public function testFoo()
	{
		$tnt = $this->createMock(\Tarantool::class);
		$tnt->expects($this->once())->method('ping');
		$tnt->ping();
	}

	public function testDoo()
	{
		try {
			static::connectTarantool()->select('_vindex', [], 'name');
			$this->assertFalse(True);
		} catch (Exception $e) {
		   $this->assertTrue(True);
		}
	}
}

