"""The catalog: unique IDs and identities, and fetching exactly the pinned commit."""
import copy
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from tooling import catalog
from tooling.machines import ROOT, discovered

PUBLISHED = dict(id='probe', kind='synth', identity=dict(engine=5, page=85), maintainer='someone',
                 status='published', repository='https://github.com/someone/opxy-probe', tag='v1.0.0',
                 commit='0' * 40, path='.')


def document(*entries):
    return {'schema': 1, 'machines': list(entries)}


class Validation(unittest.TestCase):
    def test_the_repository_catalog_is_valid(self):
        entries = catalog.load()
        self.assertTrue(all(e['status'] in catalog.STATUSES for e in entries))

    def test_entries_by_status(self):
        catalog.validate(document(PUBLISHED))
        reserved = {k: PUBLISHED[k] for k in catalog.COMMON} | {'status': 'reserved'}
        catalog.validate(document(reserved))
        catalog.validate(document(dict(PUBLISHED, status='retired')))
        for broken in (dict(reserved, tag='v1'), {k: v for k, v in PUBLISHED.items() if k != 'commit'},
                       dict(PUBLISHED, status='draft'), dict(PUBLISHED, extra=1)):
            with self.subTest(entry=broken), self.assertRaises(ValueError):
                catalog.validate(document(broken))

    def test_fields_are_checked(self):
        for change in (dict(id='Bad'), dict(id='sdk_probe'), dict(kind='player'), dict(identity=dict(engine=33, page=85)),
                       dict(maintainer='not a user'), dict(repository='https://example.com/a/b'),
                       dict(repository='git@github.com:a/b'), dict(commit='abc'), dict(tag='v 1'),
                       dict(path='../x'), dict(path='/x')):
            with self.subTest(change=change), self.assertRaises(ValueError):
                catalog.validate(document(dict(PUBLISHED, **change)))

    def test_ids_and_identities_are_held_once(self):
        other = dict(PUBLISHED, id='other', identity=dict(engine=8, page=86))
        catalog.validate(document(PUBLISHED, other))
        for clash in (dict(other, id='probe'), dict(other, identity=dict(engine=5, page=86)),
                      dict(other, identity=dict(engine=8, page=85)), dict(other, status='retired', id='probe')):
            with self.subTest(clash=clash), self.assertRaisesRegex(ValueError, 'already|held'):
                catalog.validate(document(PUBLISHED, clash))
        # Bundled machines hold theirs too.
        fm2op = next(m for m in discovered() if m.id == 'fm2op')
        for clash in (dict(PUBLISHED, id='fm2op'), dict(PUBLISHED, identity=dict(engine=fm2op.identity['engine'], page=85)),
                      dict(PUBLISHED, identity=dict(engine=5, page=fm2op.identity['page']))):
            with self.subTest(clash=clash), self.assertRaises(ValueError):
                catalog.validate(document(clash))
        # A synth's page is its own; an effect's ID is its own.
        effect = dict(PUBLISHED, id='fx', kind='effect', identity=dict(effect=2, page=21))
        catalog.validate(document(effect, dict(effect, id='fx2', identity=dict(effect=3, page=21))))


class Changes(unittest.TestCase):
    def test_entries_keep_their_identity_and_only_move_forward(self):
        reserved = {k: PUBLISHED[k] for k in catalog.COMMON} | {'status': 'reserved'}
        catalog.changes([reserved], [PUBLISHED])
        catalog.changes([PUBLISHED], [dict(PUBLISHED, tag='v1.1.0', commit='1' * 40)])
        catalog.changes([PUBLISHED], [dict(PUBLISHED, status='retired')])
        catalog.changes([], [PUBLISHED])
        for after in ([], [dict(PUBLISHED, identity=dict(engine=8, page=85))], [dict(PUBLISHED, kind='effect')],
                      [dict(reserved)], [dict(PUBLISHED, id='other')]):
            with self.subTest(after=after), self.assertRaises(ValueError):
                catalog.changes([PUBLISHED], after)
        with self.assertRaises(ValueError):
            catalog.changes([dict(PUBLISHED, status='retired')], [PUBLISHED])


class Fetching(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        root = Path(self.temporary.name).resolve()
        # A machine repository: fm2op renamed to probe, committed and tagged.
        self.repository = root / 'opxy-probe'
        shutil.copytree(ROOT / 'machines/fm2op', self.repository)
        definition = json.loads((self.repository / 'definition.json').read_text(encoding='utf-8'))
        definition['id'] = 'probe'
        (self.repository / 'definition.json').write_text(json.dumps(definition), encoding='utf-8')
        record = json.loads((self.repository / 'machine.json').read_text(encoding='utf-8'))
        record['identity'] = dict(engine=5, page=85)
        (self.repository / 'machine.json').write_text(json.dumps(record), encoding='utf-8')
        for command in (['init', '-q', '-b', 'main'], ['add', '-A'],
                        ['-c', 'user.name=t', '-c', 'user.email=t@t', 'commit', '-q', '-m', 'probe'],
                        ['tag', 'v1.0.0']):
            subprocess.run(['git', *command], cwd=self.repository, check=True, capture_output=True,
                           creationflags=catalog.NO_WINDOW)
        commit = catalog.git('rev-parse', 'HEAD', cwd=self.repository).strip()
        self.entry = dict(PUBLISHED, repository=self.repository.as_uri(), commit=commit)
        self.cache = root / 'cache'

    def test_fetch_takes_exactly_the_pinned_commit(self):
        self.assertEqual(catalog.tag_commit(self.entry['repository'], 'v1.0.0'), self.entry['commit'])
        m = catalog.fetch(self.entry, self.cache)
        self.assertEqual((m.id, m.identity), ('probe', dict(engine=5, page=85)))
        self.assertEqual(catalog.fetch(self.entry, self.cache).root, m.root)  # reused when unchanged
        (m.root / 'engine.cpp').write_text('// changed\n', encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'changed'):
            catalog.fetch(self.entry, self.cache)

    def test_fetch_refuses_a_machine_that_differs_from_its_entry(self):
        for change in (dict(identity=dict(engine=8, page=85)), dict(kind='effect', identity=dict(effect=2, page=21))):
            with self.subTest(change=change), self.assertRaisesRegex(ValueError, 'differs'):
                catalog.fetch(dict(self.entry, **change), self.cache / str(len(change)))

    def test_pinned_means_fetched_and_unchanged(self):
        m = catalog.fetch(self.entry, self.cache)
        listing = Path(self.temporary.name) / 'machines.json'
        listed = dict(self.entry, repository=PUBLISHED['repository'])  # pinned() never contacts it
        listing.write_text(json.dumps(document(listed)), encoding='utf-8')
        self.assertTrue(catalog.pinned(m, listing))
        listing.write_text(json.dumps(document(dict(listed, commit='1' * 40))), encoding='utf-8')
        self.assertFalse(catalog.pinned(m, listing))


if __name__ == '__main__':
    unittest.main()
