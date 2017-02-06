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

class RandomTest extends PHPUnit_Framework_TestCase
{
		protected static $tarantool;

		public static function setUpBeforeClass() {
			self::$tarantool = new Tarantool('localhost', getenv('PRIMARY_PORT'), 'test', 'test');
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
		public function test_04_get_strange_response() {
			self::$tarantool->select("_schema", "12345");
		}
}
