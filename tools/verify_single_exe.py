"""Reject non-system DLLs and verify every embedded resource, including tool imports."""
import argparse
import hashlib
import json
import xml.etree.ElementTree as ET
from pathlib import Path
import pefile

SYSTEM_DLLS = set('advapi32 avrt avicap32 bcrypt comctl32 crypt32 dwmapi dxgi gdi32 iphlpapi kernel32 msimg32 ncrypt ole32 oleaut32 shell32 shlwapi tdh user32 uxtheme wevtapi winmm ws2_32'.split())


def check_imports(names):
    for raw in names:
        name=raw.lower()
        if not name.endswith('.dll') or not (name[:-4] in SYSTEM_DLLS or name.startswith('api-ms-win-crt-')):
            raise ValueError('External/non-system DLL dependency: ' + raw)


def imports(pe):
    entries=list(getattr(pe,'DIRECTORY_ENTRY_IMPORT',[])) + list(getattr(pe,'DIRECTORY_ENTRY_DELAY_IMPORT',[]))
    names=[entry.dll.decode('ascii') for entry in entries]
    check_imports(names)
    return names


def verify(executable, metadata):
    data=Path(executable).read_bytes(); pe=pefile.PE(data=data)
    if pe.FILE_HEADER.Machine != 0x8664 or pe.OPTIONAL_HEADER.Subsystem != 2:
        raise ValueError('Expected x64 Windows GUI executable')
    result={'exe_sha256':hashlib.sha256(data).hexdigest(),'bytes':len(data),'imports':imports(pe),'resources':[]}
    resources={}
    app_manifest=None
    for kind in pe.DIRECTORY_ENTRY_RESOURCE.entries:
        if kind.id == 24:
            for entry in kind.directory.entries:
                if entry.id == 1:
                    record=entry.directory.entries[0].data.struct
                    app_manifest=pe.get_data(record.OffsetToData,record.Size)
        if kind.id != 10: continue
        for entry in kind.directory.entries:
            for language in entry.directory.entries:
                record=language.data.struct
                if entry.id in resources: raise ValueError('Duplicate resource ID')
                resources[entry.id]=pe.get_data(record.OffsetToData,record.Size)
    if app_manifest is None:
        raise ValueError('Missing explicit asInvoker application manifest')
    xml=ET.fromstring(app_manifest.rstrip(b'\0'))
    level=xml.find('.//{urn:schemas-microsoft-com:asm.v3}requestedExecutionLevel')
    if level is None or level.attrib != {'level':'asInvoker','uiAccess':'false'}:
        raise ValueError('Application must not request elevation or UIAccess')
    result['execution_level']='asInvoker; uiAccess=false'
    manifest=json.loads(Path(metadata).read_text())
    if set(resources) != {asset['id'] for asset in manifest['assets']}:
        raise ValueError('Missing or unexpected embedded resource')
    for asset in manifest['assets']:
        payload=resources[asset['id']]
        if len(payload)!=asset['bytes'] or hashlib.sha256(payload).hexdigest()!=asset['sha256']:
            raise ValueError('Embedded payload mismatch: '+asset['name'])
        row=dict(asset)
        if asset['name'].endswith('.exe'):
            child=pefile.PE(data=payload)
            if child.FILE_HEADER.Machine!=0x8664: raise ValueError('Tool is not x64')
            row['imports']=imports(child)
        result['resources'].append(row)
    pe.close()
    return result

if __name__=='__main__':
    p=argparse.ArgumentParser(); p.add_argument('exe',type=Path); p.add_argument('metadata',type=Path)
    a=p.parse_args(); print(json.dumps(verify(a.exe,a.metadata),indent=2))
