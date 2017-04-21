#!/usr/bin/python

import os
import sys
import subprocess

from contextlib import contextmanager


def fullPath(path):
    return os.path.join(os.path.dirname(os.path.abspath(__file__)), path)


@contextmanager
def cd(newdir):
    prevdir = os.getcwd()
    os.chdir(os.path.expanduser(newdir))
    try:
        yield
    finally:
        os.chdir(prevdir)

COMPILER = 'g++'

DIRS = {
    'tmp': 'tmp',
    'build': 'build',
    'bin': 'bin',
    'obj': 'obj',
    'src': 'src',
    'lib': 'lib',
    'include': 'include',
    'erlangInclude': '/usr/local/lib/erlang/usr/include/',
    'erlangLibs': '/usr/local/lib/erlang/lib/erl_interface-3.9.2/lib',
    'gtest': 'tests/gtest',
    'tests': 'tests'
}

FILES = {
    'cnode': 'cnode',
    'tests': 'tests',
    'libv8runner': 'libv8runner.so',
    'ov8runner': 'v8runner.o',
    'libgtest': 'libgtest.a',
    'parallelTest': 'parallel_test',
    'parallelTestTp': 'parallel_test_tp'
}

DIRS = {key: fullPath(value) for key, value in DIRS.iteritems()}


def help():
    print("""
        Help info for this script.

        In order to install:
            deps - ./install.py deps
            v8 - ./install.py v8 <version>
            tests - ./install.py tests <path_to_v8>
            cnode - ./install.py cnode <path_to_v8>
    """)


def setUp():
    if not os.path.exists(DIRS['tmp']):
        os.makedirs(DIRS['tmp'])

    if not os.path.exists(DIRS['build']):
        os.makedirs(DIRS['build'])

    if not os.path.exists(DIRS['bin']):
        os.makedirs(DIRS['bin'])

    if not os.path.exists(DIRS['obj']):
        os.makedirs(DIRS['obj'])

    if not os.path.exists(DIRS['lib']):
        os.makedirs(DIRS['lib'])


def deps():
    with cd('{tmp}'.format(**DIRS)):
        if os.system('../deps/icu-56/source/configure --prefix={build}'.format(**DIRS)) == 0:
            if os.system('make -j4 && make install') == 0:
                print('icu-56 has been installed!')
            else:
                print('Problem during make icu-56')
        else:
            print('Problem during configure icu-56')


def cnode(v8Path):
    VARS = DIRS.copy()
    VARS.update(FILES)
    VARS['v8'] = fullPath(v8Path)
    VARS['compiler'] = COMPILER

    commands = [
        '{compiler} -c -o {obj}/{ov8runner} -fpic {src}/v8runner.cpp -I{include} -I{v8}/include/ -Wall -Werror -std=c++17'.format(**VARS),
        '{compiler} -shared -o {lib}/{libv8runner} {obj}/{ov8runner} {v8}/out.gn/x64.release/obj/v8_libplatform/*.o {v8}/out.gn/x64.release/obj/v8_libbase/*.o -L{build}/lib -L{v8}/out.gn/x64.release -lpthread -licuuc -licui18n -licuio -licudata -Wall -Werror -std=c++17'.format(**VARS),
        '{compiler} -o {bin}/{cnode} -I{include} -I{v8}/include/ -I{erlangInclude} -L{build}/lib -L{lib} -L{v8}/out.gn/x64.release/ -L{erlangLibs} cnode_main.cpp {src}/cnode.cpp -lerl_interface -lei -lnsl -lpthread -licuuc -licui18n -licuio -licudata {lib}/{libv8runner} -lv8 -std=c++17 -lstdc++fs -Wall -Werror -Wno-write-strings -Wl,-rpath-link,{v8}/out.gn/x64.release/'.format(**VARS),
    ]

    for command in commands:
        print(command)
        os.system(command)

    # TODO: set LD_LIBRARY_PATH /home/eduardb/Documents/work/corezoid/v8/icu-56/source/lib:/home/eduardb/Documents/work/corezoid/v8/wrapper/lib:/home/eduardb/Documents/work/corezoid/v8/v8/out.gn/x64.release


def gtest():
    commands = [
        "{compiler} -I include -I . -c src/gtest-all.cc".format(compiler=COMPILER),
        "ar -rv {lib}/libgtest.a gtest-all.o".format(**DIRS),
        "rm gtest-all.o"
    ];

    with cd(DIRS['gtest']):
        for command in commands:
            os.system(command)


def tests(v8Path):
    gtest()

    VARS = DIRS.copy()
    VARS.update(FILES)
    VARS['v8'] = fullPath(v8Path)
    VARS['compiler'] = COMPILER

    commands = [
        '{compiler} -c -o {obj}/{ov8runner} -fpic {src}/v8runner.cpp -I{include} -I{v8}/include/ -Wall -Werror -std=c++17'.format(**VARS),
        '{compiler} -shared -o {lib}/{libv8runner} {obj}/{ov8runner} {v8}/out.gn/x64.release/obj/v8_libplatform/*.o {v8}/out.gn/x64.release/obj/v8_libbase/*.o -L{build}/lib -L{v8}/out.gn/x64.release -lpthread -licuuc -licui18n -licuio -licudata -Wall -Werror -std=c++17'.format(**VARS),
        "{compiler} -fopenmp -o {bin}/{tests} -I{include} -I{v8}/include/ -I{gtest}/include -L{build}/lib -L{lib} -L{v8}/out.gn/x64.release/ {tests}/test.cpp {lib}/{libgtest} -lpthread -licuuc -licui18n -licuio -licudata {lib}/{libv8runner} -lv8 -std=c++17 -lstdc++fs -Wl,-rpath-link,{v8}/out.gn/x64.release/".format(**VARS),
        "{compiler} -fopenmp -o {bin}/{parallelTest} -I{include} -I{v8}/include/ -I{gtest}/include -L{build}/lib -L{lib} -L{v8}/out.gn/x64.release/ {tests}/parallel_test.cpp -lpthread -licuuc -licui18n -licuio -licudata {lib}/{libv8runner} -lv8 -std=c++17 -lstdc++fs -Wl,-rpath-link,{v8}/out.gn/x64.release/".format(**VARS),
        "{compiler} -fopenmp -o {bin}/{parallelTestTp} -I{include} -I{v8}/include/ -I{gtest}/include -L{build}/lib -L{lib} -L{v8}/out.gn/x64.release/ {tests}/parallel_test_using_tp.cpp -lpthread -licuuc -licui18n -licuio -licudata {lib}/{libv8runner} -lv8 -std=c++17 -lstdc++fs -Wl,-rpath-link,{v8}/out.gn/x64.release/".format(**VARS),
    ];

    for command in commands:
        print(command)
        os.system(command)

    os.system('cp -R {tests}/data {bin}/data'.format(**VARS))

    os.system('cp {v8}/out.gn/x64.release/natives_blob.bin {bin}'.format(**VARS))
    os.system('cp {v8}/out.gn/x64.release/snapshot_blob.bin {bin}'.format(**VARS))


if __name__ == '__main__':
    sys.argv = sys.argv[1:]

    setUp()

    if len(sys.argv) == 0:
        help()
        sys.exit()

    if sys.argv[0] == 'deps':
        deps()
    elif sys.argv[0] == 'cnode':
        if len(sys.argv) <= 1:
            print('Please, provide path for v8 dir.')
        else:
            v8Path = sys.argv[1]
            cnode(v8Path)
    elif sys.argv[0] == 'tests':
        if len(sys.argv) <= 1:
            print('Please, provide path for v8 dir.')
        else:
            v8Path = sys.argv[1]
            tests(v8Path)
    else:
        help()
