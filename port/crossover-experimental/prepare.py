#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Verify and prepare the exact CrossOver/FineWine baseline (Python 3.12+)."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tarfile
import urllib.request

HERE = Path(__file__).resolve().parent


def sha256(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def verify(path, expected):
    actual = sha256(path)
    if actual != expected:
        raise ValueError(f'SHA256 mismatch: {path}: expected {expected}, got {actual}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--archive', type=Path, help='Existing complete CrossOver source tarball')
    parser.add_argument('--verify-only', action='store_true')
    args = parser.parse_args()
    lock = json.loads((HERE / 'sources.lock.json').read_text())
    for patch in lock['finewine']['patches']:
        verify(HERE / patch['path'], patch['sha256'])
    print(f"Verified {len(lock['finewine']['patches'])} original FineWine patches", flush=True)
    if args.verify_only:
        return
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    source = work / 'wine-src'
    if source.exists() or (work / 'sources').exists():
        raise FileExistsError('Preparation needs a fresh work directory; existing sources are never overwritten')
    archive = args.archive.resolve() if args.archive else work / 'crossover-sources-26.3.0.tar.gz'
    if not archive.exists():
        partial = archive.with_suffix('.partial')
        urllib.request.urlretrieve(lock['crossover']['url'], partial)
        verify(partial, lock['crossover']['sha256'])
        partial.rename(archive)
    verify(archive, lock['crossover']['sha256'])
    with tarfile.open(archive) as tar:
        members = [m for m in tar.getmembers() if m.name.startswith('sources/wine/')]
        if not members:
            raise ValueError('CrossOver archive does not contain sources/wine')
        tar.extractall(work, members=members, filter='data')
    (work / 'sources/wine').rename(source)
    (work / 'sources').rmdir()
    if (source / 'VERSION').read_text().strip() != 'Wine version 11.0':
        raise ValueError('Unexpected Wine version')
    # An uncommitted index prevents git apply from finding the surrounding checkout.
    # No synthetic commits or unsigned commits are made in the source tree.
    subprocess.run(['git', 'init', '-q', str(source)], check=True)
    for patch in lock['finewine']['patches']:
        path = str(HERE / patch['path'])
        for options in (['--check'], []):
            subprocess.run(['git', '-C', str(source), '-c', 'core.autocrlf=false',
                            'apply', *options, path], check=True)
        print(f"Applied {patch['path']}", flush=True)
    (work / 'prepared.json').write_text(json.dumps(lock, indent=2) + '\n')
    print(f'Prepared {source}', flush=True)


if __name__ == '__main__':
    main()
