"""Advisory inventory bound to an executable collection; never executable input."""
import json
import re
from .formats import require
from .collection_kernel import checked_json

LIMIT = 128 * 1024
FORMAT = 'openxy-collection-inventory/1'


def decode(data, *, digest=None, kernel=None):
    require(0 < len(data) <= LIMIT, 'Invalid collection inventory size')
    body = json.loads(data).get('body', {})
    require(checked_json(body) == data and set(body) ==
            {'format', 'kernel', 'digest', 'machines', 'extensions', 'layout'} and
            body['format'] == FORMAT, 'Invalid collection inventory')
    require(all(isinstance(body[k], str) and re.fullmatch('[0-9a-f]{64}', body[k])
                for k in ('kernel', 'digest')), 'Invalid inventory identity')
    require((digest is None or body['digest'] == digest) and
            (kernel is None or body['kernel'] == kernel), 'Inventory belongs to another collection')
    require(isinstance(body['machines'], list) and 0 < len(body['machines']) <= 128 and
            isinstance(body['extensions'], list) and len(body['extensions']) <= 32 and
            isinstance(body['layout'], dict) and len(body['layout']) <= 256, 'Invalid inventory entries')
    ids = set()
    for machine in body['machines']:
        require(isinstance(machine, dict) and set(machine) == {'id', 'name', 'kind', 'identity', 'source'} and
                isinstance(machine['id'], str) and re.fullmatch('[a-z][a-z0-9_]{0,23}', machine['id']) and
                machine['id'] not in ids and isinstance(machine['name'], str) and 0 < len(machine['name']) <= 128 and
                machine['kind'] in ('synth', 'sample', 'drum', 'effect', 'player') and
                isinstance(machine['source'], dict) and
                body['layout'].get(machine['id']) == machine['identity'], 'Invalid inventory machine')
        ids.add(machine['id'])
    require(all(isinstance(x, str) and re.fullmatch('[a-z][a-z0-9_]{0,31}', x)
                for x in body['extensions']), 'Invalid inventory extensions')
    for identity in body['layout'].values():
        require(isinstance(identity, dict) and len(identity) == 2 and 'page' in identity and
                len(set(identity) & {'engine', 'effect', 'player'}) == 1 and
                all(type(n) is int and 0 <= n <= 65535 for n in identity.values()), 'Invalid inventory layout')
    return body


def encode(description, layout, machines, extensions):
    body = dict(format=FORMAT, kernel=description['kernel'], digest=description['digest'],
                machines=machines, extensions=extensions, layout=layout)
    data = checked_json(body)
    decode(data)
    require([m['id'] for m in machines] == description['linked'], 'Inventory differs from linked collection')
    return data


def filename(digest):
    require(isinstance(digest, str) and re.fullmatch('[0-9a-f]{64}', digest), 'Invalid inventory digest')
    return 'manifest-' + digest + '.oxc'
