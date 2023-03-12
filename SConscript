Import('env')

import os

AddOption('--arrascodecov',
          dest='arrascodecov',
          type='string',
          action='store',
          help='Build with codecov instrumentation')

if GetOption('arrascodecov') != None:
    env['CXXFLAGS'].append('-prof_gen:srcpos,threadsafe')
    env['CXXFLAGS'].append('-prof_dir%s' % GetOption('arrascodecov'))
    env.CacheDir(None)

env.Tool('component')
env.Tool('dwa_install')
env.Tool('dwa_run_test')
env.Tool('dwa_utils')
env['DOXYGEN_LOC'] = '/rel/third_party/doxygen/1.8.11/bin/doxygen'
env.Tool('dwa_doxygen')
env['DOXYGEN_CONFIG']['DOT_GRAPH_MAX_NODES'] = '200'
env['CPPCHECK_LOC'] = '/rel/third_party/cppcheck/1.71/cppcheck'
env.Tool('cppcheck')
env.Tool('python_sdk')

from dwa_sdk import DWALoadSDKs
DWALoadSDKs(env)

env.AppendUnique(ISPCFLAGS = ['--dwarf-version=2', '--wno-perf'])
# When building opt-debug, ignore ISPC performance warnings
if env['TYPE_LABEL'] == 'opt-debug':
    env.AppendUnique(ISPCFLAGS = ['--wno-perf', '--werror'])

env.Tool('vtune')

env.EnvCleanUp()

# Suppress depication warning from tbb-2020.
env.AppendUnique(CPPDEFINES='TBB_SUPPRESS_DEPRECATED_MESSAGES')

# For SBB integration - SBB uses the keyword "restrict", so the compiler needs to enable it. 
if "icc" in env['COMPILER_LABEL']:
    env['CXXFLAGS'].append('-restrict')

env.Replace(USE_OPENMP = [])

#Set optimization level for debug -O0
#icc defaults to -O2 for debug and -O3 for opt/opt-debug
if env['TYPE_LABEL'] == 'debug':
    env.AppendUnique(CCFLAGS = ['-O0'])
    if 'icc' in env['COMPILER_LABEL'] and '-inline-level=0' not in env['CXXFLAGS']:
        env['CXXFLAGS'].append('-inline-level=0')

# don't error on writes to static vars
if 'icc' in env['COMPILER_LABEL'] and '-we1711' in env['CXXFLAGS']:
    env['CXXFLAGS'].remove('-we1711')

# For Arras, we've made the decision to part with the studio standards and use #pragma once
# instead of include guards. We disable warning 1782 to avoid the related (incorrect) compiler spew.
if 'icc' in env['COMPILER_LABEL'] and '-wd1782' not in env['CXXFLAGS']:
    env['CXXFLAGS'].append('-wd1782')       # #pragma once is obsolete. Use #ifndef guard instead.
if 'gcc' in env['COMPILER_LABEL']:
    env['CCFLAGS'].append('-Wno-unknown-pragmas')
    env['CCFLAGS'].append('-Wno-sign-compare')

env.TreatAllWarningsAsErrors()

if 'gcc' in env['CC']:
    env.AppendUnique(CXXFLAGS=['-Wno-class-memaccess'])

# Create include/mcrt_computation link to ../lib in the build directory.
Execute("mkdir -p include")
Execute("rm -f include/mcrt_computation")
Execute("ln -sfv ../lib include/mcrt_computation")
env.Append(CPPPATH=[env.Dir('#include')])
env.Append(CPPPATH=[env.Dir('$INSTALL_DIR/include')])
env.Append(CPPPATH=[env.Dir('include')])

env.DWASConscriptWalk(topdir='#computation', ignore=[])
env.DWASConscriptWalk(topdir='#cmd', ignore=[])
env.DWASConscriptWalk(topdir='#doc', ignore=[])
env.DWASConscriptWalk(topdir='#lib', ignore=[])

env.DWAResolveUndefinedComponents([])
env.DWAFillInMissingInitPy()
env.DWAFreezeComponents()

# Set default target
env.Default(env.Alias('@install'))


