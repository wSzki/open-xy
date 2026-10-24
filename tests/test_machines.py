"""machine.json: kinds, displays and the stock identity each machine declares."""
import copy
import json
import tempfile
from pathlib import Path
import unittest
from unittest import mock

from tooling import machines
from tooling.pipeline import config

RULES = config()['identities']


def identity(kind, **changes):
    value = {'synth': dict(engine=34, page=82), 'sample': dict(engine=35, page=2),
             'effect': dict(effect=11, page=21)}[kind]
    return dict(value, **changes)


class CollectionSelection(unittest.TestCase):
    def write_recipe(self, folder, contents):
        path = Path(folder) / 'selection.json'
        path.write_text(contents, encoding='utf-8')
        return path.relative_to(machines.ROOT).as_posix()

    def test_default_and_explicit_default_resolve_to_the_same_recipe(self):
        self.assertEqual(machines.collection_file(), 'profiles/collection.json')
        self.assertEqual(machines.collection_file('profiles/collection.json'), machines.collection_file())

    def test_recipe_selects_only_its_listed_machines(self):
        with tempfile.TemporaryDirectory(dir=machines.ROOT) as folder:
            selected = self.write_recipe(folder, json.dumps({'schema': 1, 'machines': ['fm2op']}))
            self.assertEqual([m.id for m in machines.collection(selected)], ['fm2op'])

    def test_recipe_schema_and_json_are_strict(self):
        invalid = ('{', '[]', json.dumps({'schema': True, 'machines': ['fm2op']}),
                   json.dumps({'schema': 1, 'machines': ['fm2op'], 'extra': False}),
                   json.dumps({'schema': 1}))
        with tempfile.TemporaryDirectory(dir=machines.ROOT) as folder:
            for contents in invalid:
                with self.subTest(contents=contents):
                    selected = self.write_recipe(folder, contents)
                    with self.assertRaises(ValueError):
                        machines.collection(selected)

    def test_empty_duplicate_malformed_and_unknown_selections_are_rejected(self):
        invalid = ({'schema': 1, 'machines': []},
                   {'schema': 1, 'machines': ['fm2op', 'fm2op']},
                   {'schema': 1, 'machines': [[]]},
                   {'schema': 1, 'machines': ['unknown']})
        with tempfile.TemporaryDirectory(dir=machines.ROOT) as folder:
            for value in invalid:
                with self.subTest(value=value):
                    selected = self.write_recipe(folder, json.dumps(value))
                    with self.assertRaises(ValueError):
                        machines.collection(selected)

    def test_recipe_rejects_unsafe_and_non_file_paths(self):
        with self.assertRaisesRegex(ValueError, '^Collection must be a repository-relative \\.json file$'):
            machines.collection_file(Path('profiles/collection.json'))
        for unsafe in ('/profiles/collection.json', '../profiles/collection.json',
                       'profiles\\collection.json', 'profiles/collection.json:alternate'):
            with self.subTest(path=unsafe), self.assertRaises(ValueError):
                machines.collection_file(unsafe)
        with tempfile.TemporaryDirectory(dir=machines.ROOT) as folder:
            missing = (Path(folder) / 'missing.json').relative_to(machines.ROOT).as_posix()
            with self.assertRaises(ValueError) as error:
                machines.collection_file(missing)
            self.assertEqual(str(error.exception), 'Missing collection file: ' + missing)
            not_json = Path(folder) / 'selection.txt'
            not_json.write_text('{}', encoding='utf-8')
            with self.assertRaisesRegex(ValueError, 'extension'):
                machines.collection_file(not_json.relative_to(machines.ROOT).as_posix())
            directory = Path(folder) / 'directory.json'
            directory.mkdir()
            with self.assertRaisesRegex(ValueError, 'Missing collection file:'):
                machines.collection_file(directory.relative_to(machines.ROOT).as_posix())

    def test_symlink_to_outside_repository_is_rejected(self):
        with tempfile.TemporaryDirectory(dir=machines.ROOT) as folder, tempfile.TemporaryDirectory(dir=machines.ROOT) as outside:
            root = Path(folder) / 'isolated'
            root.mkdir()
            target = Path(outside) / 'selection.json'
            target.write_text(json.dumps({'schema': 1, 'machines': ['fm2op']}), encoding='utf-8')
            link = root / 'outside.json'
            link.symlink_to(target)
            selected = link.relative_to(root).as_posix()
            with mock.patch.object(machines, 'ROOT', root):
                with self.assertRaisesRegex(ValueError, 'Path escapes its root'):
                    machines.collection_file(selected)

    def test_selected_recipe_rejects_conflicting_identities(self):
        with tempfile.TemporaryDirectory(dir=machines.ROOT) as folder:
            selected = self.write_recipe(folder, json.dumps({'schema': 1, 'machines': ['fm2op', 'macroosc']}))
            original = machines.machine

            def clash(name):
                machine = original(name)
                if name == 'macroosc':
                    return machines.Machine(**dict(machine.__dict__, identity=dict(engine=34, page=82)))
                return machine
            with mock.patch.object(machines, 'machine', clash), self.assertRaisesRegex(ValueError, 'Conflicting'):
                machines.collection(selected)


class Identity(unittest.TestCase):

    def test_stock_occupied_or_sentinel_ids_are_rejected(self):
        for kind, change in (('synth', dict(engine=33)), ('synth', dict(engine=2)), ('synth', dict(engine=63)),
                             ('synth', dict(page=81)), ('synth', dict(page=128)), ('synth', dict(engine=True)),
                             ('sample', dict(engine=26)), ('sample', dict(page=82)),
                             ('effect', dict(effect=0)), ('effect', dict(effect=5)), ('effect', dict(page=20))):
            with self.subTest(kind=kind, change=change), self.assertRaises(ValueError):
                machines.check_identity(RULES, kind, identity(kind, **change))

    def test_own_screen_pages_come_from_the_relocated_range(self):
        machines.check_identity(RULES, 'synth', identity('synth'))
        for page in (2, 21, 50, 81):
            with self.subTest(page=page), self.assertRaises(ValueError):
                machines.check_identity(RULES, 'synth', identity('synth', page=page))

    def test_names_and_fields_are_checked(self):
        # The folder name is the stock name, so identity holds only IDs.
        for kind, value in (('synth', identity('synth', name='probe')), ('synth', identity('synth', prefix='probe')),
                            ('effect', dict(identity('effect'), engine=34))):
            with self.subTest(value=value), self.assertRaises(ValueError):
                machines.check_identity(RULES, kind, value)
        for name in ('a' * 16, 'Bad', 'two-op', '', 'drum', 'reverb', 'sdk', 'kit_probe', 'extensions'):
            with self.subTest(name=name), self.assertRaises(ValueError):
                machines.machine(name)
        self.assertEqual((machines.machine('fm2op').native_name, machines.machine('fm2op').prefix), ('fm2op', 'fm2op'))
        with self.assertRaises(ValueError):
            machines.machine('2opfm')

    def test_collection_rejects_conflicting_identities(self):
        original = machines.machine

        def clash(name):
            m = original(name)
            return m if name != 'timestretch' else machines.Machine(**dict(m.__dict__, identity=dict(engine=34, page=2)))
        with mock.patch.object(machines, 'machine', clash), self.assertRaisesRegex(ValueError, 'Conflicting'):
            machines.collection()

    def test_unknown_fields_kinds_and_abi_combinations_are_rejected(self):
        path = machines.ROOT / 'machines/fm2op/machine.json'
        original = json.loads(path.read_text(encoding='utf-8'))
        for change in (dict(extra=1), dict(kind='instrument'), dict(display='segments'), dict(compiler='opxy'),
                       dict(sources=['../macroosc/engine.cpp'])):
            value = dict(copy.deepcopy(original), **change)
            text = json.dumps(value)
            real = Path.read_text
            with self.subTest(change=change), mock.patch.object(
                    Path, 'read_text', lambda self, *a, **k: text if self.name == 'machine.json' else real(self, *a, **k)):
                with self.assertRaises(ValueError):
                    machines.machine('fm2op')

    def test_dry_parameter_requires_a_normalized_effect_control(self):
        real = Path.read_text
        for identity, field in (('ringmod', 'missing'), ('ringmod', 3), ('ringmod', None),
                                ('ringmod', 'frequency'), ('ringmod', 'low_cut'), ('fm2op', 'dry')):
            path = machines.ROOT / 'machines' / identity / 'machine.json'
            value = dict(json.loads(real(path)), dryParameter=field)
            with self.subTest(identity=identity, field=field), mock.patch.object(
                    Path, 'read_text', lambda self, *a, **k: json.dumps(value) if self == path else real(self, *a, **k)):
                with self.assertRaisesRegex(ValueError, 'dryParameter'):
                    machines.machine(identity)
        self.assertEqual(machines.machine('ringmod').dry_parameter, 3)
        self.assertIsNone(machines.machine('fm2op').dry_parameter)
        path = machines.ROOT / 'machines/ringmod/machine.json'
        value = json.loads(real(path))
        del value['dryParameter']
        with mock.patch.object(Path, 'read_text',
                               lambda self, *a, **k: json.dumps(value) if self == path else real(self, *a, **k)):
            self.assertIsNone(machines.machine('ringmod').dry_parameter)


if __name__ == '__main__':
    unittest.main()
