<?php

declare(strict_types=1);

use PHPUnit\Framework\TestCase;

class DMLTest extends TestCase
{
    use TestCaseCompat;

    protected static $tarantool;

    public static function doSetUpBeforeClass()
    {
        $port = TestHelpers::getTarantoolPort();
        self::$tarantool = new Tarantool('localhost', $port, 'test', 'test');
    }

    protected function doTearDown()
    {
        $tuples = self::$tarantool->select("test");
        foreach($tuples as $value)
            self::$tarantool->delete("test", $value[0]);
    }

    private function populate($num)
    {
        for ($i = 0; $i < $num; $i++) {
            $tuple = array($i, (($i + 1) * 2) % 5, "hello, ima tuple " . $i);
            self::$tarantool->replace("test", $tuple);
        }
    }

    private function devastate($num, $assert=False)
    {
        $this->assertTrue(self::$tarantool->ping());
        for ($i = 0; $i < $num; $i++) {
            $tuple = array($i, (($i + 1) * 2) % 5, "hello, ima tuple " . $i);
            if ($assert) {
                $this->assertEquals(self::$tarantool->delete("test", array($i))[0], $tuple);
            } else {
                self::$tarantool->delete("test", array($i));
            }
        }
    }

    public function test_00_select_basic() {
        $this->assertEmpty(
            self::$tarantool->select("test")
        );
    }

    public function test_01_select_big() {
        $this->populate(100);
        $this->assertCount(
            100, self::$tarantool->select("test")
        );
        $this->assertCount(20, self::$tarantool->select("test", array(1), "secondary"));
        $this->assertCount(20, self::$tarantool->select("test", array(2), "secondary"));
        $this->assertCount(20, self::$tarantool->select("test", array(3), "secondary"));
        $this->assertCount(20, self::$tarantool->select("test", array(4), "secondary"));
        $this->assertCount(20, self::$tarantool->select("test", array(0), "secondary"));
        $this->assertCount(
            0, self::$tarantool->select("test", array(3, "hello, ima tuple 94"), "secondary")
        );
        $this->assertCount(
            1, self::$tarantool->select("test", array(3, "hello, ima tuple 93"), "secondary")
        );
        $this->devastate(100);
    }

    public function test_02_select_diff() {
        $this->populate(100);
        $this->assertCount(100, self::$tarantool->select("test"));
        $this->assertCount(1, self::$tarantool->select("test", array(1)));
        $this->assertCount(20, self::$tarantool->select("test", array(1), "secondary"));
        $this->assertCount(10, self::$tarantool->select("test", array(1), "secondary", 10));
        $this->assertCount(10, self::$tarantool->select("test", array(1), "secondary", 11, 10));
        $this->assertCount(9, self::$tarantool->select("test", array(1), "secondary", 9, 10));
        $this->assertCount(10, self::$tarantool->select("test", array(1), "secondary", 10, 10));
        $this->devastate(100);
    }

    public function test_03_insert_error() {
        self::$tarantool->insert("test", array(1, 2, "smth"));

        $this->expectException(TarantoolClientError::class);
        $this->expectExceptionMessage('Duplicate key exists');
        self::$tarantool->insert("test", array(1, 2, "smth"));
    }

    public function test_04_replaces() {
        $array_ = array(1, 2, "smth");
        $this->assertEquals($array_, self::$tarantool->insert("test", $array_)[0]);
        $array_ = array(1, 3, "smth completely different");
        $this->assertEquals($array_, self::$tarantool->replace("test", $array_)[0]);
    }

    public function test_05_delete() {
        $this->populate(5);
        $this->devastate(5, True);
    }

    public function test_06_update() {
        self::$tarantool->insert("test", array(1, 2, "test"));
        $result_tuple = array(
            1,
            18,
            "terrance and phillipe show",
            88,
            intval(0x01011)
        );
        self::$tarantool->update("test", 1, array(
            array(
                "field" => 1,
                "op" => "+",
                "arg" => 16
            ),
            array(
                "field" => 3,
                "op" => "=",
                "arg" => 98
            ),
            array(
                "field" => 4,
                "op" => "=",
                "arg" => intval(0x11111)
            ),
        ));
        self::$tarantool->update("test", 1, array(
            array(
                "field" => "s2",
                "op" => "-",
                "arg" => 10
            ),
            array(
                "field" => 4,
                "op" => "&",
                "arg" => intval(0x10101)
            )
        ));
        self::$tarantool->update("test", 1, array(
            array(
                "field" => 4,
                "op" => "^",
                "arg" => intval(0x11100)
            )
        ));
        self::$tarantool->update("test", 1, array(
            array(
                "field" => 4,
                "op" => "|",
                "arg" => intval(0x00010)
            )
        ));
        $tuple = self::$tarantool->update("test", 1, array(
            array(
                "field" => "s1",
                "op" => ":",
                "offset" => 2,
                "length" => 2,
                "list" => "rrance and phillipe show"
            )
        ));
        $this->assertEquals($result_tuple, $tuple[0]);
    }

    /**
     * @doesNotPerformAssertions
     */
    public function test_07_update_no_error() {
        self::$tarantool->update("test", 0, array());
    }

    public function test_08_update_error_nosuchfield() {
        $this->expectException(TarantoolException::class);
        $this->expectExceptionMessage("No field 'nosuchfield' defined");
        self::$tarantool->update("test", 0, array(
            array(
                "field" => "nosuchfield",
                "op" => ":"
            )
        ));
    }

    public function test_08_update_error() {
        $this->expectException(TarantoolException::class);
        $this->expectExceptionMessage('Five fields');
        self::$tarantool->update("test", 0, array(
            array(
                "field" => 2,
                "op" => ":",
                "offset" => 2,
                "length" => 2,
            )
        ));
    }

    public function test_09_update_error() {
        $this->expectException(TarantoolException::class);
        $this->expectExceptionMessage('Field OP must be provided');
        self::$tarantool->update("test", 0, array(
            array(
                "field" => 2,
                "op" => "BAD_OP",
                "arg" => 2,
            )
        ));
    }

    public function test_10_update_error() {
        $this->expectException(TarantoolException::class);
        $this->expectExceptionMessage('Field OP must be provided');
        self::$tarantool->update("test", 0, array(
            array(
                "field" => 2,
                "arg" => 2,
            )
        ));
    }

    public function test_11_update_error() {
        $this->expectException(TarantoolException::class);
        $this->expectExceptionMessage('Three fields must be provided');
        self::$tarantool->update("test", 0,
            array(
                array(
                    "op" => "^",
                    "field" => 2,
                    "arg" => 2,
                    "unneeeded field" => "LALALLALA"
                )
            )
        );
    }

    public function test_12_call() {
        $result = self::$tarantool->call("test_6", array(true, false, false),
                                         array('call_16' => true));
        $this->assertEquals(array(array(true), array(false), array(false)), $result);
        $this->assertEquals(
            array(
                '0' => array(
                    '0' => array('k1' => 'v2', 'k2' => 'v')
                )
            ),
            self::$tarantool->call("test_2", array(), array('call_16' => true))
        );
        $this->assertEquals(
            self::$tarantool->call("test_3", array(3, 4), array('call_16' => true)),
            array('0' => array('0' => 7))
        );

        $check_call_17 = self::$tarantool->call('tarantool_version_at_least',
                                                array(1, 7, 2, 0));
        if ($check_call_17[0][0]) {
            $result = self::$tarantool->call("test_6", array(true, false, false),
                                             array('call_16' => false));
            $this->assertEquals(array(true, false, false), $result);
            $this->assertEquals(
                array('0' => array('k1' => 'v2', 'k2' => 'v')),
                self::$tarantool->call("test_2", array(), array('call_16' => false))
            );
            $this->assertEquals(
                self::$tarantool->call("test_3", array(3, 4), array('call_16' => false)),
                array('0' => 7)
            );
        }
    }

    public function test_13_eval() {
        $this->assertEquals(
            self::$tarantool->eval("return test_1()"),
            array(
                '0' => 1,
                '1' => array(
                    's' => array('0' => 1, '1' => 1428578535),
                    'u' => 1428578535,
                    'v' => array(),
                    'c' => array(
                        '2' => array('0' => 1, '1' => 1428578535),
                        '106' => array('0' => 1, '1' => 1428578535)
                    ),
                    'pc' => array(
                        '2' => array('0' => 1, '1' => 1428578535, '2' => 9243),
                        '106' => array('0' => 1, '1' => 1428578535, '2' => 9243)
                    )
                ),
                '2' => 1
            )
        );
        $this->assertEquals(
            self::$tarantool->eval("return test_2()"),
            array('0' => array('k1' => 'v2', 'k2' => 'v'))
        );
        $this->assertEquals(
            self::$tarantool->eval("return test_3(...)", array(3, 4)),
            array('0' => 7)
        );
        $this->assertEquals(
            self::$tarantool->evaluate("return test_3(...)", array(3, 4)),
            array('0' => 7)
        );
    }

    public function test_14_select_limit_defaults() {
        self::$tarantool->insert("test", array(1, 2, "hello"));
        self::$tarantool->insert("test", array(2, 3, "hello"));
        self::$tarantool->insert("test", array(3, 4, "hello"));
        self::$tarantool->insert("test", array(4, 2, "hello"));
        $this->assertEquals(count(self::$tarantool->select("test", 3, "secondary", null, null, Tarantool::ITERATOR_GT)), 1);
        $this->assertEquals(count(self::$tarantool->select("test", 3, "secondary", 0,    null, Tarantool::ITERATOR_GT)), 0);
        $this->assertEquals(count(self::$tarantool->select("test", 3, "secondary", 100,  null, Tarantool::ITERATOR_GT)), 1);
    }

    public function test_15_upsert() {
        self::$tarantool->replace("test", array(123, 2, "hello, world", "again", "and again"));
        $result_tuple = self::$tarantool->upsert("test", array(124, 10, "new tuple"), array(
            array(
                "field" => 1,
                "op" => "+",
                "arg" => 10
            )
        ));
        $this->assertEquals(array(), $result_tuple);
        $result_tuple = self::$tarantool->select("test", 124);
        $this->assertEquals(array(124, 10, "new tuple"), $result_tuple[0]);
        $result_tuple = self::$tarantool->upsert("test", array(124, 10, "new tuple"), array(
            array(
                "field" => 1,
                "op" => "+",
                "arg" => 10
            )
        ));
        $this->assertEquals(array(), $result_tuple);
        $result_tuple = self::$tarantool->select("test", 124);
        $this->assertEquals(array(124, 20, "new tuple"), $result_tuple[0]);
        $result_tuple = self::$tarantool->upsert("test", array(123, 10, "new tuple"), array(
            array(
                "field" => 3,
                "op" => "#",
                "arg" => 2
            ),
            array(
                "field" => 2,
                "op" => ":",
                "offset" => 2,
                "length" => 3,
                "list" => "---"
            )
        ));
        $this->assertEquals(array(), $result_tuple);
        $result_tuple = self::$tarantool->select("test", 123);
        $this->assertEquals(array(123, 2, "he---, world"), $result_tuple[0]);
    }

    public function test_16_hash_select() {
        self::$tarantool->select("test_hash");
        self::$tarantool->select("test_hash", []);
        try {
            self::$tarantool->select("test_hash", null, null, null, null, TARANTOOL::ITERATOR_EQ);
            $this->assertFalse(True);
        } catch (TarantoolClientError $e) {
            $this->assertMatchesRegularExpression('(Invalid key part|via a partial key)', $e->getMessage());
        }
    }

    /**
     * @dataProvider provideIteratorClientError
     */
    public function test_17_01_it_clienterror($spc, $itype, $xcmsg) {
        try {
            self::$tarantool->select($spc, null, null, null, null, $itype);
            $this->assertFalse(True);
        } catch (TarantoolClientError $e) {
            $this->assertMatchesRegularExpression($xcmsg, $e->getMessage());
        }
    }

    /**
     * @dataProvider provideIteratorException
     */
    public function test_17_02_it_exception($spc, $itype, $xcmsg) {
        try {
            self::$tarantool->select($spc, null, null, null, null, $itype);
            $this->assertFalse(True);
        } catch (TarantoolException $e) {
            $this->assertStringContainsString($xcmsg, $e->getMessage());
        }
    }

    /**
     * @dataProvider provideIteratorGood
     */
    public function test_17_03_it_good($spc, $itype) {
        try {
            self::$tarantool->select($spc, null, null, null, null, $itype);
            $this->assertTrue(True);
        } catch (Exception $e) {
            $this->assertStringContainsString($xcmsg, $e->getMessage());
        }
    }

    public static function provideIteratorClientError() {
        return [
            ['test_hash', 'EQ'                ,'(Invalid key part|via a partial key)'],
            ['test_hash', 'REQ'               ,'(Invalid key part|via a partial key)'],
            ['test_hash', 'LT'                ,'(Invalid key part|via a partial key)'],
            ['test_hash', 'LE'                ,'(Invalid key part|via a partial key)'],
            ['test_hash', 'GE'                ,'(Invalid key part|via a partial key)'],
            ['test_hash', 'BITSET_ALL_SET'    ,'(Invalid key part|via a partial key)'],
            ['test_hash', 'BITSET_ANY_SET'    ,'(Invalid key part|via a partial key)'],
            ['test_hash', 'BITSET_ALL_NOT_SET','(Invalid key part|via a partial key)'],
            ['test_hash', 'BITS_ALL_SET'      ,'(Invalid key part|via a partial key)'],
            ['test_hash', 'BITS_ANY_SET'      ,'(Invalid key part|via a partial key)'],
            ['test_hash', 'BITS_ALL_NOT_SET'  ,'(Invalid key part|via a partial key)'],
            ['test_hash', 'OVERLAPS'          ,'(Invalid key part|via a partial key)'],
            ['test_hash', 'NEIGHBOR'          ,'(Invalid key part|via a partial key)'],
            ['test_hash', 'eq'                ,'(Invalid key part|via a partial key)'],
            ['test_hash', 'req'               ,'(Invalid key part|via a partial key)'],
            ['test_hash', 'lt'                ,'(Invalid key part|via a partial key)'],
            ['test_hash', 'le'                ,'(Invalid key part|via a partial key)'],
            ['test_hash', 'ge'                ,'(Invalid key part|via a partial key)'],
            ['test_hash', 'bitset_all_set'    ,'(Invalid key part|via a partial key)'],
            ['test_hash', 'bitset_any_set'    ,'(Invalid key part|via a partial key)'],
            ['test_hash', 'bitset_all_not_set','(Invalid key part|via a partial key)'],
            ['test_hash', 'bits_all_set'      ,'(Invalid key part|via a partial key)'],
            ['test_hash', 'bits_any_set'      ,'(Invalid key part|via a partial key)'],
            ['test_hash', 'bits_all_not_set'  ,'(Invalid key part|via a partial key)'],
            ['test_hash', 'overlaps'          ,'(Invalid key part|via a partial key)'],
            ['test_hash', 'neighbor'          ,'(Invalid key part|via a partial key)'],
            ['test'     , 'bitset_all_set'    ,'(does not support requested iterator type)'],
            ['test'     , 'bitset_any_set'    ,'(does not support requested iterator type)'],
            ['test'     , 'bitset_all_not_set','(does not support requested iterator type)'],
            ['test'     , 'bits_all_set'      ,'(does not support requested iterator type)'],
            ['test'     , 'bits_any_set'      ,'(does not support requested iterator type)'],
            ['test'     , 'bits_all_not_set'  ,'(does not support requested iterator type)'],
            ['test'     , 'overlaps'          ,'(does not support requested iterator type)'],
            ['test'     , 'neighbor'          ,'(does not support requested iterator type)'],
        ];
    }

    public static function provideIteratorException() {
        return [
            ['test', 'oevrlps','Bad iterator name'],
            ['test', 'e'      ,'Bad iterator name'],
            ['test', 'nghb'   ,'Bad iterator name'],
            ['test', 'ltt'    ,'Bad iterator name'],
        ];
    }

    public static function provideIteratorGood() {
        return [
            ['test_hash', 'ALL'],
            ['test_hash', 'GT' ],
            ['test_hash', 'all'],
            ['test_hash', 'gt' ],
        ];
    }

    /**
     * @doesNotPerformAssertions
     */
    public function test_18_01_delete_loop() {
        for ($i = 0; $i <= 1000; $i++) {
            self::$tarantool->replace("pstring", array("test2" . $i, $i));
            self::$tarantool->select ("pstring", array("test2" . $i));
            self::$tarantool->delete ("pstring", array("test2" . $i));
        }
        gc_collect_cycles();
        gc_collect_cycles();
        gc_collect_cycles();
        gc_collect_cycles();
    }

    /**
     * @doesNotPerformAssertions
     */
    public function test_18_02_delete_loop() {
        for ($i = 0; $i <= 1000; $i++) {
            self::$tarantool->replace("pstring", array("test2" . $i, $i));
            self::$tarantool->select ("pstring", "test2" . $i);
            self::$tarantool->delete ("pstring", "test2" . $i);
        }
        gc_collect_cycles();
        gc_collect_cycles();
        gc_collect_cycles();
        gc_collect_cycles();
    }

    public function test_19_select_space_no_index_name_known() {
        /*
         * gh-42: 'Failed to parse schema (index)' at select()
         * error when a space is passed as a number, but an index
         * is passed as a name.
         *
         * The problem appears when a schema is not fetched yet,
         * so let's test it on a new client instance.
         */
        $port = TestHelpers::getTarantoolPort();
        $tarantool = new Tarantool('localhost', $port, 'test');

        $vindex_id = 289;
        $res = $tarantool->select($vindex_id, [$vindex_id], 'primary', 1);
        $this->assertEquals($res[0][0], $vindex_id);
    }

    public function test_20_select_space_no_index_name_unknown() {
        /*
         * Related to gh-42, see above. This case is to verify
         * that the client gives an error in the case when unknown
         * space id is passed (together with some index name).
         *
         * One of transient versions of gh-42 patch had a mistake
         * on the error path that cause a segfault. This case
         * triggers execution of the corresponding code.
         */
        $port = TestHelpers::getTarantoolPort();
        $tarantool = new Tarantool('localhost', $port, 'test');

        $this->expectException(TarantoolException::class);
        $this->expectExceptionMessage('No space 1024 defined');

        $unknown_space_id = 1024;
        $tarantool->select($unknown_space_id, [], 'primary');
    }
}
