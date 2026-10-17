"""Build-time only: verified downloads and PE RCDATA. No runtime networking."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import tempfile
import urllib.request
import zipfile


def sha256(path):
    with Path(path).open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def parse_manifest(data):
    result = {}
    for line in data.decode('utf-8').splitlines():
        if not line.strip() or line.lstrip().startswith('#'):
            continue
        match = re.fullmatch(r'\s*([0-9a-fA-F]{64})\s+(\S+)\s*', line)
        if not match:
            raise ValueError('Malformed SHA-256 manifest')
        digest, name = match.groups(); name = name.lower(); digest = digest.lower()
        if name in result and result[name] != digest:
            raise ValueError('Conflicting manifest hashes')
        result[name] = digest
    if not {'ffmpeg.exe', 'presentmon.exe'} <= result.keys():
        raise ValueError('Manifest must contain both runtime tools')
    return result


def atomic_write_bytes(target, data):
    """Publish complete build outputs without following an existing output symlink."""
    target = Path(target)
    target.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode='wb', dir=target.parent,
                                         prefix=target.name + '.', suffix='.tmp',
                                         delete=False) as out:
            temporary = Path(out.name)
            out.write(data)
        os.replace(temporary, target)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def atomic_write_text(target, text):
    atomic_write_bytes(target, text.encode('utf-8'))


def fetch_verified(url, target, expected):
    target = Path(target)
    if target.is_symlink():
        raise ValueError('Symlinked runtime cache is not permitted: ' + str(target))
    if target.is_file() and sha256(target) == expected:
        return target
    target.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode='wb', dir=target.parent,
                                         prefix=target.name + '.', suffix='.tmp',
                                         delete=False) as out:
            temporary = Path(out.name)
            with urllib.request.urlopen(url, timeout=120) as response:
                while block := response.read(1024 * 1024):
                    out.write(block)
        if sha256(temporary) != expected:
            raise ValueError('Download SHA-256 mismatch: ' + url)
        os.replace(temporary, target)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    return target


def extract_verified_ffmpeg(archive, target, expected):
    """Stream and verify the extracted binary before publishing it atomically."""
    target = Path(target)
    target.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(archive) as z:
        names = [n for n in z.namelist() if n.lower().endswith('/bin/ffmpeg.exe')]
        if len(names) != 1:
            raise ValueError('Expected exactly one ffmpeg.exe')
        temporary = None
        try:
            digest = hashlib.sha256()
            with tempfile.NamedTemporaryFile(mode='wb', dir=target.parent,
                                             prefix=target.name + '.', suffix='.tmp',
                                             delete=False) as out:
                temporary = Path(out.name)
                with z.open(names[0]) as source:
                    while block := source.read(1024 * 1024):
                        digest.update(block)
                        out.write(block)
            if digest.hexdigest() != expected:
                raise ValueError('FFmpeg executable SHA-256 mismatch')
            os.replace(temporary, target)
        finally:
            if temporary is not None:
                temporary.unlink(missing_ok=True)


def make_notices(repo):
    repo = Path(repo)
    notices = (repo / 'licenses' / 'NOTICE.txt').read_bytes()
    for name in ('PresentMon-MIT.txt', 'FFmpeg-GPLv3.txt', 'FFmpeg-build-README.txt', 'LLVM-MinGW.txt', 'MinGW-w64-runtime.txt', 'Winpthreads.txt'):
        notices += b'\n\n===== ' + name.encode() + b' =====\n' + (repo / 'licenses' / name).read_bytes()
    return notices


def prepare(repo, out):
    repo = Path(repo); out = Path(out); out.mkdir(parents=True, exist_ok=True)
    manifest = (repo / 'runtime-tools.sha256').read_bytes()
    expected = parse_manifest(manifest)
    sources = json.loads((repo / 'runtime-tools.sources.json').read_text())
    if sources['schemaVersion'] != 1:
        raise ValueError('Unsupported runtime source metadata')
    for key in ('presentMon', 'ffmpeg'):
        source = sources[key]
        if not source['url'].startswith('https://'):
            raise ValueError('Runtime download must use HTTPS')
        if source['outputName'].lower() not in expected:
            raise ValueError('Output absent from trust manifest')
    pm = fetch_verified(sources['presentMon']['url'], out / 'PresentMon.download.exe', sources['presentMon']['downloadSha256'])
    if sha256(pm) != expected['presentmon.exe']:
        raise ValueError('PresentMon executable SHA-256 mismatch')
    ff = fetch_verified(sources['ffmpeg']['url'], out / 'ffmpeg.download.zip', sources['ffmpeg']['downloadSha256'])
    extract_verified_ffmpeg(ff, out / 'ffmpeg.exe', expected['ffmpeg.exe'])
    atomic_write_bytes(out / 'PresentMon.exe', pm.read_bytes())
    atomic_write_bytes(out / 'runtime-tools.sha256', manifest)
    atomic_write_bytes(out / 'help.txt', (repo / 'СПРАВКА.txt').read_bytes())
    notices = make_notices(repo)
    atomic_write_bytes(out / 'licenses.txt', notices)
    assets = ['PresentMon.exe', 'ffmpeg.exe', 'runtime-tools.sha256', 'help.txt', 'licenses.txt']
    records = [(101+i, name, sha256(out/name)) for i, name in enumerate(assets)]
    version = hashlib.sha256(json.dumps(records).encode()).hexdigest()
    atomic_write_text(out/'payload.rc', '#include <windows.h>\n1 RT_MANIFEST "' + (repo/'src'/'SingleExe.manifest').as_posix() + '"\n' + '\n'.join(f'{i} RCDATA "{(out/name).as_posix()}"' for i, name, _ in records) + '\n')
    atomic_write_text(out/'EmbeddedPayload.h', '#pragma once\n#include <array>\nstruct EmbeddedAsset { int id; const wchar_t* name; const char* sha; };\n' +
        'inline constexpr const wchar_t* kEmbeddedVersion = L"'+version+'";\n' +
        'inline constexpr std::array<EmbeddedAsset, 5> kEmbeddedAssets = {{\n' +
        ',\n'.join(f'{{{i}, L"{name}", "{digest}"}}' for i,name,digest in records) + '\n}};\n')
    atomic_write_text(out/'payload.json', json.dumps({'version':version, 'assets': [{'id':i,'name':n,'sha256':h,'bytes':(out/n).stat().st_size} for i,n,h in records]}, indent=2))

if __name__ == '__main__':
    parser=argparse.ArgumentParser(); parser.add_argument('--repo',type=Path,required=True); parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args(); prepare(args.repo.resolve(), args.output.resolve())
