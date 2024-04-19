# Copyright 2024 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

# -*- coding: utf-8 -*-
import os

name = 'mcrt_computation'

if 'early' not in locals() or not callable(early):
    def early(): return lambda x: x

@early()
def version():
    """
    Increment the build in the version.
    """
    _version = '14.1'
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

variants = [
    ['os-CentOS-7', 'opt_level-optdebug', 'refplat-vfx2021.0', 'gcc-9.3.x.1'],
    ['os-CentOS-7', 'opt_level-debug', 'refplat-vfx2021.0', 'gcc-9.3.x.1'],
    ['os-CentOS-7', 'opt_level-optdebug', 'refplat-vfx2021.0', 'clang-13'],

    ['os-CentOS-7', 'opt_level-optdebug', 'refplat-vfx2022.0', 'gcc-9.3.x.1'],
    ['os-CentOS-7', 'opt_level-debug', 'refplat-vfx2022.0', 'gcc-9.3.x.1'],

    ['os-rocky-9', 'opt_level-optdebug', 'refplat-vfx2021.0', 'gcc-9.3.x.1'],
    ['os-rocky-9', 'opt_level-debug', 'refplat-vfx2021.0', 'gcc-9.3.x.1'],

    ['os-rocky-9', 'opt_level-optdebug', 'refplat-vfx2022.0', 'gcc-9.3.x.1'],
    ['os-rocky-9', 'opt_level-debug', 'refplat-vfx2022.0', 'gcc-9.3.x.1'],

    ['os-rocky-9', 'opt_level-optdebug', 'refplat-vfx2023.0', 'gcc-11.x'],
    ['os-rocky-9', 'opt_level-debug', 'refplat-vfx2023.0', 'gcc-11.x'],
]

conf_rats_variants = variants[0:2]
conf_CI_variants = list(filter(lambda v: 'os-CentOS-7' in v, variants))

requires = [
    'arras4_core-4.10',
    'mcrt_dataio-14.0',
    'mcrt_messages-13.0',
    'moonray-16.1',
    'scene_rdl2-14.0',
]

private_build_requires = [
    'cmake_modules-1.0',
    'cppunit',
]

commandstr = lambda i: "cd build/"+os.path.join(*variants[i])+"; ctest -j $(nproc)"
testentry = lambda i: ("variant%d" % i,
                       { "command": commandstr(i),
                         "requires": ["cmake-3.23"] + variants[i] } )
testlist = [testentry(i) for i in range(len(variants))]
tests = dict(testlist)

def commands():
    prependenv('CMAKE_PREFIX_PATH', '{root}')
    prependenv('LD_LIBRARY_PATH', '{root}/lib64:{root}/dso')
    prependenv('PATH', '{root}/bin')

uuid = '9b2f7154-23bd-4b97-9aa9-1fcdaf6d2a05'

config_version = 0
