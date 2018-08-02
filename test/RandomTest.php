<?php

function generateRandomString($length = 10) {
	$characters = '0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ';
	$charactersLength = strlen($characters);
	$randomString = '';
	for ($i = 0; $i < $length; $i++) {
		$randomString .= $characters[rand(0, $charactersLength - 1)];
	}
	return $randomString;
}

use PHPUnit\Framework\TestCase;

final class RandomTest extends TestCase
{
	protected static $tarantool;

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

	public static function setUpBeforeClass() {
		self::$tarantool = static::connectTarantool('test', 'test');
		self::$tarantool->ping();
	}

	public function test_01_random_big_response() {
		for ($i = 5000; $i < 100000; $i += 1000) {
			$request = "do return '" . generateRandomString($i) . "' end";
			self::$tarantool->evaluate($request);
		}
		$this->assertTrue(True);
	}

	public function test_02_very_big_response() {
		$request = "do return '" . str_repeat('x', 1024 * 1024 * 1) . "' end";
		self::$tarantool->evaluate($request);
		$this->assertTrue(True);
	}

	public function test_03_another_big_response() {
		for ($i = 100; $i <= 5200; $i += 100) {
			$result = self::$tarantool->call('test_5', array($i));
			$this->assertEquals($i, count($result));
		}
	}

	/**
	 * @doesNotPerformAssertions
	 */
	public function test_04_get_strange_response() {
		self::$tarantool->select("_schema", "12345");
	}
}
