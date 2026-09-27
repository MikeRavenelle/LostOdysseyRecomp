"""Temporary checksum-gated transport; not part of the game or release."""
import base64
import gzip
import hashlib
import os
from pathlib import Path
import subprocess

baseline = subprocess.check_output(['git', 'rev-parse', 'HEAD^'], text=True).strip()
assert baseline == 'ae82bfbfe94a576efe727504053b257b8e4e9572', 'Baseline changed'
parts = sorted(Path(__file__).parent.glob('part-??.b64'))
assert len(parts) == 4, 'Incomplete patch'
encoded = ''.join(p.read_text(encoding='ascii').strip() for p in parts)
raw = gzip.decompress(base64.b64decode(encoded, validate=True))
assert hashlib.sha256(raw).hexdigest() == '499553cf07519e174c39f9918b195fbbc57b37a9b9d77eca54ac4b8f427e5d2e', 'Checksum mismatch'
path = Path(os.environ['RUNNER_TEMP']) / 'pr73-validation-object-fix.patch'
path.write_bytes(raw)
subprocess.run(['git', 'apply', '--check', '--index', str(path)], check=True)
subprocess.run(['git', 'apply', '--index', str(path)], check=True)
subprocess.run(['git', 'diff', '--cached', '--check'], check=True)
