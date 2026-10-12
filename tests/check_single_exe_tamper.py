"""Integration checks against the real built PE; never modifies that artifact."""
import importlib.util
import sys
import tempfile
from pathlib import Path
import pefile

spec=importlib.util.spec_from_file_location('verify', Path(__file__).parents[1]/'tools'/'verify_single_exe.py')
verify=importlib.util.module_from_spec(spec); spec.loader.exec_module(verify)
exe,metadata=map(Path,sys.argv[1:]); original=exe.read_bytes(); pe=pefile.PE(data=original)
verify.verify(exe,metadata)
mutations=[]
for kind in pe.DIRECTORY_ENTRY_RESOURCE.entries:
    if kind.id==10:
        record=kind.directory.entries[0].directory.entries[0].data.struct
        damaged=bytearray(original); damaged[pe.get_offset_from_rva(record.OffsetToData)] ^= 1
        mutations.append(('corrupt embedded resource',damaged))
    if kind.id==24:
        record=kind.directory.entries[0].directory.entries[0].data.struct
        offset=pe.get_offset_from_rva(record.OffsetToData)
        damaged=bytearray(original)
        start=original.index(b'asInvoker',offset,offset+record.Size)
        damaged[start:start+9]=b'asInvokeX'
        mutations.append(('modified elevation manifest',damaged))
entry=pe.DIRECTORY_ENTRY_IMPORT[0]
offset=pe.get_offset_from_rva(entry.struct.Name)
damaged=bytearray(original); damaged[offset:offset+len(entry.dll)]=b'x'*len(entry.dll)
mutations.append(('non-system DLL import',damaged))
assert len(mutations)==3, 'Missing test mutation target'
with tempfile.TemporaryDirectory() as temp:
    for label,data in mutations:
        target=Path(temp)/'tampered.exe'; target.write_bytes(data)
        try: verify.verify(target,metadata)
        except ValueError as error: print(label+': correctly rejected ('+str(error)+')')
        else: raise AssertionError('Accepted '+label)
print('3 real-PE tamper tests passed')
