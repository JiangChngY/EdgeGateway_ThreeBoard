#!/usr/bin/env python3
"""Verify ELF architecture and versioned imports against the frozen runtime copy."""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

root = Path(sys.argv[2] if len(sys.argv) > 2 else os.environ.get(
    'EDGE_TARGET_ROOT', str(Path.home() / 'edgegateway_desktop_sdk/sysroot'))).resolve(strict=True)
binary = Path(sys.argv[1]).resolve()
readelf = os.environ.get('READELF') or shutil.which('readelf')
if not readelf:
    raise SystemExit('readelf not found; install binutils or set READELF')
def require(condition, message):
    if not condition:
        raise RuntimeError(message)
def read(option, path):
    return subprocess.check_output([str(readelf), option, str(path)], text=True,
                                   env=dict(os.environ, LC_ALL='C'))
def within(path, directory):
    try:
        path.relative_to(directory)
        return True
    except ValueError:
        return False
def in_root(path):
    """Resolve target absolute symlinks relative to target root, never the host."""
    pending = list(path.relative_to(root).parts)
    parts = []
    links = 0
    while pending:
        part = pending.pop(0)
        if part in ('', '.'):
            continue
        if part == '..':
            if not parts:
                raise RuntimeError('Dependency path escapes target root: '+str(path))
            parts.pop()
            continue
        candidate = root.joinpath(*parts, part)
        if candidate.is_symlink():
            links += 1
            if links > 64:
                raise RuntimeError('Dependency symlink loop: '+str(path))
            target = os.readlink(candidate)
            if target.startswith('/'):
                parts = []
            pending = list(Path(target.lstrip('/')).parts)+pending
        else:
            parts.append(part)
    result = root.joinpath(*parts)
    require(within(result, root), 'Resolved dependency escapes target root')
    return result

def resolve(name, requester):
    paths = []
    dynamic = read('-Wd', requester)
    for search in re.findall(r'\((?:RUNPATH|RPATH)\).*?\[(.*?)\]', dynamic):
        for directory in search.split(':'):
            if not directory:
                continue
            if '$ORIGIN' in directory or '${ORIGIN}' in directory:
                if not within(requester, root):
                    continue  # Never search the host artifact directory.
                directory = directory.replace('${ORIGIN}', str(requester.parent)).replace('$ORIGIN', str(requester.parent))
                path = Path(directory)
            elif directory.startswith('/'):
                path = root/directory.lstrip('/')
            else:
                continue  # Runtime working-directory lookup is not a frozen dependency.
            paths.append(path)
    paths += [root/'usr/lib', root/'lib', root/'vendor/lib']
    for directory in paths:
        try:
            path = in_root(directory/name)
        except ValueError as error:
            raise RuntimeError('Dependency path escapes target root: '+str(directory)) from error
        if path.is_file():
            return path
    raise RuntimeError(f'Missing target runtime dependency: {name} (needed by {requester.name})')
header = read('-Wh', binary)
require('ELF32' in header and re.search(r'Machine:\s+ARM\b', header), 'Expected an ELF32 ARM executable')
require('hard-float ABI' in header, 'Expected ARM hard-float ABI')
require('/lib/ld-linux-armhf.so.3' in read('-Wl', binary), 'Expected ARM hard-float Linux interpreter')
visited = set()
report = {'binary': str(binary), 'target_root': str(root), 'readelf': str(readelf),
          'elf32_arm_hardfloat': True, 'libraries': {}, 'requirements': {}}
def check(path):
    path = path.resolve()
    if path in visited:
        return
    visited.add(path)
    versions = read('-WV', path)
    dependency = None
    requirement_report = {}
    for line in versions.split('Version needs section')[-1].splitlines() if 'Version needs section' in versions else []:
        match = re.search(r'File:\s+(\S+)', line)
        if match:
            dependency = match[1]
            requirement_report[dependency] = []
        match = re.search(r'Name:\s+(\S+)', line)
        if match and dependency:
            version = match[1]
            provided = read('-WV', resolve(dependency, path)).split('Version needs section')[0]
            definitions = set(re.findall(r'Name:\s+(\S+)', provided))
            require(version in definitions, f'{path.name}: {dependency} lacks {version}')
            requirement_report[dependency].append(version)
    # Check actual versioned strong undefined symbols, not just max version tags.
    symbols = read('-Ws', path)
    imports = [name.replace('@@', '@') for name in
               re.findall(r'\bGLOBAL\s+DEFAULT\s+UND\s+(\S+@\S+)', symbols)]
    provided_symbols = set()
    dependencies = re.findall(r'\(NEEDED\).*?\[(.*?)\]', read('-Wd', path))
    for name in dependencies:
        library = resolve(name, path)
        report['libraries'][name] = str(library)
        for line in read('-Ws', library).splitlines():
            if ' UND ' not in line:
                fields = line.split()
                if len(fields) >= 8:
                    provided_symbols.add(fields[7].replace('@@', '@'))
    missing = [name for name in imports if name not in provided_symbols]
    require(not missing, f'{path.name}: unresolved versioned symbols: {missing}')
    report['requirements'][path.name] = requirement_report
    for name in dependencies:
        check(resolve(name, path))
check(binary)
print(json.dumps(report, indent=2))
print('ABI_VERSIONED_IMPORTS_PASS')
