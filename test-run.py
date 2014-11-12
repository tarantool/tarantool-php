#!/usr/bin/env python

import os
import sys
import shlex
import shutil
import subprocess

import time

from lib.tarantool_server import TarantoolServer

from pprint import pprint

def read_popen(cmd):
    path = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE)
    path.wait()
    return path.stdout.read()

def find_php_bin():
    path = read_popen('which php')
    if (path.find('phpenv') != -1):
        version = read_popen('phpenv global')
        if (version.find('system') != -1):
            return '/usr/bin/php'
        return '~/.phpenv/versions/{0}/bin/php'.format(version.strip())
    return path

def main():
    path = os.path.dirname(sys.argv[0])
    if not path:
        path = '.'
    os.chdir(path)
    srv = None
    srv = TarantoolServer()
    srv.script = 'test/shared/box.lua'
    srv.start()
    test_dir_path = os.path.abspath(os.path.join(os.getcwd(), 'test'))
    test_cwd = os.path.dirname(srv.vardir)
    test_lib_path = ""
    try:
        shutil.copy('test/shared/phpunit.xml', test_cwd)
        cmd = ''
        test_lib_path = os.path.join(test_dir_path, 'phpunit.phar')
        shutil.copy('test/shared/tarantool.ini', test_cwd)
        shutil.copy('modules/tarantool.so', test_cwd)
        if '--flags' in sys.argv:
            os.environ['ZEND_DONT_UNLOAD_MODULES'] = '1'
            os.environ['USE_ZEND_ALLOC'] = '0'
            os.environ['MALLOC_CHECK_'] = '1'
        if '--valgrind' in sys.argv:
            cmd = cmd + 'valgrind --leak-check=full --log-file=php.out '
            cmd = cmd + '--suppressions=test/shared/valgrind.sup '
            cmd = cmd + '--num-callers=40 ' + find_php_bin()
            cmd = cmd + ' -c tarantool.ini {0}'.format(test_lib_path)
        elif '--gdb' in sys.argv:
            cmd = cmd + 'gdb {0} --ex '.format(find_php_bin())
            cmd = cmd + '"set args -c tarantool.ini {0}"'.format(test_lib_path)
        else:
            cmd = '{0} -c tarantool.ini {1}'.format(find_php_bin(), test_lib_path)

        print('Running ' + repr(cmd))
        version = read_popen('php-config --version').strip(' \n\t') + '.'
        version1 = read_popen('php-config --extension-dir').strip(' \n\t')
        version += '\n' + ('With' if version1.find('non-zts') == -1 else 'Without') + ' ZTS'
        version += '\n' + ('With' if version1.find('no-debug') == -1 else 'Without') + ' Debug'
        print('Running against ' + version)
        proc = subprocess.Popen(cmd, shell=True, cwd=test_cwd)
        proc.wait()
        time.sleep(50)
    finally:
        a = [
                os.path.join(test_cwd, 'tarantool.ini'),
                os.path.join(test_cwd, 'phpunit.xml'),
                os.path.join(test_cwd, 'tarantool.so'),
        ]
        for elem in a:
            if os.path.exists(elem):
                os.remove(elem)

exit(main())
