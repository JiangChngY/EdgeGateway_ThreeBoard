#!/usr/bin/env python3
"""Fetch the verified Arm-owned redirect in parallel, then check the full digest."""
from concurrent.futures import ThreadPoolExecutor
import hashlib
from pathlib import Path
import shutil
import sys
import urllib.request

directory = Path(sys.argv[1]).resolve()
assert directory == (Path.home() / 'edgegateway_desktop_sdk/downloads')
name = 'gcc-arm-9.2-2019.12-x86_64-arm-none-linux-gnueabihf.tar.xz'
# Verified live 302 redirect from developer.arm.com on 2026-09-06.
url = 'https://armkeil.blob.core.windows.net/developer/Files/downloads/gnu-a/9.2-2019.12/binrel/'+name
size = 264181856
md5 = 'ae539d09dadacf7f22fcd6f54870e5ad'  # Arm official .asc
sha256 = '51bbaf22a4d3e7a393264c4ef1e45566701c516274dde19c4892c911caa85617'
destination = directory/name
parts = directory/'ranges'
parts.mkdir(exist_ok=True)

def download(index):
    lo = size*index//12
    hi = size*(index+1)//12-1
    path = parts/f'{index:02d}.part'
    if path.exists() and path.stat().st_size == hi-lo+1:
        return path
    request = urllib.request.Request(url, headers={'Range': f'bytes={lo}-{hi}'})
    with urllib.request.urlopen(request, timeout=90) as response:
        assert response.status == 206
        assert response.headers['Content-Range'] == f'bytes {lo}-{hi}/{size}'
        with path.open('wb') as output:
            shutil.copyfileobj(response, output, length=1024*1024)
    assert path.stat().st_size == hi-lo+1
    print('downloaded', index, path.stat().st_size, flush=True)
    return path

with ThreadPoolExecutor(max_workers=12) as executor:
    paths = list(executor.map(download, range(12)))
temporary = directory/(name+'.assembled')
with temporary.open('wb') as output:
    for path in paths:
        with path.open('rb') as source:
            shutil.copyfileobj(source, output, length=1024*1024)
data = temporary.read_bytes()
assert len(data) == size
assert hashlib.md5(data).hexdigest() == md5
assert hashlib.sha256(data).hexdigest() == sha256
temporary.replace(destination)
print('DOWNLOAD_VERIFIED', sha256, flush=True)
