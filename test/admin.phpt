--TEST--
Tarantool administation commands test
--FILE--
<?php
require_once "lib/TarantoolUTest.php";

$tarantool = new Tarantool("localhost", 33013, 33015);

echo "---------- test begin ----------\n";
echo "help\n";
echo $tarantool->admin("help");
echo "----------- test end -----------\n\n";

echo "---------- test begin ----------\n";
echo "insert\n";
for ($i = 0; $i < 10; ++$i)
    echo $tarantool->admin("lua box.insert(0, $i, 'test_uid$i', 'test field #$i')");
echo "----------- test end -----------\n\n";

echo "---------- test begin ----------\n";
echo "select\n";
echo $tarantool->admin("lua box.select(0, 1, 'test_uid$i')");
echo "----------- test end -----------\n\n";

echo "---------- test begin ----------\n";
echo "delete\n";
for ($i = 0; $i < 10; ++$i)
    echo $tarantool->admin("lua box.delete(0, $i)");
echo "----------- test end -----------\n\n";
?>
===DONE===
--EXPECT--
---------- test begin ----------
help
available commands:
 - help
 - exit
 - show info
 - show fiber
 - show configuration
 - show slab
 - show palloc
 - show stat
 - save coredump
 - save snapshot
 - lua command
 - reload configuration
 - show injections (debug mode only)
 - set injection <name> <state> (debug mode only)
----------- test end -----------

---------- test begin ----------
insert
 - 0: {'test_uid0', 'test field #0'}
 - 1: {'test_uid1', 'test field #1'}
 - 2: {'test_uid2', 'test field #2'}
 - 3: {'test_uid3', 'test field #3'}
 - 4: {'test_uid4', 'test field #4'}
 - 5: {'test_uid5', 'test field #5'}
 - 6: {'test_uid6', 'test field #6'}
 - 7: {'test_uid7', 'test field #7'}
 - 8: {'test_uid8', 'test field #8'}
 - 9: {'test_uid9', 'test field #9'}
----------- test end -----------

---------- test begin ----------
select
----------- test end -----------

---------- test begin ----------
delete
 - 0: {'test_uid0', 'test field #0'}
 - 1: {'test_uid1', 'test field #1'}
 - 2: {'test_uid2', 'test field #2'}
 - 3: {'test_uid3', 'test field #3'}
 - 4: {'test_uid4', 'test field #4'}
 - 5: {'test_uid5', 'test field #5'}
 - 6: {'test_uid6', 'test field #6'}
 - 7: {'test_uid7', 'test field #7'}
 - 8: {'test_uid8', 'test field #8'}
 - 9: {'test_uid9', 'test field #9'}
----------- test end -----------

===DONE===