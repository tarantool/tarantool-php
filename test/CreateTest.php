<?php

class CreateTest extends PHPUnit_Framework_TestCase
{
		protected static $port, $tm;

		public static function setUpBeforeClass() {
			self::$port = getenv('PRIMARY_PORT');
			self::$tm = ini_get("tarantool.timeout");
			// error_log("before setting tarantool timeout");
			ini_set("tarantool.timeout", "0.1");
			// error_log("after setting tarantool timeout");
		}

		public static function tearDownAfterClass() {
			ini_set("tarantool.timeout", self::$tm);
		}

		public function test_00_create_basic() {
			$c = new Tarantool('localhost', self::$port);
			$c->connect();
			$this->assertTrue($c->ping());
			$c->close();
		}

		public function test_01_create_test_ping_and_close() {
			$c = new Tarantool('localhost', self::$port);
			$c->connect();
			$c->connect();
			$this->assertTrue($c->ping());
			$c->close();
			$this->assertTrue($c->ping());
			$c->close();
			$c->close();
		}

		public function test_01_01_double_disconnect() {
			$a = new Tarantool('localhost', self::$port);
			$a->disconnect();
			$a->disconnect();
		}

		/**
		 * @expectedException TarantoolException
		 * @expectedExceptionMessageRegExp /Name or service not known|nodename nor servname provided|Temporary failure in name resolution/
		 */
		public function test_02_create_error_host() {
			(new Tarantool('very_bad_host'))->connect();
		}

		/**
		 * @expectedException TarantoolException
		 * @expectedExceptionMessageRegExp /Connection refused|Network is unreachable/
		 */
		public function test_03_00_create_error_port() {
			(new Tarantool('127.0.0.1', 65500))->connect();
		}

		/**
		 * @expectedException TarantoolException
		 * @expectedExceptionMessage Invalid primary port value
		 */
		public function test_03_01_create_error_port() {
			new Tarantool('localhost', 123456);
		}

		public function test_04_create_many_conns() {
			$a = 1;
			while ($a < 10) {
				$this->assertTrue((new Tarantool('127.0.0.1', self::$port))->ping());
				$a++;
			}
		}

		public function test_05_flush_authenticate() {
			$c = new Tarantool('localhost', self::$port);
			$c->connect();
			$this->assertTrue($c->ping());
			$c->authenticate('test', 'test');
			$c->select("test");
			$c->flushSchema();
			$c->select("test");
			$c->flush_schema();
		}

		public function test_05_flush_construct() {
			$c = new Tarantool('localhost', self::$port, 'test', 'test');
			$this->assertTrue($c->ping());
			$c->select("test");
			$c->flushSchema();
			$c->select("test");
			$c->flush_schema();
		}

		/**
		 * @expectedException TarantoolClientError
		 * @expectedExceptionMessage Incorrect password supplied for user
		 */
		public function test_06_bad_credentials() {
				$c = new Tarantool('localhost', self::$port);
				$c->connect();
				$this->assertTrue($c->ping());
				$c->authenticate('test', 'bad_password');
		}

		/**
		 * @expectedException TarantoolClientError
		 * @expectedExceptionMessage Incorrect password supplied for user
		 */
		public function test_07_bad_guest_credentials() {
			$c = new Tarantool('localhost', self::$port);
			$c->connect();
			$this->assertTrue($c->ping());
			$c->authenticate('guest', 'guest');
		}

		/**
		 * @expectedException TarantoolClientError
		 * @expectedExceptionMessage Incorrect password supplied for user
		 */
		/**
		 * Comment this, since behaviour of authentication with 'empty password' has changed
		public function test_07_01_bad_guest_credentials() {
			$c = new Tarantool('localhost', self::$port);
			$c->connect();
			$this->assertTrue($c->ping());
			$c->authenticate('guest', '');
		}
		*/

		/**
		 * @dataProvider provideGoodCredentials
		 */
		public function test_08_good_credentials_construct($username, $password = null) {
			if (func_num_args() === 1) {
				$c = new Tarantool('localhost', self::$port, $username);
			} else {
				$c = new Tarantool('localhost', self::$port, $username, $password);
			}
			$this->assertTrue($c->ping());
		}

		public function test_09_inheritance() {
			$c = new class ('localhost', self::$port) extends Tarantool {
				public $property = 42;
			};
			$this->assertSame(42, $c->property);
		}

		public static function provideGoodCredentials()
		{
				return [
						['guest'],
						['guest', null],
				];
		}
}

