# Copyright 2024-2025 DreamWorks Animation LLC
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
    _version = '15.31'
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
    [   # variant 0
        'os-rocky-9',
        'opt_level-optdebug',
        'refplat-vfx2023.1',
        'gcc-11.x'
    ],
    [   # variant 1
        'os-rocky-9',
        'opt_level-debug',
        'refplat-vfx2023.1',
        'gcc-11.x'
    ],
    [   # variant 2
        'os-rocky-9',
        'opt_level-optdebug',
        'refplat-vfx2023.1',
        'clang-17.0.6.x'
    ],
    [   # variant 3
        'os-rocky-9',
        'opt_level-optdebug',
        'refplat-vfx2024.0',
        'gcc-11.x'
    ],
    [   # variant 4
        'os-rocky-9',
        'opt_level-optdebug',
        'refplat-vfx2025.0',
        'gcc-11.x'
    ],
    [   # variant 5
        'os-rocky-9',
        'opt_level-optdebug',
        'refplat-houdini21.0',
        'gcc-11.x'
    ],
    [   # variant 6
        'os-rocky-9',
        'opt_level-optdebug',
        'refplat-vfx2022.0',
        'gcc-9.3.x.1'
    ],
]

conf_rats_variants = variants[0:2]
conf_CI_variants = variants

# Add ephemeral package to each variant.
for i, variant in enumerate(variants):
    variant.insert(0, '.mcrt_computation_variant-%d' % i)

requires = [
    'arras4_core-4.10',
    'mcrt_dataio-15.19',
    'mcrt_messages-14.8',
    'moonray-17.31',
    'scene_rdl2-15.18',
]

private_build_requires = [
    'cmake_modules-1.0',
    'cppunit',
]

commandstr = lambda i: "cd build/"+os.path.join(*variants[i])+"; ctest -j $(nproc)"
testentry = lambda i: ("variant%d" % i,
                       { "command": commandstr(i),
                        "requires": ["cmake"],
                        "on_variants": {
                            "type": "requires",
                            "value": [".mcrt_computation_variant-%d" % i],
                            },
                        "run_on": "explicit",
                        }, )
testlist = [testentry(i) for i in range(len(variants))]
tests = dict(testlist)

def commands():
    prependenv('CMAKE_PREFIX_PATH', '{root}')
    prependenv('LD_LIBRARY_PATH', '{root}/lib64:{root}/dso')
    prependenv('PATH', '{root}/bin')

uuid = '9b2f7154-23bd-4b97-9aa9-1fcdaf6d2a05'

config_version = 0
