#!/usr/bin/env python3
"""Prepare an isolated public-API Qt 5.12.9 application SDK for the 6ULL root.

Uses the existing MP157 SDK's same-version public Qt headers/host tools and
compiler headers, but ALL target libraries/CRT come from 6ULL. Only suitable
for this Core/Network/SQL app, with ABI verification and target-library QEMU
tests required. Does not modify either board runtime or rebuild Qt/plugins.
"""
from pathlib import Path
import json
import hashlib
import shutil
import subprocess

base = Path.home()/'edgegateway_desktop_sdk'
sdk = Path.home()/'edgegateway_imx6ull_sdk'
target = Path('/srv/nfs/edgegateway/imx6ull')
root = sdk/'sysroot'
assert base.is_dir() and (target/'lib/libc-2.23.so').is_file()
sdk.mkdir(exist_ok=True)
for sub in ('lib', 'usr/lib'):
    dest = root/sub
    dest.mkdir(parents=True, exist_ok=True)
    subprocess.run(['rsync', '-a', '--exclude=modules/', '--exclude=firmware/',
                    '--exclude=python*/', '--exclude=cups/', str(target/sub)+'/', str(dest)+'/'], check=True)
# The compiler's development headers are separate from the older target DSOs.
chain = base/'toolchains/gcc-arm-9.2-2019.12-x86_64-arm-none-linux-gnueabihf'
shutil.copytree(chain/'arm-none-linux-gnueabihf/libc/usr/include', root/'usr/include', dirs_exist_ok=True)
shutil.copytree(base/'qt', sdk/'qt', symlinks=True, dirs_exist_ok=True)
(sdk/'bin').mkdir(exist_ok=True)
shutil.copy2(base/'bin/qmake', sdk/'bin/qmake')
(sdk/'bin/qt.conf').write_text((base/'bin/qt.conf').read_text().replace(str(base), str(sdk)))
# Header and qmake metadata paths change; toolchain remains the shared compiler.
for path in (sdk/'qt/mkspecs').rglob('*'):
    if path.is_file() and path.suffix in ('.conf', '.pri', '.prf'):
        text = path.read_text()
        text = text.replace(str(base), str(sdk)).replace(str(sdk/'toolchains'), str(base/'toolchains'))
        path.write_text(text)
# Ensure the target libgcc DSO is selected before the compiler's newer copy.
link = root/'usr/lib/libgcc_s.so'
if link.is_symlink() or link.exists(): link.unlink()
link.symlink_to('../../lib/libgcc_s.so.1')
manifest = {'target': str(target), 'compiler': str(chain),
            'headers': 'Qt 5.12.9 public headers and compiler GCC 9.2.1/glibc 2.30 headers',
            'runtime': '6ULL Qt 5.12.9 / glibc 2.23 / libstdc++ 6.0.21',
            'limits': 'Core/Network/SQL public API only; require versioned import verification and QEMU integration test',
            'target_hashes': {}}
for rel in ('lib/libc-2.23.so', 'usr/lib/libQt5Core.so.5.12.9', 'usr/lib/libQt5Network.so.5.12.9',
            'usr/lib/libQt5Sql.so.5.12.9', 'usr/lib/libstdc++.so.6.0.21'):
    manifest['target_hashes'][rel] = hashlib.sha256((target/rel).read_bytes()).hexdigest()
(sdk/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
print('IMX6ULL_APPLICATION_SDK_PREPARED')
