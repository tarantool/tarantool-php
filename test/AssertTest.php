<?php

use PHPUnit\Framework\TestCase;

final class AssertTest extends TestCase
{
	protected static $tarantool, $tm;

	static function connectTarantool() {
		$port = getenv('PRIMARY_PORT');
		if ($port[0] == '/') {
			return new Tarantool('unix/:' . $port, 'prefix');
		} else {
			return new Tarantool('tcp://localhost:' . $port);
		}
	}

	public static function setUpBeforeClass() {
		self::$tm = ini_get("tarantool.request_timeout");
		ini_set("tarantool.request_timeout", "0.1");
		self::$tarantool = static::connectTarantool();
		self::$tarantool->authenticate('test', 'test');
	}

	public static function tearDownAfterClass() {
		ini_set("tarantool.request_timeout", self::$tm);
	}

	protected function tearDown() {
		$tuples = self::$tarantool->select("test");
		foreach($tuples as $value)
			self::$tarantool->delete("test", Array($value[0]));
	}

	public function test_00_timedout() {
		self::$tarantool->eval("
			function assert_f()
				os.execute('sleep 1')
				return 0
			end");
		try {
			$result = self::$tarantool->call("assert_f");
			$this->assertFalse(True);
		} catch (TarantoolException $e) {
			$this->assertContains("Failed to read", $e->getMessage());
		}

		/* We can reconnect and everything will be ok */
		self::$tarantool->close();
		self::$tarantool->select("test");
	}

	/**
	 * @doesNotPerformAssertions
	 */
	public function test_01_closed_connection() {
		for ($i = 0; $i < 20000; $i++) {
			try {
				self::$tarantool->call("nonexistentfunction");
			} catch (TarantoolClientError $e) {
				continue;
			}
		}
	}
}
