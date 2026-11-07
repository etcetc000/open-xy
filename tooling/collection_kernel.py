"""Resident coordinator generation; hardware activation is separately qualified."""
from pathlib import Path
import copy
import json
from .formats import require
from .pipeline import ROOT,read,check_patch_boundaries
from .formats import sha

TEMPLATE='sdk/platform/opxy-1.2.1/collection_kernel.cpp.in'
RESIDENT_BYTES=0x9000
OVERLAY='profiles/collection-loader-1.2.1.json'
OVERLAY_SHA256='094deff2ac094a0b32cded835837e0ac8a079f6f3e81a525247f51b3676c11f4'


def profile(base):
    from . import collection_probe,collection_rpc
    raw=read(ROOT/OVERLAY)
    require(sha(raw)==OVERLAY_SHA256, 'Unreviewed resident resource hooks')
    overlay=json.loads(raw)
    require(overlay['schema']==1 and overlay['base']==base['id'], 'Wrong resident resource profile')
    result=collection_rpc.profile(collection_probe.profile(base))
    result=copy.deepcopy(result)
    result['patches'].extend(overlay['hooks'])
    check_patch_boundaries(result,len(base['patches'])+5)
    return result


def render(interface, platform, abi):
    require(len(platform)==len(abi)==32, 'Invalid resident identity')
    geometry=interface['geometry']
    require(geometry['payload_base']+geometry['payload_bytes']==geometry['reserved_end'] and
            geometry['resident_base']+geometry['resident_bytes']==geometry['payload_base'],
            'Noncontiguous resident geometry')
    def array(data):return '{'+','.join(str(b) for b in data)+'}'
    values=dict(LIMITS=','.join((hex(geometry['payload_base']),hex(geometry['payload_bytes']),
                                str(len(interface['entries'])),array(platform),array(abi))),
                HEAP=','.join(hex(n) for n in (geometry['resident_base'],0x200000,
                                             0x8c0d0380,geometry['resident_base']-0x8c0d0380,0x800000)),
                RESIDENT_BASE=hex(geometry['resident_base']),RESIDENT_BYTES=hex(geometry['resident_bytes']))
    # The supported profile's SDK heap and reservation are a fixed ABI, never
    # a caller-controlled address range admitted merely by alignment.
    require(interface['target']=='opxy-1.2.1' and geometry['resident_base']==0x8d2d0380 and
            geometry['reserved_end']==0x8d4cc380, 'Unsupported native collection reservation')
    text=(ROOT/TEMPLATE).read_text(encoding='utf-8')
    for name,value in values.items():
        require('@'+name+'@' in text, 'Missing coordinator placeholder')
        text=text.replace('@'+name+'@',value)
    require('@' not in text, 'Unexpanded resident coordinator')
    return text


def generate(directory, interface, platform, abi):
    path=Path(directory)/'collection_kernel.cpp'
    path.write_text(render(interface,platform,abi),encoding='utf-8',newline='\n')
    return path


def resource_files(assets):
    """Immutable files and descriptors, deduplicated by kind and full digest."""
    import hashlib
    from . import collection_activation as activation
    resources={}
    for name,data in assets.items():
        # Presets are ordinary content files, installed separately from the RAM resource cache.
        if name.startswith('content/presets/') and name.endswith('.json'):
            continue
        kind=2 if name=='content/op1_factory.json' else 1
        require(name=='content/op1_factory.json' or name.startswith('content/display/') and name.endswith('.svg'),
                'Unexpected collection resource')
        require(data and len(data)<=(activation.MAX_CATALOG_BYTES if kind==2 else activation.MAX_SVG_BYTES)
                and b'\0' not in data, 'Invalid collection text resource')
        digest=hashlib.sha256(data).digest()
        path=('catalog-' if kind==2 else 'asset-')+digest.hex()+'.oxc'
        resources[path]=(kind,digest,data)
    ordered=[resources[path] for path in sorted(resources)]
    require(1<=len(ordered)<=activation.MAX_RESOURCES and sum(k==2 for k,_,_ in ordered)==1 and
            sum(len(data) for _,_,data in ordered)<=activation.MAX_RESOURCE_TOTAL, 'Collection resource budget exceeded')
    return ordered


def descriptors(directory, assets):
    import struct
    data=b''.join(digest+struct.pack('<2I',len(content),kind) for kind,digest,content in resource_files(assets))
    text=('extern "C" {\nextern const unsigned char sdk_collection_resource_descriptors[] '
          '__asm__("sdk_collection_resource_descriptors") = {'+','.join(str(b) for b in data)+'};\n}\n')
    path=Path(directory)/'collection_descriptors.cpp'
    path.write_text(text,encoding='utf-8',newline='\n')
    return path


def information(report,os_digest):
    from .collection_activation import fingerprint
    body=dict(format='openxy-collection-kernel/1',platform=report['collection_platform'],
              abi=fingerprint(report['collection_interface']).hex(),
              interface=report['collection_interface'],os_sha256=os_digest,embedded=report['linked'])
    return checked_json(body)


def fallback(profile):
    data=read(ROOT/'sdk/platform/opxy-1.2.1/screen.svg')
    require(sha(data)==profile['screen_template_sha256'],'Unreviewed protected fallback artwork')
    return data


def checked_json(body):
    import hashlib
    raw=json.dumps(body,sort_keys=True,separators=(',',':')).encode()
    return json.dumps(dict(body=body,sha256=hashlib.sha256(raw).hexdigest()),sort_keys=True,separators=(',',':')).encode()+b'\n'


def read_information(data):
    from . import collection_activation as activation
    require(0<len(data)<=65536, 'Invalid resident kernel information size')
    document=json.loads(data);body=document.get('body',{})
    require(checked_json(body)==data and body.get('format')=='openxy-collection-kernel/1', 'Corrupt resident kernel information')
    require(set(body)=={'format','platform','abi','interface','os_sha256','embedded'} and
            len(bytes.fromhex(body['platform']))==32 and len(bytes.fromhex(body['os_sha256']))==32 and
            activation.fingerprint(body['interface']).hex()==body['abi'], 'Invalid resident kernel identity')
    expected=activation.geometry(dict(module_base=0x8d2d0380,reserved_bytes=0x200000,table_bytes=0x4000),RESIDENT_BYTES)
    require(body['interface']['geometry']==expected and body['interface']['target']=='opxy-1.2.1' and
            0<len(body['interface']['entries'])<=activation.MAX_ENTRIES, 'Unsupported resident kernel layout')
    return body
