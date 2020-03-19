<?php

/*
 * PhpUnit compatibility layer.
 *
 * This set of traits allows to run the same test code using
 * different phpunit versions (phpunit-6 and phpunit-7 at the
 * moment).
 *
 * Now it implements the workaround for phpunit-7 requirement to
 * use `void` return type declarations for the following methods
 * (see [1]).
 *
 * - setUpBeforeClass()
 * - tearDownAfterClass()
 * - setUp()
 * - tearDown()
 *
 * The problem is that php-7.0 does not support `void` return type
 * declaration, so a trick is necessary to support both php-7.0
 * and phpunit-7+ for, say, php-7.3+.
 *
 * The `TestCaseCompat` trait implements the trick. Use it in your
 * test case class and specify do*() methods instead of ones
 * listed above (see the example below).
 *
 * For now it is the only problem, which is solved by the
 * `TestCaseCompat` trait, but it may be expanded in a future.
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
 * Use `TestCaseCompat` trait and consider others as
 * implementation details.
 *
 * [1]: https://phpunit.de/announcements/phpunit-7.html
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
 * TestCaseCompat (public).
 *
 * This trait accumulates all hacks defined above.
 */
trait TestCaseCompat
{
    use SetUpTearDownTrait;
}
