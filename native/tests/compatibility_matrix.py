"""Static ABI/trampoline and rejection tests; never launch fixture executables.

python compatibility_matrix.py --build-dir BUILD/Release --fixtures FIXTURES
    --cache SYMBOL_CACHE --report REPORT.json

Fixtures are local directories containing Taskmgr.exe and metadata.json. Neither
Microsoft binaries nor PDB files are redistributed by this repository.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess

p = argparse.ArgumentParser(__doc__)
p.add_argument('--build-dir', type=Path, required=True)
p.add_argument('--fixtures', type=Path, required=True)
p.add_argument('--cache', type=Path, required=True)
p.add_argument('--report', type=Path, required=True)
a = p.parse_args()
tool = a.build_dir.resolve() / 'MCNativeCompatibilityVerify.exe'
cache = a.cache.resolve()
current = Path(os.environ['SystemRoot']) / 'System32/Taskmgr.exe'

def verify(image, pdb='-', cache_path=cache, flags=('--offline',)):
    r = subprocess.run([str(tool), str(image), str(pdb), str(cache_path), *flags],
                       text=True, capture_output=True, timeout=180)
    report = json.loads(r.stdout)
    report['exit_code'] = r.returncode
    return report

rows = []
for folder in sorted(a.fixtures.resolve().iterdir()):
    if not (folder / 'metadata.json').exists():
        continue
    metadata = json.loads((folder / 'metadata.json').read_text())
    image = folder / 'Taskmgr.exe'
    assert hashlib.sha256(image.read_bytes()).hexdigest() == metadata['sha256']
    result = verify(image)
    result.update(fixture_version=metadata['version'], source=metadata['exe_url'])
    rows.append(result)
rows.append(verify(current))

# These files stay in the explicitly chosen fixture workspace, never the release.
negative_root = a.fixtures.resolve() / '.negative'
negative_root.mkdir(exist_ok=True)
content = current.read_bytes()
pe = struct.unpack_from('<I', content, 60)[0]
negatives = []
def reject(name, image, expected, **kwargs):
    result = verify(image, **kwargs)
    assert result['exit_code'] != 0 and result['injected'] is False
    assert result['error'].startswith(expected), (name, result)
    negatives.append(dict(case=name, error=result['error'], injected=False, passed=True))

bad = negative_root / 'invalid.exe'
bad.write_bytes(b'not a PE file')
reject('invalid_pe', bad, 'invalid_image')
arm = bytearray(content)
struct.pack_into('<H', arm, pe + 4, 0xAA64)
bad = negative_root / 'arm64.exe'
bad.write_bytes(arm)
reject('arm64_image', bad, 'architecture')
old = next(a.fixtures.resolve().glob('10.0.19041.*/metadata.json'))
old_key = json.loads(old.read_text())['pdb_key']
reject('wrong_pdb_guid', current, 'pdb_identity', pdb=cache / old_key / 'Taskmgr.pdb')
key = rows[-1]['pdb_key']
truncated = negative_root / 'truncated'
truncated.mkdir(exist_ok=True)
bad_pdb = truncated / 'Taskmgr.pdb'
bad_pdb.write_bytes((cache / key / 'Taskmgr.pdb').read_bytes()[:4096])
reject('truncated_pdb', current, 'pdb_identity', pdb=bad_pdb)
reject('offline_without_cache', current, 'offline_cache_miss', cache_path=negative_root / 'empty-cache')
reject('cancel_before_matching', current, 'cancelled', flags=('--offline', '--cancel'))

# GUID/age remains valid but the real local object allocation ABI changes.
# A matching PDB alone must not authorize this binary for injection.
changed = bytearray(content)
rva = rows[-1]['functions']['LoadHeatMap']['rva']
sections = struct.unpack_from('<H', changed, pe + 6)[0]
optional = struct.unpack_from('<H', changed, pe + 20)[0]
for i in range(sections):
    _, va, size, raw = struct.unpack_from('<IIII', changed, pe + 24 + optional + i * 40 + 8)
    if va <= rva < va + size:
        offset = raw + rva - va
        break
else:
    raise AssertionError('No file section for heatmap loader')
position = changed.find(b'\xb9\x30\x00\x00\x00', offset, offset + 128)
assert position >= 0
changed[position + 1] = 0x38
bad = negative_root / 'unsupported-layout.exe'
bad.write_bytes(changed)
reject('changed_heatmap_size', bad, 'unsupported_layout:heatmap_allocation_size')

embedded = verify(current, flags=('--offline', '--embedded'))
assert embedded['method'] == 'embedded_verified_profile'
passed = all(r.get('compatible') and r.get('all_trampolines') and r['exit_code'] == 0 for r in rows)
report = dict(passed=passed, test_kind='static_pe_pdb_abi_trampoline_only',
              cross_os_runtime_tested=False, fixtures=rows, rejection_tests=negatives,
              embedded_profile=embedded)
a.report.resolve().parent.mkdir(parents=True, exist_ok=True)
a.report.write_text(json.dumps(report, indent=2), encoding='utf-8')
print(json.dumps(dict(passed=passed, fixture_count=len(rows), rejection_count=len(negatives))))
raise SystemExit(0 if passed else 1)
