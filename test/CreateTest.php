<?php

use PHPUnit\Framework\TestCase;

final class CreateTest extends TestCase
{
	protected static $tm, $con_type;

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
			self::$con_type = 'UNIX';
		} else {
			$uri = sprintf('tcp://%s%s%s%slocalhost:%d',
						  $user ? $user : '',
						  ($user and $pass) ? ':' : '',
						  ($user and $pass) ? $pass : '',
						  $user ? '@' : '',
						  $port);
			self::$con_type = 'TCP';
		}
		return new Tarantool($uri);
	}

	public static function setUpBeforeClass() {
		self::$tm = ini_get("tarantool.timeout");
		ini_set("tarantool.timeout", "0.1");
	}

	public static function tearDownAfterClass() {
		ini_set("tarantool.timeout", self::$tm);
	}

	public function test_00_create_basic() {
		$c = static::connectTarantool();
		$c->connect();
		$this->assertTrue($c->ping());
		$c->close();
	}

	public function test_01_create_test_ping_and_close() {
		$c = static::connectTarantool();
		$c->connect();
		$c->connect();
		$this->assertTrue($c->ping());
		$c->close();
		$this->assertTrue($c->ping());
		$c->close();
		$c->close();
	}

	/**
	 * @doesNotPerformAssertions
	 */
	public function test_01_01_double_disconnect() {
		$a = static::connectTarantool();
		$a->disconnect();
		$a->disconnect();
	}

	/**
	 * @expectedException TarantoolException
	 * @expectedExceptionMessageRegExp /Name or service not known|nodename nor servname provided|getaddrinfo/
	 */
	public function test_02_create_error_host() {
		(new Tarantool('very_bad_host'))->connect();
	}

	/**
	 * @expectedException TarantoolException
	 * @expectedExceptionMessageRegExp /Connection refused|Network is unreachable/
	 */
	public function test_03_00_create_error_port() {
		(new Tarantool('localhost', 65500))->connect();
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
			$this->assertTrue(static::connectTarantool()->ping());
			$a++;
		}
	}

	public function test_05_flush_authenticate() {
		$c = static::connectTarantool();
		$c->connect();
		$this->assertTrue($c->ping());
		$c->authenticate('test', 'test');
		$c->select("test");
		$c->flushSchema();
		$c->select("test");
		$c->flush_schema();
	}

	public function test_05_flush_construct() {
		$c = static::connectTarantool('test', 'test');
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
		$c = static::connectTarantool();
		$c->connect();
		$this->assertTrue($c->ping());
		$c->authenticate('test', 'bad_password');
	}

	/**
	 * @expectedException TarantoolClientError
	 * @expectedExceptionMessage Incorrect password supplied for user
	 */
	public function test_07_bad_guest_credentials() {
		$c = static::connectTarantool();
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
		$c = static::connectTarantool($username, $password);
		/*
		if (func_num_args() === 1) {
			$c = new Tarantool('localhost', intval(self::$port), $username);
		} else {
			$c = new Tarantool('localhost', intval(self::$port), $username, $password);
		}
		 */
		$this->assertTrue($c->ping());
	}

	public static function provideGoodCredentials()
	{
			return [
					['guest'],
					['guest', null],
			];
	}


	public function test_09_parse_uri_compatibility() {
		$port = getenv('PRIMARY_PORT');

		if (self::$con_type == 'UNIX') {
			$c = new Tarantool(sprintf('unix://%s', $port));
			$this->assertEquals($c->call('box.session.user')[0][0], 'guest');
			$c->close();

			$c = new Tarantool(sprintf('unix/:%s', $port));
			$this->assertEquals($c->call('box.session.user')[0][0], 'guest');
			$c->close();

			$c = new Tarantool(sprintf('test:test@unix/:%s', $port));
			$this->assertEquals($c->call('box.session.user')[0][0], 'test');
			$c->close();
		} elseif (self::$con_type == 'TCP') {
			$c = new Tarantool('localhost', intval($port));
			$this->assertEquals($c->call('box.session.user')[0][0], 'guest');
			$c->close();

			$c = new Tarantool('localhost', intval($port), 'test', 'test');
			$this->assertEquals($c->call('box.session.user')[0][0], 'test');
			$c->close();


			$c = new Tarantool(sprintf('tcp://localhost:%d', $port));
			$this->assertEquals($c->call('box.session.user')[0][0], 'guest');
			$c->close();

			$c = new Tarantool(sprintf('tcp://test:test@localhost:%d', $port));
			$this->assertEquals($c->call('box.session.user')[0][0], 'test');
			$c->close();
		}
	}

	public function test_10_zero_retry_exception() {
		$t = static::connectTarantool();
		$rc = ini_get('tarantool.retry_count');

		ini_set('tarantool.retry_count', 0);
		$this->assertEquals($t->ping(), true);
		ini_set('tarantool.retry_count', $rc);
	}
}
