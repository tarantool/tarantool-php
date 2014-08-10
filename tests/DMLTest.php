<?php
class DMLTest extends PHPUnit_Framework_TestCase
{
    /**
     * @beforeClass
     */
    public function setUp()
    {
        $this->tarantool = new Tarantool('localhost', getenv('LISTEN'));
        $this->assertTrue($this->tarantool->ping());
        $this->tarantool->authenticate('test', 'test');
    }

    /**
     * @after
     */
    public function cleanup()
    {
        $this->tarantool->authenticate('test', 'test');
        $tuples = $this->tarantool->select("test");
        foreach($tuples as $value)
            $this->tarantool->delete("test", $value[0]);
    }

    private function populate($num)
    {
        for ($i = 0; $i < $num; $i++) {
            $tuple = array($i, (($i + 1) * 2) % 5, "hello, ima tuple " . $i);
            $this->tarantool->replace("test", $tuple);
        }
    }

    private function devastate($num, $assert=False)
    {
        for ($i = 0; $i < $num; $i++) {
            $tuple = array($i, (($i + 1) * 2) % 5, "hello, ima tuple " . $i);
            if ($assert) {
                $this->assertEquals($this->tarantool->delete("test", array($i))[0], $tuple);
            } else {
                $this->tarantool->delete("test", array($i));
            }
        }
    }

    public function test_00_select_basic() {
        $this->assertEmpty(
            $this->tarantool->select("test")
        );
    }

    public function test_01_select_big() {
        $this->populate(100);
        $this->assertCount(
            100, $this->tarantool->select("test")
        );
        $this->assertCount(20, $this->tarantool->select("test", array(1), "secondary"));
        $this->assertCount(20, $this->tarantool->select("test", array(2), "secondary"));
        $this->assertCount(20, $this->tarantool->select("test", array(3), "secondary"));
        $this->assertCount(20, $this->tarantool->select("test", array(4), "secondary"));
        $this->assertCount(20, $this->tarantool->select("test", array(0), "secondary"));
        $this->assertCount(
            0, $this->tarantool->select("test", array(3, "hello, ima tuple 94"), "secondary")
        );
        $this->assertCount(
            1, $this->tarantool->select("test", array(3, "hello, ima tuple 93"), "secondary")
        );
        $this->devastate(100);
    }

    public function test_02_select_diff() {
        $this->populate(100);
        $this->assertCount(100, $this->tarantool->select("test"));
        $this->assertCount(1, $this->tarantool->select("test", array(1)));
        $this->assertCount(20, $this->tarantool->select("test", array(1), "secondary"));
        $this->assertCount(10, $this->tarantool->select("test", array(1), "secondary", 10));
        $this->assertCount(10, $this->tarantool->select("test", array(1), "secondary", 11, 10));
        $this->assertCount(9, $this->tarantool->select("test", array(1), "secondary", 9, 10));
        $this->assertCount(10, $this->tarantool->select("test", array(1), "secondary", 10, 10));
        $this->devastate(100);
    }

    /**
     * @expectedException Exception
     * @expectedExceptionMessage Query error 3
     **/
    public function test_03_insert_error() {
        $this->tarantool->insert("test", array(1, 2, "smth"));
        $this->tarantool->insert("test", array(1, 2, "smth"));
    }

    public function test_04_replaces() {
        $array_ = array(1, 2, "smth");
        $this->assertEquals($array_, $this->tarantool->insert("test", $array_)[0]);
        $array_ = array(1, 3, "smth completely different");
        $this->assertEquals($array_, $this->tarantool->replace("test", $array_)[0]);
    }

    public function test_05_delete() {
        $this->populate(5);
        $this->devastate(5, True);
    }

    public function test_06_update() {
        $this->tarantool->insert("test", array(1, 2, "test"));
        $result_tuple = array(
            9,
            18,
            "terrance and phillipe show",
            88,
            intval(0x01011)
        );
        $this->tarantool->update("test", 1, array(
            array(
                "field" => 1,
                "op" => "+",
                "arg" => 16
            ),
            array(
                "field" => 0,
                "op" => "=",
                "arg" => 9
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
        $this->tarantool->update("test", 9, array(
            array(
                "field" => 3,
                "op" => "-",
                "arg" => 10
            ),
            array(
                "field" => 4,
                "op" => "&",
                "arg" => intval(0x10101)
            )
        ));
        $this->tarantool->update("test", 9, array(
            array(
                "field" => 4,
                "op" => "^",
                "arg" => intval(0x11100)
            )
        ));
        $this->tarantool->update("test", 9, array(
            array(
                "field" => 4,
                "op" => "|",
                "arg" => intval(0x00010)
            )
        ));
        $tuple = $this->tarantool->update("test", 9, array(
            array(
                "field" => 2,
                "op" => ":",
                "offset" => 2,
                "length" => 2,
                "list" => "rrance and phillipe show"
            )
        ));
        $this->assertEquals($result_tuple, $tuple[0]);
    }

    public function test_07_update_no_error() {
        $this->tarantool->update("test", 0, array());
    }

    /**
     * @expectedException Exception
     * @expectedExceptionMessage Five fields
     */
    public function test_08_update_error_01() {
        $this->tarantool->update("test", 0, array(
            array(
                "field" => 2,
                "op" => ":",
                "offset" => 2,
                "length" => 2,
            )
        ));
    }

    /**
     * @expectedException Exception
     * @expectedExceptionMessage Field OP must be provided
     */
    public function test_08_update_error_02() {
        $this->tarantool->update("test", 0, array(
            array(
                "field" => 2,
                "op" => "BAD_OP",
                "arg" => 2,
            )
        ));
    }

    /**
     * @expectedException Exception
     * @expectedExceptionMessage Field OP must be provided
     */
    public function test_08_update_error_03() {
        $this->tarantool->update("test", 0, array(
            array(
                "field" => 2,
                "arg" => 2,
            )
        ));
    }

    /**
     * @expectedException Exception
     * @expectedExceptionMessage Three fields must be provided
     */
    public function test_08_update_error_04() {
        $this->tarantool->update("test", 0, array(
            array(
                "op" => "^",
                "field" => 2,
                "arg" => 2,
                "unneeeded field" => "LALALLALA"
            )
        ));
    }

    /**
     * @afterClass
     */
    public function tearDown()
    {
        $this->tarantool->close();
    }
}
