"""Temporary, checksum-gated patch transport for the exact PR73 baseline."""
import base64
import gzip
import hashlib
import os
from pathlib import Path
import subprocess

baseline = 'b001e8f645c6f0cabefbea8a7586e6354cd80a5c'
parent = subprocess.check_output(['git', 'rev-parse', 'HEAD^'], text=True).strip()
if parent != baseline:
    raise SystemExit('PR73 baseline moved; refuse automatic application')
parts = sorted(Path(__file__).parent.glob('part-??.b64'))
if len(parts) != 5:
    raise SystemExit('Incomplete patch transport')
encoded = ''.join(p.read_text(encoding='ascii').strip() for p in parts)
raw = gzip.decompress(base64.b64decode(encoded, validate=True))
if hashlib.sha256(raw).hexdigest() != '10b1b464b35e26f3b72c6b23a41eb130aa2286a36fe973c6ab7fc23f6f555d12':
    raise SystemExit('Patch checksum mismatch')
path = Path(os.environ['RUNNER_TEMP']) / 'pr73-recovery.patch'
path.write_bytes(raw)
subprocess.run(['git', 'apply', '--check', '--index', str(path)], check=True)
subprocess.run(['git', 'apply', '--index', str(path)], check=True)
subprocess.run(['git', 'diff', '--cached', '--check'], check=True)
