"""The catalog: unique IDs, and fetching exactly the pinned commit."""
import contextlib
import copy
import io
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

from tooling import catalog, cpu_measurement
from tooling.machines import FIXED_NUMBERS, ROOT
from machine_fixtures import FOLDER

PUBLISHED = dict(id='probe', kind='synth', maintainer='someone',
                 status='published', repository='https://github.com/someone/opxy-probe', tag='v1.0.0',
                 commit='0' * 40, path='.', keywords=['fm'])
# An entry from before keywords, as most catalog entries were.
UNLISTED = {k: v for k, v in PUBLISHED.items() if k != 'keywords'}


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
        catalog.validate(document(dict(PUBLISHED, status='draft')))
        for broken in (dict(reserved, tag='v1'), dict(reserved, status='retired', repository=PUBLISHED['repository']),
                       dict(reserved, status='retired', commit='xyz'), {k: v for k, v in PUBLISHED.items() if k != 'commit'},
                       dict(reserved, status='draft'), dict(PUBLISHED, status='beta'), dict(PUBLISHED, extra=1)):
            with self.subTest(entry=broken), self.assertRaises(ValueError):
                catalog.validate(document(broken))

    def test_fields_are_checked(self):
        for change in (dict(id='Bad'), dict(id='sdk_probe'), dict(kind='unknown'), dict(identity=dict(engine=36, page=85)),
                       dict(maintainer='not a user'), dict(repository='https://example.com/a/b'),
                       dict(repository='https://github.com/someone/..'),
                       dict(repository='git@github.com:a/b'), dict(commit='abc'), dict(tag='v 1'),
                       dict(path='../x'), dict(path='/x')):
            with self.subTest(change=change), self.assertRaises(ValueError):
                catalog.validate(document(dict(PUBLISHED, **change)))

    def test_keywords_are_words_from_the_list(self):
        reserved = {k: PUBLISHED[k] for k in catalog.COMMON} | {'status': 'reserved'}
        for entry in (dict(PUBLISHED, keywords=['fm', 'bell']), dict(reserved, keywords=['pad']),
                      dict(PUBLISHED, keywords=list(catalog.KEYWORDS[:catalog.MAX_KEYWORDS]))):
            catalog.validate(document(entry))
        for keywords in ([], ['fm', 'fm'], ['FM'], ['unknown'], 'fm', [1], [['fm']], [{}], [None], list(catalog.KEYWORDS[:catalog.MAX_KEYWORDS + 1])):
            with self.subTest(keywords=keywords), self.assertRaisesRegex(ValueError, 'keywords'):
                catalog.validate(document(dict(PUBLISHED, keywords=keywords)))
        extension = dict(PUBLISHED, id='probe_extension', kind='extension', keywords=['fm'])
        with self.assertRaisesRegex(ValueError, 'only machines'):
            catalog.validate({'schema': 1, 'extensions': [extension]}, 'extensions')

    def test_player_catalog_has_no_audio_render_cpu_record(self):
        player = dict(PUBLISHED, kind='player')
        catalog.validate(document(player))
        catalog.changes([], [player])
        with self.assertRaisesRegex(ValueError, 'audio cpu'):
            catalog.validate(document(dict(player, cpu={})))

    def test_ids_are_held_once(self):
        other = dict(PUBLISHED, id='other')
        catalog.validate(document(PUBLISHED, other))
        for clash in (dict(other, id='probe'), dict(other, status='retired', id='probe')):
            with self.subTest(clash=clash), self.assertRaisesRegex(ValueError, 'already'):
                catalog.validate(document(PUBLISHED, clash))
        # Open-XY's own machines are catalog entries like any other, so their IDs are held there.
        entries = catalog.load()
        self.assertLessEqual(set(FIXED_NUMBERS), {e['id'] for e in entries})
        with self.assertRaisesRegex(ValueError, 'already'):
            catalog.validate(document(*entries, dict(PUBLISHED, id='fm2op')))


class Official(unittest.TestCase):
    def test_only_listed_ids_are_open_xys_own_whoever_hosts_them(self):
        karplus = next(e for e in catalog.load() if e['id'] == 'karplus')
        self.assertTrue(catalog.official(karplus))
        # The same account hosting a machine someone else wrote does not make it official.
        self.assertFalse(catalog.official(dict(karplus, id='helper')))
        self.assertFalse(catalog.official(PUBLISHED))


class BuilderPatterns(unittest.TestCase):
    def test_the_builder_checks_kits_with_the_catalogs_patterns(self):
        source = (ROOT / 'builder/core.mjs').read_text(encoding='utf-8')
        for key, pattern in (('repository', catalog.REPOSITORY), ('tag', catalog.TAG), ('commit', catalog.COMMIT),
                             ('maintainer', catalog.MAINTAINER)):
            found = re.search(key + r": /\^(.*?)\$/[,}]", source)
            self.assertIsNotNone(found, key)
            self.assertEqual(found.group(1).replace('\\/', '/'), pattern, key)
        keywords = re.search(r'export const MACHINE_KEYWORDS = \[(.*?)\];', source, re.S)
        self.assertIsNotNone(keywords)
        self.assertEqual(tuple(re.findall(r"'([a-z]+)'", keywords.group(1))), catalog.KEYWORDS)
        self.assertIn(f'const MAX_KEYWORDS = {catalog.MAX_KEYWORDS};', source)
        # The CPU-high label's threshold, in one constant on each side.
        self.assertIn(f'export const CPU_HIGH_PERCENT = {cpu_measurement.HIGH_PERCENT};', source)
        for guide in ('docs/catalog.md', 'docs/builder.md'):
            self.assertIn(f'above {cpu_measurement.HIGH_PERCENT}% of an audio block', ' '.join(
                (ROOT / guide).read_text(encoding='utf-8').split()), guide)
        # docs/catalog.md explains each word, in the same order.
        docs = (ROOT / 'docs/catalog.md').read_text(encoding='utf-8')
        table = re.search(r'^\| Keyword \| A machine that \|\n\| --- \| --- \|\n((?:\|.*\n)+)', docs, re.M)
        self.assertIsNotNone(table)
        self.assertEqual(tuple(re.findall(r'^\| `([a-z]+)` \|', table.group(1), re.M)), catalog.KEYWORDS)


class Changes(unittest.TestCase):
    def test_entries_keep_their_kind_and_only_move_forward(self):
        reserved = {k: PUBLISHED[k] for k in catalog.COMMON} | {'status': 'reserved'}
        from test_cpu_measurement import measurement
        measured = dict(PUBLISHED, cpu=measurement())
        catalog.changes([reserved], [measured])
        catalog.changes([PUBLISHED], [dict(measured, tag='v1.1.0', commit='1' * 40, cpu=measurement(commit='1'*40))])
        catalog.changes([PUBLISHED], [dict(PUBLISHED, status='retired')])
        catalog.changes([], [measured])
        # A draft is not built yet; a published machine may go back to draft.
        draft = dict(measured, status='draft')
        catalog.changes([PUBLISHED], [draft])
        catalog.changes([draft], [measured])
        catalog.changes([reserved], [draft])
        for after in ([], [dict(PUBLISHED, kind='effect')],
                      [dict(reserved)], [dict(PUBLISHED, id='other')],
                      [dict(PUBLISHED, repository='https://github.com/other/opxy-probe')],
                      [dict(PUBLISHED, maintainer='other')]):
            with self.subTest(after=after), self.assertRaises(ValueError):
                catalog.changes([PUBLISHED], after)
        with self.assertRaises(ValueError):
            catalog.changes([dict(PUBLISHED, status='retired')], [PUBLISHED])

    def test_changing_only_keywords_is_not_an_update(self):
        from test_cpu_measurement import measurement
        # An entry from before measurements were required, and one measured at an earlier commit.
        legacy = UNLISTED
        carried = dict(UNLISTED, commit='1' * 40, cpu=measurement())
        for entry in (legacy, carried):
            with self.subTest(entry=entry['commit']):
                catalog.changes([entry], [dict(entry, keywords=['fm'])])
                catalog.changes([dict(entry, keywords=['fm'])], [dict(entry, keywords=['pad', 'bass'])])
                # A release change alongside a keyword change still needs its measurement.
                with self.assertRaisesRegex(ValueError, 'cpu'):
                    catalog.changes([entry], [dict(entry, keywords=['fm'], tag='v2.0.0')])

    def test_new_and_updated_machines_list_a_keyword(self):
        from test_cpu_measurement import measurement
        measured = dict(UNLISTED, cpu=measurement())
        reserved = {k: PUBLISHED[k] for k in catalog.COMMON} | {'status': 'reserved'}
        # Entries from before keywords stay valid while unchanged, or as they retire.
        catalog.changes([UNLISTED], [UNLISTED])
        catalog.changes([UNLISTED], [dict(UNLISTED, status='retired')])
        catalog.changes([], [reserved])
        for before, after in (([], measured), ([reserved], measured), ([reserved], dict(measured, status='draft')),
                              ([UNLISTED], dict(measured, tag='v1.1.0')), ([PUBLISHED], UNLISTED),
                              ([], dict(UNLISTED, kind='player'))):
            with self.subTest(after=after), self.assertRaisesRegex(ValueError, 'at least one keyword'):
                catalog.changes(before, [after])
            catalog.changes(before, [dict(after, keywords=['pad'])])
        # Extensions are not listed by keyword.
        extension = dict(UNLISTED, id='probe_extension', kind='extension')
        catalog.changes([], [extension])


class Fetching(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        root = Path(self.temporary.name).resolve()
        # A machine repository: the probesynth fixture renamed to probe, committed and tagged.
        self.repository = root / 'opxy-probe'
        shutil.copytree(FOLDER / 'probesynth', self.repository)
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
        # An edit git status cannot see is still a change.
        catalog.git('checkout', '--', 'engine.cpp', cwd=m.root)
        catalog.git('update-index', '--assume-unchanged', 'engine.cpp', cwd=m.root)
        (m.root / 'engine.cpp').write_text('// hidden\n', encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'changed'):
            catalog.fetch(self.entry, self.cache)

    def test_prune_keeps_only_the_pinned_folders(self):
        m = catalog.fetch(self.entry, self.cache)
        earlier = self.cache / 'probe-0123456789ab'
        catalog.git('clone', '-q', str(self.repository), str(earlier))
        (self.cache / 'probe-fedcba987654.partial').mkdir()
        (self.cache / 'collection.json').write_text('{}', encoding='utf-8')
        self.assertEqual(catalog.prune(self.cache, [m.root]), ['probe-0123456789ab', 'probe-fedcba987654.partial'])
        self.assertEqual(sorted(p.name for p in self.cache.iterdir()),
                         sorted(['collection.json', m.root.relative_to(self.cache.resolve()).parts[0]]))
        self.assertEqual(catalog.fetch(self.entry, self.cache).root, m.root)

    def test_an_interrupted_fetch_is_started_again(self):
        broken = self.cache / f"probe-{self.entry['commit'][:12]}"
        broken.mkdir(parents=True)
        catalog.git('init', '--quiet', cwd=broken)
        self.assertEqual(catalog.fetch(self.entry, self.cache).root, broken)

    def test_fetch_refuses_a_machine_that_differs_from_its_entry(self):
        for change in (dict(kind='effect'), dict(kind='sample')):
            with self.subTest(change=change), self.assertRaisesRegex(ValueError, 'differs'):
                catalog.fetch(dict(self.entry, **change), self.cache / str(len(change)))

    def test_pinned_means_fetched_and_unchanged(self):
        m = catalog.fetch(self.entry, self.cache)
        listing = Path(self.temporary.name) / 'machines.json'
        listed = dict(self.entry, repository=PUBLISHED['repository'])  # pinned() never contacts it
        listing.write_text(json.dumps(document(listed)), encoding='utf-8')
        self.assertTrue(catalog.pinned(m, catalog.load(listing)))
        listing.write_text(json.dumps(document(dict(listed, commit='1' * 40))), encoding='utf-8')
        self.assertFalse(catalog.pinned(m, catalog.load(listing)))


class KeywordOnlyChanges(unittest.TestCase):
    """`check --base` and `test --changed-from` pass over an entry whose keywords alone
    changed, and still take one whose release moved with them."""
    def setUp(self):
        from test_cpu_measurement import measurement
        # Above the recommended CPU, so check names each entry it reviews with a warning.
        self.before = [dict(PUBLISHED, id='same', cpu=measurement()), dict(PUBLISHED, id='moved', cpu=measurement())]
        self.after = [dict(self.before[0], keywords=['pad']),
                      dict(self.before[1], keywords=['pad'], tag='v2.0.0', commit='1' * 40, cpu=measurement(commit='1' * 40))]
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.cache = Path(temporary.name)
        # No git, network or machine build: the catalogs, their base and fetching are given.
        self.patches = [mock.patch.object(catalog, 'load', lambda: self.after),
                        mock.patch.object(catalog, 'load_extensions', lambda: []),
                        mock.patch.object(catalog, 'base_entries',
                                          lambda base, listed, path: copy.deepcopy(self.before) if listed == 'machines' else []),
                        mock.patch.object(catalog, 'release_controls', lambda *args: []),
                        mock.patch.object(catalog, 'fetch',
                                          lambda entry, cache: SimpleNamespace(id=entry['id'], root=self.cache / entry['id']))]
        for patcher in self.patches:
            patcher.start()
            self.addCleanup(patcher.stop)

    def run_main(self, *argv):
        out = io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
            return catalog.main(list(argv)), out.getvalue()

    def test_check_reviews_only_the_moved_release(self):
        status, out = self.run_main('check', '--base', 'base')
        self.assertEqual(status, 0, out)
        self.assertIn('WARNING: moved:', out)
        self.assertNotIn('WARNING: same:', out)
        # Its peak is above the CPU-high threshold, so check says the builder labels it.
        self.assertIn('NOTE: moved: the builder labels it CPU-high: worst observed block peak', out)
        self.assertNotIn('NOTE: same:', out)
        # The moved release still needs its measurement; the keyword change alone does not.
        self.after[1] = {k: v for k, v in self.after[1].items() if k != 'cpu'}
        status, out = self.run_main('check', '--base', 'base')
        self.assertEqual(status, 1)
        self.assertIn('moved: new or updated submissions require hardware cpu', out)

    def test_changed_from_tests_only_the_moved_release(self):
        with mock.patch('tooling.machine_test.main', return_value=0) as machine_test:
            status, out = self.run_main('test', '--changed-from', 'base', '--cache', str(self.cache))
        self.assertEqual(status, 0, out)
        self.assertIn('Testing 1 machine and 0 extension entries', out)
        machines = [a for a in machine_test.call_args.args[0] if a.startswith('--machine=')]
        self.assertEqual(machines, [f'--machine={self.cache / "moved"}'])


class Controls(unittest.TestCase):
    """A moved release keeps what each saved parameter value means."""

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        root = Path(self.temporary.name).resolve()
        self.repository = root / 'opxy-probe'
        shutil.copytree(FOLDER / 'probesynth', self.repository)
        record = json.loads((self.repository / 'machine.json').read_text(encoding='utf-8'))
        record['identity'] = dict(engine=5, page=85)
        (self.repository / 'machine.json').write_text(json.dumps(record), encoding='utf-8')
        self.definition = json.loads((self.repository / 'definition.json').read_text(encoding='utf-8'))
        self.definition['id'] = 'probe'
        # Four positions and one alternate, so a release can remove a parameter.
        self.parameters = self.definition['parameters'] + [self.alternate('spread')]
        subprocess.run(['git', 'init', '-q', '-b', 'main'], cwd=self.repository, check=True, capture_output=True,
                       creationflags=catalog.NO_WINDOW)
        self.before = dict(PUBLISHED, repository=self.repository.as_uri(), commit=self.release(self.parameters))
        self.cache = root / 'cache'

    @staticmethod
    def alternate(name):
        return dict(id=name, label=name.title(), default=0.5, mapping=dict(kind='linear', minimum=0, maximum=1),
                    display='percent', control=0)

    def release(self, parameters):
        definition = dict(self.definition, parameters=parameters,
                          presets={'Init': {p['id']: p['default'] for p in parameters}})
        (self.repository / 'definition.json').write_text(json.dumps(definition), encoding='utf-8')
        for command in (['add', '-A'], ['-c', 'user.name=t', '-c', 'user.email=t@t', 'commit', '-q',
                                        '--allow-empty', '-m', 'release']):
            subprocess.run(['git', *command], cwd=self.repository, check=True, capture_output=True,
                           creationflags=catalog.NO_WINDOW)
        return catalog.git('rev-parse', 'HEAD', cwd=self.repository).strip()

    def update(self, parameters, allow=False):
        after = dict(self.before, tag='v1.1.0', commit=self.release(parameters))
        return catalog.release_controls([self.before], [after], self.cache, allow)

    def test_unchanged_controls_pass_and_new_text_is_reported(self):
        self.assertEqual(self.update(copy.deepcopy(self.parameters)), [])
        relabelled = copy.deepcopy(self.parameters)
        relabelled[0].update(label='Harmonic', default=0.5)
        self.assertEqual(self.update(relabelled), ['probe v1.0.0 -> v1.1.0: parameter 0 (shape): label Shape -> Harmonic',
                                                   'probe v1.0.0 -> v1.1.0: parameter 0 (shape): default 0.3333333333333333 -> 0.5'])
        # A release that keeps its commit is not fetched.
        self.assertEqual(catalog.release_controls([self.before], [dict(self.before, repository='file:///missing')]), [])

    def test_an_appended_parameter_passes(self):
        report = self.update(self.parameters + [self.alternate('depth')])
        self.assertEqual(report, ['probe v1.0.0 -> v1.1.0: parameter 5 (depth): added'])

    def test_control_changes_fail_naming_the_parameter(self):
        reordered = copy.deepcopy(self.parameters)
        reordered[1], reordered[2] = reordered[2], reordered[1]
        remapped = copy.deepcopy(self.parameters)
        remapped[3]['mapping']['maximum'] = 8000
        renamed = copy.deepcopy(self.parameters)
        renamed[3]['id'] = 'release'
        moved = copy.deepcopy(self.parameters)
        moved[4]['control'] = 2
        for parameters, message in ((self.parameters[:4], r'parameter 4 \(spread\): removed'),
                                    (reordered, r'parameter 1 \(width\): moved to parameter 2'),
                                    (remapped, r'parameter 3 \(motion\): mapping changed'),
                                    (renamed, r'parameter 3 \(motion\): id changed to release'),
                                    (moved, r'parameter 4 \(spread\): control position changed from 0 to 2')):
            with self.subTest(message=message):
                with self.assertRaisesRegex(ValueError, '(?s)' + message + '.*controls-changed'):
                    self.update(parameters)
                # A catalog maintainer's approval lets it through, still reported.
                self.assertTrue(any(re.search(message, line) for line in self.update(parameters, allow=True)))

    def test_the_check_command_compares_with_the_base_catalog(self):
        # Without a moved release, check --base needs no network or flag.
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(catalog.main(['check', '--base', 'HEAD', '--cache', str(self.cache)]), 0)


if __name__ == '__main__':
    unittest.main()
