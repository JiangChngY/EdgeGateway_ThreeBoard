#!/usr/bin/env python3
"""Reconstruct a limited Qt application SDK, not a Qt library build SDK.

All target QT_FEATURE values come from the installed target mkspecs metadata.
Architecture-dependent desktop qconfig.h is deliberately not reused.
The application uses public Qt APIs; private-header compatibility is not promised.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

sdk = Path(sys.argv[1]).resolve()
assert sdk == (Path.home() / 'edgegateway_desktop_sdk')
target = Path('/srv/nfs/edgegateway/mp157')
host = Path('/opt/Qt5.12.9/5.12.9/gcc_64')
src = host.parent / 'Src'
chain = sdk / 'toolchains/gcc-arm-9.2-2019.12-x86_64-arm-none-linux-gnueabihf'
compiler = chain / 'bin/arm-none-linux-gnueabihf-g++'
libc = chain / 'arm-none-linux-gnueabihf/libc'
sysroot = sdk / 'sysroot'
qt = sdk / 'qt'
assert compiler.is_file() and (libc/'usr/include/features.h').is_file()
for path in (sysroot, qt, sdk/'bin', sdk/'logs'):
    path.mkdir(parents=True, exist_ok=True)

def sync(source, dest):
    dest.mkdir(parents=True, exist_ok=True)
    excludes = ['--exclude=firmware/', '--exclude=modules/'] if source == target/'lib' else []
    if source == target/'usr/lib':
        excludes += ['--exclude=/python*/']
    subprocess.run(['rsync', '-a', '--chmod=u+rwX', *excludes,
                    str(source)+'/', str(dest)+'/'], check=True)

# Only independent copies are modified. No hard links back to the NFS runtime.
sync(libc/'usr/include', sysroot/'usr/include')
for part in ('lib', 'usr/lib', 'vendor/lib', 'usr/share/fonts', 'usr/share/qt5', 'etc/fonts'):
    if (target/part).is_dir():
        sync(target/part, sysroot/part)
# Rootfs ships only the libgcc SONAME; add the SDK-only linker alias so the
# compiler cannot silently pick its bundled libgcc_s runtime instead.
gcc_alias = sysroot/'usr/lib/libgcc_s.so'
if not gcc_alias.exists():
    assert (sysroot/'lib/libgcc_s.so.1').exists()
    gcc_alias.symlink_to('../../lib/libgcc_s.so.1')
sync(host/'include', qt/'include')
sync(src/'qtbase/mkspecs', qt/'mkspecs')
sync(target/'usr/lib/mkspecs', qt/'mkspecs')
supplemental_modules = []
for module, library in (('multimedia', 'Qt5Multimedia'), ('virtualkeyboard', 'Qt5VirtualKeyboard')):
    metadata = qt/f'mkspecs/modules/qt_lib_{module}.pri'
    if not (target/f'usr/lib/mkspecs/modules/qt_lib_{module}.pri').exists():
        original = host/f'mkspecs/modules/qt_lib_{module}.pri'
        content = original.read_text()
        # Both same-version public modules have no configurable public features
        # or system-library uses. Private/backend metadata is NOT copied.
        assert re.search(rf'^QT\.{module}\.VERSION = 5\.12\.9$', content, re.M)
        for field in ('enabled_features', 'disabled_features', 'uses'):
            assert re.search(rf'^QT\.{module}\.{field} =\s*$', content, re.M)
        assert (sysroot/'usr/lib'/f'lib{library}.so.5.12.9').is_file()
        # QT_CONFIG tokens such as alsa/pulseaudio describe the host build's
        # backend and are deliberately excluded from this public declaration.
        content = '\n'.join(line for line in content.splitlines()
                            if not line.lstrip().startswith('QT_CONFIG'))+'\n'
        metadata.write_text('# SDK-only public declaration from official Qt 5.12.9; target DSO verified.\n'+content)
        supplemental_modules.append(module)
    link = sysroot/'usr/lib'/f'lib{library}.so'
    if not link.exists():
        link.symlink_to(f'lib{library}.so.5.12.9')
for part in ('GLES2', 'GLES3', 'EGL', 'KHR'):
    sync(src/'qtbase/src/3rdparty/angle/include'/part, sysroot/'usr/include'/part)

# Make absolute in-root symlinks relocatable, in the SDK copy only.
for base in (sysroot, qt/'include'):
    for directory, _, files in os.walk(base):
        for name in files:
            link = Path(directory)/name
            if link.is_symlink() and os.readlink(link).startswith('/'):
                destination = base / os.readlink(link).lstrip('/')
                if destination.exists():
                    link.unlink()
                    link.symlink_to(os.path.relpath(destination, link.parent))

def read_pri(path):
    out = {}
    for line in path.read_text().replace('\\\n', ' ').splitlines():
        m = re.match(r'^\s*([\w.]+)\s*(\+?=)\s*(.*)$', line)
        if m:
            out[m[1]] = ((out.get(m[1], '')+' ') if m[2] == '+=' else '')+m[3].strip()
    return out

def feature_macros(values, prefix):
    result = {}
    for suffix, flag in (('enabled_features', 1), ('disabled_features', -1)):
        for feature in values.get(prefix+'.'+suffix, '').split():
            result[re.sub(r'[^a-zA-Z0-9_]', '_', feature)] = flag
    return result

def write_config(path, features, extra=''):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text('// Generated from target Qt 5.12.9 mkspecs; no desktop feature defaults.\n'+
                    ''.join(f'#define QT_FEATURE_{name} {flag}\n' for name, flag in sorted(features.items()))+extra)

global_values = read_pri(target/'usr/lib/mkspecs/qconfig.pri')
assert global_values['QT_VERSION'] == '5.12.9'
assert global_values['QT_ARCH'] == 'arm'
global_features = feature_macros(global_values, 'QT.global')
global_features['cross_compile'] = 1
private_values = read_pri(target/'usr/lib/mkspecs/qmodule.pri')
assert private_values.get('QT_COORD_TYPE') == 'double'
assert 'reduce_relocations' in private_values['QT.global_private.disabled_features'].split()
write_config(qt/'include/QtCore/qconfig.h', global_features,
             '#define QT_VERSION_STR "5.12.9"\n#define QT_VERSION_MAJOR 5\n'
             '#define QT_VERSION_MINOR 12\n#define QT_VERSION_PATCH 9\n'
             '#define QT_LARGEFILE_SUPPORT 64\n#define QT_VISIBILITY_AVAILABLE true\n')
for header in (qt/'include/QtCore').glob('**/qconfig_p.h'):
    write_config(header, feature_macros(private_values, 'QT.global_private'))

generated = []
all_target_features = dict(global_features)
all_target_features.update(feature_macros(private_values, 'QT.global_private'))
qt_config_tokens = set(global_values.get('QT_CONFIG', '').split())
for pri in sorted((target/'usr/lib/mkspecs/modules').glob('qt_lib_*.pri')):
    values = read_pri(pri)
    name_keys = [key for key in values if key.endswith('.name')]
    if not name_keys:
        continue
    key = name_keys[0][:-5]
    module_name = values[key+'.name']
    features = feature_macros(values, key)
    qt_config_tokens.update(values.get('QT_CONFIG', '').split())
    for feature, state in features.items():
        if feature in all_target_features and all_target_features[feature] != state:
            raise RuntimeError(f'Conflicting target feature metadata: {feature}')
        all_target_features[feature] = state
    private = key.endswith('_private')
    # Derive the actual config filename from the matching official host header.
    candidates = list((qt/'include'/module_name).glob('**/*-config'+('_p' if private else '')+'.h'))
    for header in candidates:
        if not private and '/5.12.9/' in str(header):
            continue
        aliases = ''
        if module_name == 'QtGui' and not private:
            # These public legacy defines are specified by qtbase/src/gui/configure.json.
            for feature, macros in {
                'opengles2': ('QT_OPENGL_ES', 'QT_OPENGL_ES_2'),
                'opengles3': ('QT_OPENGL_ES_3',),
                'opengles31': ('QT_OPENGL_ES_3_1',),
                'opengles32': ('QT_OPENGL_ES_3_2',),
                'angle': ('QT_OPENGL_ES_2_ANGLE',),
                'dynamicgl': ('QT_OPENGL_DYNAMIC',),
            }.items():
                if features.get(feature) == 1:
                    aliases += ''.join(f'#define {macro} true\n' for macro in macros)
        write_config(header, features, aliases)
        generated.append(str(header.relative_to(qt)))

# Replay public compatibility aliases using Qt's own configure declarations.
# A privateFeature can intentionally emit a public legacy QT_NO_* define.
legacy = {}
unresolved = []
for configure in src.glob('*/**/configure.json'):
    if '3rdparty' in configure.parts:
        continue
    try:
        definitions = json.loads(configure.read_text(), strict=False).get('features', {})
    except (ValueError, UnicodeError):
        continue
    for name, definition in definitions.items():
        outputs = [({'type': item} if isinstance(item, str) else item)
                   for item in definition.get('output', [])]
        state = all_target_features.get(re.sub(r'[^a-zA-Z0-9_]', '_', name))
        for output in outputs:
            if output.get('type') in ('publicFeature', 'privateFeature'):
                state = all_target_features.get(re.sub(r'[^a-zA-Z0-9_]', '_', output.get('name', name)), state)
        if state is None and any(item.get('type') in ('feature', 'publicQtConfig')
                                 and item.get('name', name) in qt_config_tokens for item in outputs):
            state = 1
        if state is None:
            continue
        for output in outputs:
            kind = output.get('type')
            if kind not in ('feature', 'define'):
                continue
            if output.get('condition'):
                unresolved.append({'file': str(configure.relative_to(src)), 'feature': name, 'output': output})
                continue
            if kind == 'feature' and state == -1:
                macro = 'QT_NO_'+output.get('name', name).replace('-', '_').upper()
                legacy[macro] = ''
            elif kind == 'define' and ((state == 1) != output.get('negative', False)):
                value = output.get('value', 'true')
                if not re.fullmatch(r'true|false|[0-9]+|"[^"\n]*"', str(value)):
                    unresolved.append({'file': str(configure.relative_to(src)), 'feature': name, 'output': output})
                    continue
                legacy[output['name']] = value
with (qt/'include/QtCore/qconfig.h').open('a') as config:
    config.write('// Public compatibility aliases from official configure.json outputs.\n')
    for macro, value in sorted(legacy.items()):
        config.write(f'#ifndef {macro}\n#define {macro} {value}\n#endif\n')

# A copied host qmake is a host tool; its qt.conf and mkspec now target ARM.
shutil.copy2(host/'bin/qmake', sdk/'bin/qmake')
(sdk/'bin/qt.conf').write_text(f'''[Paths]
Prefix={qt}
Headers={qt}/include
Libraries={sysroot}/usr/lib
ArchData={qt}
Data={qt}
Binaries={host}/bin
LibraryExecutables={host}/libexec
HostBinaries={host}/bin
HostData={qt}
HostLibraries={host}/lib
HostPrefix={host}
Plugins={sysroot}/usr/lib/plugins
Qml2Imports={sysroot}/usr/lib/qml
Sysroot={sysroot}
SysrootifyPrefix=false
HostSpec=linux-g++
TargetSpec=linux-edge-arm-g++
''')
spec = qt/'mkspecs/linux-edge-arm-g++'
spec.mkdir(exist_ok=True)
(spec/'qmake.conf').write_text(f'''MAKEFILE_GENERATOR = UNIX
CONFIG += incremental
QMAKE_INCREMENTAL_STYLE = sublib
include(../common/linux.conf)
include(../common/gcc-base-unix.conf)
include(../common/g++-unix.conf)
QMAKE_CC = {chain}/bin/arm-none-linux-gnueabihf-gcc
QMAKE_CXX = {compiler}
QMAKE_LINK = $$QMAKE_CXX
QMAKE_LINK_SHLIB = $$QMAKE_CXX
QMAKE_AR = {chain}/bin/arm-none-linux-gnueabihf-ar cqs
QMAKE_OBJCOPY = {chain}/bin/arm-none-linux-gnueabihf-objcopy
QMAKE_NM = {chain}/bin/arm-none-linux-gnueabihf-nm -P
QMAKE_STRIP = {chain}/bin/arm-none-linux-gnueabihf-strip
QMAKE_CFLAGS += -mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard
QMAKE_CXXFLAGS += $$QMAKE_CFLAGS
QMAKE_LFLAGS += -L{sysroot}/usr/lib -Wl,-rpath-link,{sysroot}/usr/lib -Wl,-rpath-link,{sysroot}/lib -Wl,-rpath-link,{sysroot}/vendor/lib
QMAKE_LIBS_OPENGL_ES2 = -lGLESv2
QMAKE_LIBS_EGL = -lEGL
load(qt_config)
''')
shutil.copy2(qt/'mkspecs/linux-g++/qplatformdefs.h', spec/'qplatformdefs.h')

manifest = {'scope': 'Public-API application SDK; not for rebuilding Qt or private-API plugins',
            'qt_version': '5.12.9', 'target': str(target), 'compiler': str(compiler),
            'target_qconfig': global_values, 'generated_config_headers': generated,
            'target_qmodule': private_values,
            'legacy_config_defines': legacy, 'unresolved_dynamic_config_outputs': unresolved,
            'supplemental_public_modules': supplemental_modules,
            'source_hashes': {}}
for relative in ('usr/lib/libQt5Core.so.5.12.9', 'usr/lib/libQt5Quick.so.5.12.9',
                 'lib/libc-2.31.so', 'usr/lib/libstdc++.so.6.0.28'):
    manifest['source_hashes'][relative] = hashlib.sha256((target/relative).read_bytes()).hexdigest()
(sdk/'sdk-manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
subprocess.run([str(sdk/'bin/qmake'), '-query'], check=True)
print('SDK_PREPARED', sdk, flush=True)
