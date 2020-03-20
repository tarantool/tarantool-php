<?php

/*
 * PhpUnit compatibility layer.
 *
 * This set of traits allows to run the same test code using
 * different phpunit versions (phpunit-6 and phpunit-7 at the
 * moment).
 *
 * Now it contains workarounds for two problems:
 *
 * - phpunit-7 requirement to use `void` return type declaration
 *   for several methods.
 * - phpunit-8 deprecation warning re using assertContains() for
 *   search a substring in a string.
 *
 * Usage: add `use TestCaseCompat;` in a TestCase derived class
 * and follow instructions and examples below.
 *
 * `void` return type declaration
 * ------------------------------
 *
 * phpunit-7 requires to use `void` return type declaration for
 * the following methods (and several others, which we don't use):
 *
 * - setUpBeforeClass()
 * - tearDownAfterClass()
 * - setUp()
 * - tearDown()
 *
 * See the announcement [1] for more information about changes in
 * this phpunit version.
 *
 * The problem is that php-7.0 does not support `void` return type
 * declaration, so a trick is necessary to support both php-7.0
 * and phpunit-7+ for, say, php-7.3+.
 *
 * The `TestCaseCompat` trait implements the trick. Use it in your
 * test case class and specify do*() methods instead of ones
 * listed above (see the example below).
 *
 * Example:
 *
 *  | use PHPUnit\Framework\TestCase;
 *  |
 *  | final class FooTest extends TestCase
 *  | {
 *  |     use TestCaseCompat;
 *  |
 *  |     public static function doSetUpBeforeClass()
 *  |     {
 *  |         <...>
 *  |     }
 *  |
 *  |     public static function doTearDownAfterClass() {
 *  |         <...>
 *  |     }
 *  |
 *  |     protected function doSetUp() {
 *  |         <...>
 *  |     }
 *  |
 *  |     protected function doTearDown() {
 *  |         <...>
 *  |     }
 *  | }
 *
 * [1]: https://phpunit.de/announcements/phpunit-7.html
 *
 * assertContains() for strings
 * ----------------------------
 *
 * phpunit-8 warns about using of assertContains() for search a
 * substring in a string. Add `use TestCaseCompat;` to a TestCase
 * derived class and use assertStringContainsString() on any
 * phpunit-6+ version.
 */

use PHPUnit\Framework\TestCase;

$testCaseRef = new ReflectionClass(TestCase::class);

/*
 * SetUpTearDownTraitDefaultDoMethods (internal).
 *
 * The common code to use in SetUpTearDownTrait traits on both
 * phpunit-6 and phpunit-7+.
 */
trait SetUpTearDownTraitDefaultDoMethods
{
    private static function doSetUpBeforeClass()
    {
        parent::setUpBeforeClass();
    }

    private static function doTearDownAfterClass()
    {
        parent::tearDownAfterClass();
    }

    private function doSetUp()
    {
        parent::setUp();
    }

    private function doTearDown()
    {
        parent::tearDown();
    }
}

/*
 * SetUpTearDownTrait (private).
 *
 * This trait allow to overcome the problem that php-7.0 does not
 * support `void` return type declaration, while phpunit-7 (which
 * supports php-7.1+) requires them for certain set of methods.
 *
 * The idea is borrowed from Symfony's PHPUnit Bridge, see [1].
 *
 * [1]: https://symfony.com/doc/current/components/phpunit_bridge.html#removing-the-void-return-type
 */
if ($testCaseRef->getMethod('setUp')->hasReturnType()) {
    /* phpunit-7 and newer */
    trait SetUpTearDownTrait
    {
        use SetUpTearDownTraitDefaultDoMethods;

        public static function setUpBeforeClass(): void
        {
            self::doSetUpBeforeClass();
        }

        public static function tearDownAfterClass(): void
        {
            self::doTearDownAfterClass();
        }

        protected function setUp(): void
        {
            self::doSetUp();
        }

        protected function tearDown(): void
        {
            self::doTearDown();
        }
    }
} else {
    /* phpunit-6 */
    trait SetUpTearDownTrait
    {
        use SetUpTearDownTraitDefaultDoMethods;

        public static function setUpBeforeClass()
        {
            self::doSetUpBeforeClass();
        }

        public static function tearDownAfterClass()
        {
            self::doTearDownAfterClass();
        }

        protected function setUp()
        {
            self::doSetUp();
        }

        protected function tearDown()
        {
            self::doTearDown();
        }
    }
}

/*
 * AssertStringContainsStringTrait (private).
 *
 * phpunit-6 does not contain assertStringContainsString() method,
 * while phpunit-8 warns about using assertContains() for search a
 * substring in a string.
 *
 * This trait adds assertStringContainsString() method for
 * phpunit-6.
 */
if ($testCaseRef->hasMethod('assertStringContainsString')) {
    trait AssertStringContainsStringTrait
    {
        /* Nothing to define. */
    }
} else {
    trait AssertStringContainsStringTrait
    {
        public static function assertStringContainsString(
            $needle, $haystack, $message = '')
        {
            self::assertContains($needle, $haystack, $message);
        }
    }
}

/*
 * TestCaseCompat (public).
 *
 * This trait accumulates all hacks defined above.
 */
trait TestCaseCompat
{
    use SetUpTearDownTrait;
    use AssertStringContainsStringTrait;
}
