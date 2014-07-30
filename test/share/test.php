<?
$tarantool = new Tarantool('localhost', 3301);
printf("Tarantool object created\n");
if ($tarantool->connect()) {
    printf("Tarantool connected\n");
}
if ($tarantool->ping()) {
    printf("Ping OK\n");
}
if ($tarantool->authenticate('test', 'test')) {
    printf("Authenticated\n");
}
