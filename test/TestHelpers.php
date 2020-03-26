<?php

/*
 * Set of utility functions for testing.
 */
final class TestHelpers {
    /*
     * Get a port number, where tarantool expects binary protocol
     * clients.
     *
     * The source of information is PRIMARY_PORT environment
     * variable, which is set by the test-run.py script.
     *
     * If it is not set for some reason, the function will raise
     * an exception.
     */
    public static function getTarantoolPort() {
        $port = intval(getenv('PRIMARY_PORT'));
        if ($port == 0)
            throw new Exception(
                'Unable to parse PRIMARY_PORT environment variable as int');
        return $port;
    }
};
