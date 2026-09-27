"""Temporary checksum-gated transport for the reviewed development patch."""
import base64
import gzip
import hashlib
import os
from pathlib import Path
import subprocess

parts = sorted(Path(__file__).parent.glob('part-??.b64'))
assert len(parts) == 5, 'Incomplete patch transport'
encoded = ''.join(p.read_text(encoding='ascii').strip() for p in parts)
raw = gzip.decompress(base64.b64decode(encoded, validate=True))
assert hashlib.sha256(raw).hexdigest() == 'f3746a2ae859895c079e739d29abaaa5b04db5eca60e2f4d2d5294243c51c555', 'Patch checksum mismatch'
path = Path(os.environ['RUNNER_TEMP']) / 'fg-p0-p4.patch'
path.write_bytes(raw)
subprocess.run(['git', 'apply', '--check', '--index', str(path)], check=True)
subprocess.run(['git', 'apply', '--index', str(path)], check=True)
subprocess.run(['git', 'diff', '--cached', '--check'], check=True)
