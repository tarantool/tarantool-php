#!/usr/bin/env python

import os
import sys
import shlex
import shutil
import subprocess

from lib.tarantool_server import TarantoolServer

from pprint import pprint

def main():
    path = os.path.dirname(sys.argv[0])
    if not path:
        path = '.'
    os.chdir(path)
    srv = None
    srv = TarantoolServer()
    srv.script = 'tests/shared/box.lua'
    srv.start()
    test_dir_path = os.path.abspath(os.path.join(os.getcwd(), 'tests'))
    test_cwd = os.path.dirname(srv.vardir)
    try:
        shutil.copy('tests/shared/phpunit.xml', test_cwd)
        if len(sys.argv) > 1 and sys.argv[1] == 'global':
            cmd = 'phpunit -v'
        else:
            test_lib_path = os.path.join(test_dir_path, 'phpunit.phar')
            shutil.copy('tests/shared/php.ini', test_cwd)
            shutil.copy('modules/tarantool.so', test_cwd)
            os.environ['PATH'] += os.pathsep + test_dir_path
            cmd = 'php -c php.ini {0}'.format(test_lib_path)
        print('Running ' + repr(cmd))
        version = subprocess.Popen('php-config --version',
                shell=True, stdout=subprocess.PIPE)
        version.wait()
        version = version.stdout.read().strip(' \n\t') + '.'
        version1 = subprocess.Popen('php-config --extension-dir',
                shell=True, stdout=subprocess.PIPE)
        version1.wait()
        version1 = version1.stdout.read().strip(' \n\t')
        if version1.find('non-zts') != -1:
            version += ' Without ZTS'
        else:
            version += ' With ZTS'
        if version1.find('no-debug') != -1:
            version += ', Without Debug'
        else:
            version += ', With Debug'
        print('Running against ' + version)
        proc = subprocess.Popen(cmd, shell=True, cwd=test_cwd)
        return proc.wait()
    finally:
        a = [
                os.path.join(test_cwd, 'php.ini'),
                os.path.join(test_cwd, 'phpunit.xml'),
                os.path.join(test_cwd, 'tarantool.so'),
        ]
        for elem in a:
            if os.path.exists(elem):
                os.remove(elem)

exit(main())
