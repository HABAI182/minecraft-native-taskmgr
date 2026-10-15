"""Regenerate the decoder-only Zydis amalgamation from a local upstream clone.
Usage: python amalgamate_zydis.py PATH_TO_ZYDIS_CLONE
The pinned commits are documented in third_party/zydis/README.md.
"""
from pathlib import Path
import importlib.util, contextlib, io, re, sys, hashlib
root=Path(sys.argv[1]).resolve()
spec=importlib.util.spec_from_file_location('zydis_amalgamate',root/'assets/amalgamate.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
find=module.find_files
allowed={'Decoder.c','DecoderData.c','MetaInfo.c','Mnemonic.c','Register.c','SharedData.c','Utils.c','Zydis.c'}
module.find_files=lambda pattern,directory:[p for p in find(pattern,directory) if p.name in allowed]
covered=set()
with contextlib.redirect_stdout(io.StringIO()):
 header='\n'.join(module.FILE_HEADER+module.merge_headers(header='Zydis/Zydis.h',search_paths=module.PUBLIC_INCLUDE_PATHS,covered_headers=covered,stack=[]))
 source='\n'.join(module.FILE_HEADER+module.merge_sources(source_dir=root/'src',covered_headers=covered))
source=re.sub(r'(?m)^// Source file: .*[/\\]([^/\\\r\n]+)$',r'// Source file: src/\1',source)
destination=Path(__file__).resolve().parents[1]/'third_party/zydis'
for name,data in [('Zydis.h',header),('Zydis.c',source)]:
 content=data.encode('utf-8');(destination/name).write_bytes(content)
 print(name,len(content),hashlib.sha256(content).hexdigest())
