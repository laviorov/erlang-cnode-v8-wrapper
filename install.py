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


DIRS = {
    'bin': 'bin',
    'obj': 'obj',
    'src': 'src',
    'lib': 'lib',
    'include': 'include',
    'icu56': 'deps/icu-56/',
    'erlangInclude': '/usr/local/lib/erlang/usr/include/',
    'erlangLibs': '/usr/local/lib/erlang/lib/erl_interface-3.9.2/lib',
}

FILES = {
    'cnode': 'cnode',
    'libv8runner': 'libv8runner.so',
    'ov8runner': 'v8runner.o'
}

DIRS = {key: fullPath(value) for key, value in DIRS.iteritems()}
# FILES = {key: fullPath(value) for key, value in FILES.iteritems()}

def help():
    print("""
        Help info for this script.

        In order to install:
            deps - ./install.py deps
            v8 - ./install.py v8 <version>
            tests - ./install.py tests
            cnode - ./install.py cnode <path_to_v8>
    """)


def setUp():
    if not os.path.exists(DIRS['bin']):
        os.makedirs(DIRS['bin'])

    if not os.path.exists(DIRS['obj']):
        os.makedirs(DIRS['obj'])

    if not os.path.exists(DIRS['lib']):
        os.makedirs(DIRS['lib'])



def deps():
    with cd('./deps/icu-56/source'):
        if subprocess.call('./configure', shell=True) == 0:
            if subprocess.call('make -j4', shell=True) == 0:
                print('icu-56 has been installed!')
            else:
                print('Problem during make icu-56')
        else:
            print('Problem during configure icu-56')


def cnode(v8Path):
    VARS = DIRS.copy()
    VARS.update(FILES)
    VARS['v8'] = fullPath(v8Path)

    commands = [
        'g++ -c -o {obj}/{ov8runner} -fpic {src}/v8runner.cpp -I{include} -I{v8}/include/ -Wall -Werror -std=c++17'.format(**VARS),
        'g++ -shared -o {lib}/{libv8runner} {obj}/{ov8runner} {v8}/out.gn/x64.release/obj/v8_libplatform/*.o {v8}/out.gn/x64.release/obj/v8_libbase/*.o -L{icu56}/source/lib -lpthread -licuuc -licui18n -licuio -licudata -Wall -Werror -std=c++17'.format(**VARS),
        'g++ -o {bin}/{cnode} -I{include} -I{v8}/include/ -I{erlangInclude} -L{icu56}/source/lib -L{lib} -L{v8}/out.gn/x64.release/ -L{erlangLibs} cnode_main.cpp {src}/cnode.cpp -lerl_interface -lei -lnsl -lpthread -licuuc -licui18n -licuio -licudata {lib}/{libv8runner} -lv8 -std=c++17 -lstdc++fs -Wall -Werror -Wno-write-strings -Wl,-rpath-link,{v8}/out.gn/x64.release/'.format(**VARS),
    ]

    for command in commands:
        print(command)
        os.system(command)

    # TODO: set LD_LIBRARY_PATH /home/eduardb/Documents/work/corezoid/v8/icu-56/source/lib:/home/eduardb/Documents/work/corezoid/v8/wrapper/lib:/home/eduardb/Documents/work/corezoid/v8/v8/out.gn/x64.release


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
    else:
        help()