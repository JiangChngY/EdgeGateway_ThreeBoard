#!/usr/bin/env python3
"""Build and click-test a disposable copy of the vendor desktop using Xvfb."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from integrate_systemui import integrate

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('vendor_source', type=Path)
    parser.add_argument('resources', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    # All edits are generated test fixtures in our own temporary directory.
    with tempfile.TemporaryDirectory(prefix='edge-vendor-test-') as temporary:
        root = Path(temporary)
        source, build = root / 'systemui', root / 'build'
        shutil.copytree(args.vendor_source, source)
        integrate(source)
        shutil.copy2(Path(__file__).with_name('test_vendor_driver.h'), source)
        main_cpp = source / 'main.cpp'
        content = main_cpp.read_text(encoding='utf-8')
        content = content.replace('#include <QQmlApplicationEngine>',
            '#include <QQmlApplicationEngine>\n#include "test_vendor_driver.h"', 1)
        content = content.replace('    engine.load(url);',
            '    engine.load(url);\n    edgeVendorTest(&engine);', 1)
        main_cpp.write_text(content, encoding='utf-8')
        with (source / 'systemui.pro').open('a') as file:
            file.write('\nQT += testlib\n')
        build.mkdir(); (root / 'ui').mkdir()
        (build / 'resource').symlink_to(args.resources.resolve(), target_is_directory=True)
        env = dict(os.environ, QT_QPA_PLATFORM='xcb', QT_QUICK_BACKEND='software',
                   EDGE_VENDOR_CAPTURE=str(output), EDGE_DESKTOP_CONFIG=str(root / 'desktop.ini'))
        (root / 'desktop.ini').write_text(
            '[serial]\nauto_open=false\n[storage]\ndatabase=' + str(root / 'samples.db') + '\n')
        with (output / 'build.log').open('w') as log:
            subprocess.run(['qmake', str(source / 'systemui.pro')], cwd=build,
                           stdout=log, stderr=subprocess.STDOUT, check=True)
            subprocess.run(['make', '-s', '-j4'], cwd=build,
                           stdout=log, stderr=subprocess.STDOUT, check=True)
        proc = subprocess.run(['xvfb-run', '-a', '-s', '-screen 0 1024x600x24', './systemui'],
                              cwd=build, env=env, capture_output=True, timeout=30)
        log = proc.stdout.decode(errors='replace') + proc.stderr.decode(errors='replace')
        (output / 'runtime.log').write_text(log, encoding='utf-8')
        print(log)
        if proc.returncode or 'PASS: vendor desktop' not in log:
            raise RuntimeError('Vendor UI regression failed: ' + str(proc.returncode))
        for error in ('ReferenceError:', 'TypeError:', 'is not a type', 'is not installed', 'Cannot open: qrc:/edgegateway'):
            if error in log:
                raise RuntimeError('Unexpected QML runtime error: ' + error)

if __name__ == '__main__':
    main()
