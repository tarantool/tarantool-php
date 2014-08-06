#!/usr/bin/env python

import os
import sys
import shlex
import shutil
import subprocess

from lib.tarantool_server import TarantoolServer

def main():
    path = os.path.dirname(sys.argv[0])
    if not path:
        path = '.'
    os.chdir(path)

    srv = None
    srv = TarantoolServer()
    srv.script = 'tests/shared/box.lua'
    srv.start()

    shutil.copy('tests/shared/php.ini', srv.vardir)
    shutil.copy('modules/tarantool.so', srv.vardir)
    test_dir_path = os.path.join(os.getcwd(), 'tests')
    test_lib_path = os.path.join(test_dir_path, 'phpunit.phar')
    cmd = shlex.split('php -c php.ini {0} {1}'.format(test_lib_path, test_dir_path))
    proc = subprocess.Popen(cmd, cwd=srv.vardir)
    return proc.wait()

exit(main())
