<?
$tarantool = new Tarantool('localhost', 3301);
$tarantool->connect();
$tarantool->authenticate('guest', 'guest');
$tarantool->ping();
