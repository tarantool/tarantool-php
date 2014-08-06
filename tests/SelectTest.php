<?php
class SelectTest extends PHPUnit_Framework_TestCase
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

    public function populate($num)
    {
        for ($i = 0; $i < $num; $i++) {
            $tuple = array($i, (($i + 1) * 2) % 5, "hello, ima tuple " . $i);
            $this->tarantool->replace("test", $tuple);
        }
    }

    public function devastate($num)
    {
        for ($i = 0; $i < $num; $i++) {
            $tuple = array($i, (($i + 1) * 2) % 5, "hello, ima tuple " . $i);
            $this->tarantool->delete("test", array($i));
        }
    }

    public function test_00_select_basic()
    {
        $this->assertEmpty(
            $this->tarantool->select("test")
        );
    }

    public function test_01_select_big()
    {
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

    /*TODO: Currently no iterators in the module - fix this*/
    public function test_02_select_diff()
    {
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
     * @afterClass
     */
    public function tearDown()
    {
        $this->tarantool->close();
    }
}
