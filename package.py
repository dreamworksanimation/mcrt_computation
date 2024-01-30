# Copyright 2024 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

# -*- coding: utf-8 -*-
import os, sys

unittestflags = (['@run_all', '--unittest-xml']
                 if os.environ.get('BROKEN_CUSTOM_ARGS_UNITTESTS') else [])

name = 'mcrt_computation'

if 'early' not in locals() or not callable(early):
    def early(): return lambda x: x

@early()
def version():
    """
    Increment the build in the version.
    """
    _version = '13.12'
    from rezbuild import earlybind
    return earlybind.version(this, _version)

description = 'Mcrt Computations'

authors = [
    'PSW Rendering and Shading',
    'moonbase-dev@dreamworks.com',
    'Toshi.Kato@dreamworks.com'
]

help = ('For assistance, '
        "please contact the folio's owner at: moonbase-dev@dreamworks.com")

if 'scons' in sys.argv:
    build_system = 'scons'
    build_system_pbr = 'bart_scons-10'
else:
    build_system = 'cmake'
    build_system_pbr = 'cmake_modules'

variants = [
    ['os-CentOS-7', 'opt_level-optdebug', 'refplat-vfx2021.0', 'gcc-9.3.x.1'],
    ['os-CentOS-7', 'opt_level-debug', 'refplat-vfx2021.0', 'gcc-9.3.x.1'],
    ['os-CentOS-7', 'opt_level-optdebug', 'refplat-vfx2021.0', 'clang-13'],

    ['os-CentOS-7', 'opt_level-optdebug', 'refplat-vfx2022.0', 'gcc-9.3.x.1'],
    ['os-CentOS-7', 'opt_level-debug', 'refplat-vfx2022.0', 'gcc-9.3.x.1'],

    ['os-CentOS-7', 'opt_level-optdebug', 'refplat-vfx2020.3', 'icc-19.0.5.281.x.2'],
    ['os-CentOS-7', 'opt_level-debug', 'refplat-vfx2020.3', 'icc-19.0.5.281.x.2'],
    ['os-CentOS-7', 'opt_level-optdebug', 'refplat-vfx2020.3', 'gcc-6.3.x.2'],
    ['os-CentOS-7', 'opt_level-debug', 'refplat-vfx2020.3', 'gcc-6.3.x.2'],

    ['os-rocky-9', 'opt_level-optdebug', 'refplat-vfx2021.0', 'gcc-9.3.x.1'],
    ['os-rocky-9', 'opt_level-debug', 'refplat-vfx2021.0', 'gcc-9.3.x.1'],

    ['os-rocky-9', 'opt_level-optdebug', 'refplat-vfx2022.0', 'gcc-9.3.x.1'],
    ['os-rocky-9', 'opt_level-debug', 'refplat-vfx2022.0', 'gcc-9.3.x.1'],

    ['os-rocky-9', 'opt_level-optdebug', 'refplat-vfx2023.0', 'gcc-11.x'],
    ['os-rocky-9', 'opt_level-debug', 'refplat-vfx2023.0', 'gcc-11.x'],
]

conf_rats_variants = variants[0:2]
conf_CI_variants = list(filter(lambda v: 'os-CentOS-7' in v, variants))

scons_targets = ['@install'] + unittestflags
sconsTargets = {
    'refplat-vfx2020.3': scons_targets,
    'refplat-vfx2021.0': scons_targets,
    'refplat-vfx2022.0': scons_targets,
    'refplat-vfx2023.0': scons_targets,
}

requires = [
    'arras4_core-4.10',
    'mcrt_dataio-13.10',
    'mcrt_messages-12.2',
    'moonray-15.12',
    'scene_rdl2-13.7',
]

private_build_requires = [
    build_system_pbr,
    'cppunit',
]

commandstr = lambda i: "cd build/"+os.path.join(*variants[i])+"; ctest -j $(nproc)"
testentry = lambda i: ("variant%d" % i,
                       { "command": commandstr(i),
                         "requires": ["cmake-3.23"] + variants[i] } )
testlist = [testentry(i) for i in range(len(variants))]
tests = dict(testlist)

if build_system is 'cmake':
    def commands():
        prependenv('CMAKE_PREFIX_PATH', '{root}')
        prependenv('LD_LIBRARY_PATH', '{root}/lib64:{root}/dso')
        prependenv('PATH', '{root}/bin')
else:
    def commands():
        prependenv('LD_LIBRARY_PATH', '{root}/lib:{root}/dso')
        prependenv('PATH', '{root}/bin')

uuid = '9b2f7154-23bd-4b97-9aa9-1fcdaf6d2a05'

config_version = 0
