#!/usr/bin/env python

import os
import sys
import shlex
import shutil
import subprocess

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
        return '~/.phpenv/versions/{0}/bin/php'.format(version)
    return path

def build_gdb_cmd(unit):
    cmds = [
        "set args -c php.ini " + unit,
        "set env MALLOC_CHECK_=3",
        "run",
        "bt full",
        "continue",
        "quit"
    ]
    return 'gdb ' + find_php_bin() + ''.join([' --ex ' + repr(cmd) for cmd in cmds])

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
    test_lib_path = ""
    try:
        shutil.copy('tests/shared/phpunit.xml', test_cwd)
        if 'global' in sys.argv:
            cmd = 'phpunit -v'
        else:
            test_lib_path = os.path.join(test_dir_path, 'phpunit.phar')
            shutil.copy('tests/shared/php.ini', test_cwd)
            shutil.copy('modules/tarantool.so', test_cwd)
            os.environ['PATH'] += os.pathsep + test_dir_path
            cmd = 'php -c php.ini {0}'.format(test_lib_path)
        print('Running ' + repr(cmd))
        version = read_popen('php-config --version').strip(' \n\t') + '.'
        version1 = read_popen('php-config --extension-dir').strip(' \n\t')
        version += ' ' + ('With' if version1.find('non-zts') == -1 else 'Without') + ' ZTS'
        version += ' ' + ('With' if version1.find('no-debug') == -1 else 'Without') + ' Debug'
        print('Running against ' + version)
        proc = subprocess.Popen(cmd, shell=True, cwd=test_cwd)
        cmd_stat = proc.wait()
        if (cmd_stat in [245, 139] and 'global' not in sys.argv):
            proc = subprocess.Popen(build_gdb_cmd(test_lib_path), shell=True, cwd=test_cwd)
    finally:
        del srv
        a = [
                os.path.join(test_cwd, 'php.ini'),
                os.path.join(test_cwd, 'phpunit.xml'),
                os.path.join(test_cwd, 'tarantool.so'),
        ]
        for elem in a:
            if os.path.exists(elem):
                os.remove(elem)

exit(main())
