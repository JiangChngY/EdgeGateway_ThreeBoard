#!/usr/bin/env python3
"""Run synthetic Qt tests with ARM binaries + frozen target libraries on Xvfb."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

sdk = (Path.home() / 'edgegateway_desktop_sdk')
module = (Path.home() / 'edgegateway_desktop')
runtime = sdk/'sysroot'

def build(source, project, output, log):
    output.mkdir(parents=True, exist_ok=True)
    with log.open('w') as stream:
        subprocess.run([str(sdk/'bin/qmake'), str(source/project),
                        'CONFIG+=release', 'CONFIG-=debug'], cwd=output,
                       stdout=stream, stderr=subprocess.STDOUT, check=True)
        subprocess.run(['make', '-j2'], cwd=output, stdout=stream,
                       stderr=subprocess.STDOUT, check=True)

def execute(binary, cwd, output, extra):
    output.mkdir(parents=True, exist_ok=True)
    options = {
        'LD_LIBRARY_PATH': f'{runtime}/lib:{runtime}/usr/lib:{runtime}/vendor/lib',
        'LD_BIND_NOW': '1', 'QT_PLUGIN_PATH': f'{runtime}/usr/lib/plugins',
        'QML2_IMPORT_PATH': f'{runtime}/usr/lib/qml', 'QT_QPA_PLATFORM': 'xcb',
        'QT_X11_NO_MITSHM': '1', 'QT_XCB_GL_INTEGRATION': 'none',
        'QT_QUICK_BACKEND': 'software', 'QT_QPA_FONTDIR': f'{runtime}/usr/share/fonts',
        'FONTCONFIG_FILE': f'{runtime}/etc/fonts/fonts.conf',
        'XDG_RUNTIME_DIR': f'{sdk}/smoke/run', 'XDG_CACHE_HOME': f'{sdk}/smoke/cache',
        'EDGE_DESKTOP_CONFIG': f'{sdk}/scripts/smoke.ini',
        'EDGE_VENDOR_CAPTURE': str(output),
    }
    command = ['xvfb-run', '-a', '-s', '-screen 0 1280x800x24 -nolisten tcp',
               'qemu-arm', '-L', str(runtime)]
    for key, value in options.items():
        command += ['-E', f'{key}={value}']
    command += [str(binary), *extra]
    result = subprocess.run(command, cwd=cwd, capture_output=True, timeout=60)
    text = result.stdout.decode(errors='replace')+result.stderr.decode(errors='replace')
    (output/'runtime.log').write_text(text)
    print(text, flush=True)
    assert result.returncode == 0, f'ARM test exited {result.returncode}'
    return text

parser = argparse.ArgumentParser()
parser.add_argument('mode', choices=('desktop', 'vendor'))
args = parser.parse_args()
if args.mode == 'desktop':
    output = sdk/'tests/desktop'
    output.mkdir(parents=True, exist_ok=True)
    directory = sdk/'build-desktop-test'
    build(module, 'test_ui.pro', directory, output/'build.log')
    text = execute(directory/'edge-ui-test', directory, output, [str(output)])
    assert 'PASS: tab taps' in text
else:
    output = sdk/'tests/vendor'
    output.mkdir(parents=True, exist_ok=True)
    source = sdk/'systemui-test-source'
    shutil.copytree(sdk/'systemui-source/systemui', source, dirs_exist_ok=True)
    shutil.copy2(module/'test_vendor_driver.h', source/'test_vendor_driver.h')
    main = source/'main.cpp'
    text = main.read_text()
    assert '#include <QQmlApplicationEngine>' in text and '    engine.load(url);' in text
    main.write_text(text.replace('#include <QQmlApplicationEngine>',
                    '#include <QQmlApplicationEngine>\n#include "test_vendor_driver.h"', 1)
                   .replace('    engine.load(url);',
                            '    engine.load(url);\n    edgeVendorTest(&engine);', 1))
    with (source/'systemui.pro').open('a') as project:
        project.write('\nQT += testlib\nQMAKE_POST_LINK =\n')
    resources = sdk/'systemui-resources'
    resources.mkdir(exist_ok=True)
    subprocess.run(['rsync', '-a', '--chmod=u+rwX', '/srv/nfs/edgegateway/mp157/opt/ui/resource/',
                    str(resources)+'/'], check=True)
    directory = sdk/'build-systemui-test'
    build(source, 'systemui.pro', directory, output/'build.log')
    if not (directory/'resource').exists():
        (directory/'resource').symlink_to(resources, target_is_directory=True)
    text = execute(directory/'systemui', directory, output, [])
    assert 'PASS: vendor desktop' in text
for error in ('ReferenceError:', 'TypeError:', 'is not a type', 'is not installed',
              'Cannot open: qrc:/edgegateway', 'symbol lookup error', 'version `'):
    assert error not in text, f'Unexpected runtime error: {error}'
print('ARM_QEMU_UI_REGRESSION_PASS', args.mode)
