"""Which machines a native module links when the collection doesn't fit at once."""
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from tooling import native

FM, MACRO, BIG, SMALL = (SimpleNamespace(id=i) for i in ('fm2op', 'macroosc', 'big', 'small'))
PAGE, MASTER = (SimpleNamespace(id=i) for i in ('page', 'master'))


def linked(machines, too_big, extensions=()):
    """linked_machines with GNU ld replaced: a selection overflows when too_big says so.
    Each call lists its machines, then its extensions as 'extension:<id>'."""
    calls = []
    collection = native.Collection(machines, extensions)

    def link(_directory, _tools, _profile, _definitions, machines_, selected, name='module', runtime_members=(),
             chosen_extensions=None):
        chosen = native.extensions_of(native.subset(machines_, selected, chosen_extensions))
        ids = [m.id for m in selected] + ['extension:' + e.id for e in chosen]
        calls.append((ids, name))
        return '2100.0 of 2032 KiB' if too_big(ids) else None
    with patch.object(native, 'link', link):
        return native.linked_machines(Path('unused'), {}, {}, collection, {}), calls


class LinkedMachines(unittest.TestCase):
    def test_a_collection_that_fits_links_everything_once(self):
        machines = [FM, MACRO, BIG, SMALL]
        result, calls = linked(machines, lambda ids: False)
        self.assertEqual(result, machines)
        self.assertEqual(calls, [(['fm2op', 'macroosc', 'big', 'small'], 'module')])

    def test_otherwise_it_links_the_official_machines_once_each_other_fits_beside_them(self):
        result, calls = linked([FM, BIG, MACRO, SMALL], lambda ids: len(ids) == 4)
        self.assertEqual(result, [FM, MACRO])
        self.assertEqual(calls[1:], [(['fm2op', 'big', 'macroosc'], 'fit'), (['fm2op', 'macroosc', 'small'], 'fit'),
                                     (['fm2op', 'macroosc'], 'module')])

    def test_a_machine_too_big_beside_the_official_ones_fails_by_name(self):
        with self.assertRaisesRegex(ValueError, 'big does not fit beside the official machines, 2100.0 of 2032 KiB'):
            linked([FM, MACRO, BIG, SMALL], lambda ids: 'big' in ids)

    def test_each_machine_and_each_extension_fits_beside_the_official_machines_alone(self):
        # Extensions are chosen like machines: a big machine needn't fit beside them.
        result, calls = linked([FM, MACRO, BIG], lambda ids: len(ids) > 4, [PAGE, MASTER])
        self.assertEqual(list(result), [FM, MACRO])
        self.assertEqual(list(result.extensions), [PAGE, MASTER])
        self.assertEqual(calls[1:], [(['fm2op', 'macroosc', 'big'], 'fit'),
                                     (['fm2op', 'macroosc', 'extension:page'], 'fit'),
                                     (['fm2op', 'macroosc', 'extension:master'], 'fit'),
                                     (['fm2op', 'macroosc', 'extension:page', 'extension:master'], 'module')])

    def test_an_extension_too_big_beside_the_official_ones_fails_by_name(self):
        with self.assertRaisesRegex(ValueError, 'The page extension does not fit beside the official machines'):
            linked([FM, MACRO, SMALL], lambda ids: 'extension:page' in ids, [PAGE])

    def test_official_machines_alone_with_extensions_may_still_overflow_together(self):
        with self.assertRaisesRegex(ValueError, 'The official machines and the extensions exceed'):
            linked([FM, MACRO], lambda ids: len(ids) == 4, [PAGE, MASTER])

    def test_a_collection_without_official_machines_must_fit_at_once(self):
        with self.assertRaisesRegex(ValueError, 'Machines exceed the shared module memory, 2100.0 of 2032 KiB'):
            linked([BIG, SMALL], lambda ids: len(ids) == 2)


class AbsentExtensions(unittest.TestCase):
    def test_an_unchosen_extensions_accessors_resolve_to_kit_absent_as_in_the_builder(self):
        page = SimpleNamespace(id='page', master_processor=None)
        master = SimpleNamespace(id='master', master_processor={'artwork': 'a.svg'})
        self.assertEqual(native.absent_extension_symbols(page), {'sdk_extension_page_hooks': 'kit_absent'})
        self.assertEqual(native.absent_extension_symbols(master), dict.fromkeys(
            ('sdk_extension_master_hooks', 'sdk_extension_master_processor', 'sdk_extension_master_panel'), 'kit_absent'))

    def test_a_subset_keeps_every_extension_unless_told_which(self):
        collection = native.Collection([FM, BIG], [PAGE, MASTER])
        self.assertEqual(native.subset(collection, [FM]).extensions, (PAGE, MASTER))
        self.assertEqual(native.subset(collection, [FM], (MASTER,)).extensions, (MASTER,))
        self.assertEqual(native.subset(collection, [FM], ()).extensions, ())


if __name__ == '__main__':
    unittest.main()
