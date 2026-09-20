#!/usr/bin/env python3
"""Collect production ARM artifacts only after validation has completed."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

sdk = (Path.home() / 'edgegateway_desktop_sdk')
target = Path('/srv/nfs/edgegateway/mp157')
output = sdk/'artifacts'
output.mkdir(exist_ok=True)
for directory in ('abi', 'logs', 'screenshots/desktop', 'screenshots/vendor'):
    (output/directory).mkdir(parents=True, exist_ok=True)
manifest = json.loads((sdk/'sdk-manifest.json').read_text())
for relative, expected in manifest['source_hashes'].items():
    actual = hashlib.sha256((target/relative).read_bytes()).hexdigest()
    if actual != expected:
        raise RuntimeError('Target library changed during SDK work: '+relative)
records = {}
for name, path in {
        'edge-desktop': sdk/'build-desktop/edge-desktop',
        'systemui': sdk/'build-systemui/systemui'}.items():
    data = path.read_bytes()
    if 'edgegateway.backend.v1'.encode('utf-16-le') not in data:
        raise RuntimeError('Missing final integration identity: '+name)
    result = subprocess.run(['python3', str(sdk/'scripts/verify_abi.py'), str(path), str(target)],
                            capture_output=True, text=True, check=True)
    if 'ABI_VERSIONED_IMPORTS_PASS' not in result.stdout:
        raise RuntimeError('ABI verification did not complete: '+name)
    (output/'abi'/f'{name}.log').write_text(result.stdout)
    abi = json.loads(result.stdout.split('ABI_VERSIONED_IMPORTS_PASS')[0])
    records[name] = {'sha256': hashlib.sha256(data).hexdigest(), 'size': len(data),
                     'direct_version_requirements': abi['requirements'][name],
                     'resolved_dependency_count': len(abi['libraries'])}
    shutil.copy2(path, output/name)
    shutil.copy2(path.parent/'edge-desktop.map', output/'abi'/f'{name}.map')
for mode in ('desktop', 'vendor'):
    log = sdk/'logs'/f'final-test-{mode}.log'
    text = log.read_text()
    if 'ARM_QEMU_UI_REGRESSION_PASS '+mode not in text:
        raise RuntimeError('Latest ARM UI regression has not passed: '+mode)
    shutil.copy2(log, output/'logs'/log.name)
    shutil.copy2(sdk/'tests'/mode/'runtime.log', output/'logs'/f'{mode}-runtime.log')
    for image in (sdk/'tests'/mode).glob('*.png'):
        shutil.copy2(image, output/'screenshots'/mode/image.name)
for name in ('final-build.log', 'smoke-xvfb.log', 'bootstrap-parallel.log'):
    shutil.copy2(sdk/'logs'/name, output/'logs'/name)
shutil.copy2(sdk/'sdk-manifest.json', output/'sdk-manifest.json')
summary = {'status': 'ARM build + versioned imports + QEMU xcb software UI tests passed',
           'target_libraries_unchanged': True, 'binaries': records,
           'hardware_acceptance': 'Not performed: physical board boot, linuxfb/touch, serial wiring, audio/camera/GPU',
           'deployment': 'Not performed by SDK builder; systemui is production, tests use separate binaries'}
(output/'validation.json').write_text(json.dumps(summary, indent=2)+'\n')
print(json.dumps(summary, indent=2))
