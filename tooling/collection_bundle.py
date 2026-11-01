"""Build fixed-kernel RAM collections. This module never contacts a device."""
from pathlib import Path
import json
import re
from . import collection_activation as activation,collection_kernel as kernel,native
from .formats import elf32,require,sha
from .pipeline import read,document,fresh_output,save_json
from .release import outside_checkouts
from .machines import read_layout


def check_package(package):
    package=Path(package).resolve()
    manifest=document(package/'manifest.json');report=document(package/'analysis/compile-report.json')
    require(manifest.get('collection_loader') is True and report.get('collection_loader') is True and
            document(package/'verification.json').get('fresh_source_rebuild_matches') is True,
            'A verified resident-kernel package is required')
    for name,digest in manifest['files'].items():
        from .pipeline import child
        require(sha(read(child(package,name)))==digest,'Kernel package hash mismatch: '+name)
    info=kernel.read_information(read(package/'candidate/projects/openxy/kernel.oxc'))
    require(info==kernel.read_information(kernel.information(report,sha(read(package/'candidate/os.ldr')))),
            'Kernel information differs from its compiled package')
    return manifest,report,info


def resident_exports(data,geometry):
    _,symbols=elf32(data);base=geometry['resident_base'];end=geometry['payload_base']
    return {name:(s['value'],s['size'],s['type']) for name,s in symbols.items()
            if s['type'] in (1,2) and s['size'] and base<=s['value']<end}


def pack(package,out,toolchain,compiler,entries=None,layout=None):
    package=Path(package).resolve();manifest,baseline,info=check_package(package)
    require(outside_checkouts(out),'Keep executable collections outside every checkout')
    # Use the installed kernel's ledger unless the caller supplies the ledger
    # from a later collection, which retains all IDs of removed machines.
    old=read_layout(package/'layout.json')
    layout=old if layout is None else layout
    require(all(layout.get(name)==identity for name,identity in old.items()),'Collection layout changed an embedded machine ID')
    entries=native.package_collection(package) if entries is None else entries
    profile,machines,definitions=native.configuration(entries,layout,loader=True)
    _,embedded,_=native.configuration(native.package_collection(package),native.package_layout(package),loader=True)
    require(native.provenance(embedded)==baseline['sources'],'Kernel SDK sources changed; use its source snapshot or rebuild the kernel')
    machines.collection_external=True
    output=fresh_output(out);analysis=output/'analysis';analysis.mkdir()
    module,report,_=native.compile_collection(analysis,toolchain,compiler,profile,machines,definitions,
        native.package_version(package),baseline['profiling'],False)
    require(report['collection_interface']==info['interface'] and report['collection_platform']==info['platform'],
            'Collection requires a different resident kernel')
    geometry=info['interface']['geometry'];resident=geometry['resident_bytes']
    require(module[:resident]==read(package/'analysis/module.bin')[:resident] and
            resident_exports(read(analysis/'module.elf'),geometry)==resident_exports(read(package/'analysis/module.elf'),geometry),
            'Collection changed resident code, state layout or exported addresses')
    from .collection_resident import check_kernel_layout
    checked=check_kernel_layout(read(analysis/'module.elf'),info['interface'])
    sections,_=elf32(read(analysis/'module.elf'));base=geometry['payload_base']
    data=activation.encode(module[resident:],base=base,capacity=geometry['payload_bytes'],
        bss_offset=sections['.bss']['address']-base,bss_bytes=sections['.bss']['size'],
        text_bytes=sections['.text']['size'],entries=len(info['interface']['entries']),
        platform=bytes.fromhex(info['platform']),abi=bytes.fromhex(info['abi']),
        resources_offset=checked['resources_offset'],resources_count=checked['resources_count'])
    activation.inspect(data,geometry=geometry,entries=len(info['interface']['entries']),
        platform=bytes.fromhex(info['platform']),abi=bytes.fromhex(info['abi']))
    (output/'bundle.oxc').write_bytes(data)
    assets=native.prepare_assets(read(native.STOCK_CONTENT/'op1_factory.json'),profile,machines)
    resources={}
    for kind,digest,content in kernel.resource_files(assets):
        name=('asset-' if kind==1 else 'catalog-')+digest.hex()+'.oxc'
        path=output/'resources'/name;path.parent.mkdir(exist_ok=True);path.write_bytes(content)
        resources[name]=sha(content)
    presets={}
    for name,preset_data in assets.items():
        if name.startswith('content/presets/'):
            target=output/'presets'/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(preset_data)
            presets[name]=sha(preset_data)
    description=dict(format='openxy-executable-collection/1',kernel=sha(read(package/'candidate/projects/openxy/kernel.oxc')),
        platform=info['platform'],abi=info['abi'],image_sha256=sha(data),digest=data[112:144].hex(),
        resources=resources,presets=presets,linked=report['linked'],interface=info['interface'],collection=entries)
    save_json(output/'collection.json',description)
    save_json(output/'layout.json',native.layout_document(native.numbers_after(layout,machines)))
    (output/'kernel.oxc').write_bytes(read(package/'candidate/projects/openxy/kernel.oxc'))
    read_bundle(output)
    return output,description


def read_bundle(folder):
    folder=Path(folder);description=document(folder/'collection.json')
    require(description.get('format')=='openxy-executable-collection/1','Wrong executable collection format')
    info=kernel.read_information(read(folder/'kernel.oxc'))
    require(sha(read(folder/'kernel.oxc'))==description['kernel'] and info['platform']==description['platform'] and
            info['abi']==description['abi'] and info['interface']==description['interface'],'Collection/kernel identity mismatch')
    data=read(folder/'bundle.oxc')
    require(sha(data)==description['image_sha256'],'Collection file hash mismatch')
    checked=activation.inspect(data,geometry=info['interface']['geometry'],entries=len(info['interface']['entries']),
        platform=bytes.fromhex(info['platform']),abi=bytes.fromhex(info['abi']))
    require(checked['digest']==description['digest'],'Collection digest mismatch')
    expected={r['path']:r['sha256'] for r in checked['resources']}
    require(description['resources']==expected,'Collection resource manifest mismatch')
    resources={name:read(folder/'resources'/name) for name in expected}
    require(all(sha(resources[name])==digest for name,digest in expected.items()),'Collection resource checksum mismatch')
    require(all(len(resources[r['path']])==r['bytes'] and b'\0' not in resources[r['path']]
                for r in checked['resources']),'Collection resource length or text differs from descriptor')
    preset_files(folder,description)
    return description,info,data,resources


def preset_files(folder,description):
    result={}
    for name,digest in description.get('presets',{}).items():
        require(re.fullmatch(r'content/presets/[a-z][a-z0-9_]{0,14}/[a-z0-9][a-z0-9_-]{0,23}\.json',name) is not None,
                'Invalid collection preset path')
        data=read(Path(folder)/'presets'/name)
        require(0<len(data)<=16384 and sha(data)==digest and isinstance(json.loads(data),dict),
                'Collection preset checksum or content mismatch')
        result[tuple(name.split('/'))]=data
    return result
