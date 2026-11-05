"""FX III, the third send effect on Aux 5: its effect list, its firmware edits and
its behaviour against stock boundary doubles."""
import dataclasses
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from tooling import aux5_stage, native, stock_tables
from tooling.adapters import FX3_LIMIT, STOCK_CATALOG, catalog_defaults, fx3_effects, generate_track5_fx_table
from tooling.definitions import EFFECT_BUFFER_LIMIT
from tooling.firmware import active_services, patch_active
from tooling.pipeline import ROOT, config
from test_sample_services import HOST_CXX
from machine_fixtures import fixture

PLATFORM = ROOT / 'sdk/platform/opxy-1.2.1'
# FX III's own edits, by symbol and address, and the aux controller's four sites,
# which it shares with the aux service (aux_controller.cpp).
FX3_HOOKS = {
    'sdk_track5_key': 0x8c08f624, 'sdk_track5_vector': 0x8d53b5ac,
    'sdk_track5_effect': 0x8d53b5c0, 'sdk_track5_name_call': 0x8d53b83e, 'sdk_track5_shown': 0x8d5299a0,
    'sdk_track5_find_inputs': 0x8d529730, 'sdk_track5_find_inactive': 0x8d529742,
    'sdk_track5_icons': 0x8d529a80, 'sdk_track5_click': 0x8c08f7d2,
}
SHARED = {'sdk_aux_shift_m1_hook': 0x8c08f6ec, 'sdk_aux_page_hook': 0x8c08f540,
          'sdk_aux_key_hook': 0x8c08fd30, 'sdk_aux_turn_hook': 0x8c08fa14}


def effect(n):
    m = fixture('probefx')
    return dataclasses.replace(m, id=f'fx{n}')


class EffectList(unittest.TestCase):
    def test_stock_effects_come_from_the_stock_catalog_then_the_collections_in_order(self):
        stock = json.loads(STOCK_CATALOG.read_bytes())['fx_types']
        synth = fixture('probesynth')
        rows = fx3_effects([effect(2), synth, effect(1)])
        self.assertEqual([r[2] for r in rows], [t['type'] for t in stock] + ['fx2', 'fx1'])
        self.assertEqual([r[0] for r in rows[:len(stock)]], [str(t['id']) for t in stock])
        self.assertTrue(all(r[1] == 'nullptr' for r in rows[:len(stock)]), 'Stock effects are always present')
        self.assertEqual(rows[-1][:2], ('fx1_fx_id', 'fx1_ready'))
        # Defaults: stock's catalog values, and the machine's as its catalog entry has them, Q15 to Q31.
        self.assertEqual(rows[0][3], [v << 16 for v in stock[0]['default_params']])
        self.assertEqual(rows[-1][3], [v << 16 for v in catalog_defaults(effect(1).definition)])

    def test_a_build_without_collection_effects_lists_stocks_alone(self):
        self.assertEqual(len(fx3_effects([fixture('probesynth')])), len(json.loads(STOCK_CATALOG.read_bytes())['fx_types']))

    def test_labels_fit_the_stock_list(self):
        long = dataclasses.replace(fixture('probefx'), id='a' * 32)
        self.assertEqual(fx3_effects([long])[-1][2], 'a' * 15)

    def test_the_list_is_bounded(self):
        stock = len(json.loads(STOCK_CATALOG.read_bytes())['fx_types'])
        fx3_effects([effect(n) for n in range(FX3_LIMIT - stock)])
        with self.assertRaisesRegex(ValueError, 'FX III lists at most'):
            fx3_effects([effect(n) for n in range(FX3_LIMIT - stock + 1)])
        header = (PLATFORM / 'track5_fx.hpp').read_text(encoding='utf-8')
        self.assertIn('const u32 kFx3Limit = %d;' % FX3_LIMIT, header)

    def test_invalid_stock_defaults_are_refused(self):
        catalog = json.loads(STOCK_CATALOG.read_bytes())
        catalog['fx_types'][0]['default_params'][0] = 40000
        with self.assertRaisesRegex(ValueError, 'Invalid FX III effect'):
            fx3_effects([], json.dumps(catalog))

    def test_the_table_takes_firmware_facts_from_their_declarations(self):
        with tempfile.TemporaryDirectory() as directory:
            text = generate_track5_fx_table(directory, [effect(1)]).read_text(encoding='utf-8')
        self.assertIn('sdk_fx3_records = %#x;' % stock_tables.EFFECT_RECORDS, text)
        self.assertIn('sdk_fx3_record_bytes = %d;' % stock_tables.RECORD_BYTES, text)
        self.assertIn('sdk_fx3_record_count = %d;' % stock_tables.EFFECT_COUNT, text)
        self.assertIn('sdk_fx3_buffer_limit = %d;' % EFFECT_BUFFER_LIMIT, text)
        # A collection effect is named by its link-time symbols, so a build without
        # it has nothing to resolve; stock's are numbers.
        self.assertIn('extern const char fx1_fx_id[] __asm__("fx1_fx_id");', text)
        self.assertIn('int fx1_ready() __asm__("fx1_ready");', text)
        self.assertIn('{fx1_fx_id, fx1_ready, "fx1",', text)
        self.assertIn('{reinterpret_cast<const char*>(0), nullptr, "delay",', text)
        source = (PLATFORM / 'track5_fx.cpp').read_text(encoding='utf-8')
        # No effect machine is named in the platform source, nor a relocated table.
        self.assertNotRegex(source, r'\w+_fx_id')
        self.assertNotIn('%#x' % stock_tables.EFFECT_RECORDS, source)
        self.assertNotRegex(source, r'0x80000\b')  # the borrowed scratch offset is the effect buffer limit


class FirmwareEdits(unittest.TestCase):
    def test_fx3_hooks_are_in_every_build(self):
        patches = {p.get('symbol'): p for p in config()['patches'] if p['kind'] == 'hook'}
        for symbol, address in {**FX3_HOOKS, **SHARED}.items():
            with self.subTest(symbol=symbol):
                self.assertEqual(int(patches[symbol]['address'], 16), address)
                self.assertNotIn('service', patches[symbol])
        # No other hook names FX III.
        self.assertEqual(sorted(s for s in patches if s and 'track5' in s), sorted(FX3_HOOKS))
        services = active_services(config(), native.service_users([fixture('probesynth')]))
        self.assertTrue(all(patch_active(patches[s], services) for s in {**FX3_HOOKS, **SHARED}))

    def test_fx3_runs_from_aux_5s_stage_not_a_hook_of_its_own(self):
        self.assertEqual(aux5_stage.EFFECT, ('track5_fx', 'sdk_track5_stage'))
        source = aux5_stage.render(set(native.SDK_UNITS), aux5_stage.EFFECT, aux5_stage.TIMER)
        self.assertIn('    sdk_track5_stage(a, b, bus);', source)
        self.assertIn('__asm__("sdk_track5_stage")', (PLATFORM / 'track5_fx.cpp').read_text(encoding='utf-8'))
        self.assertNotIn('8d51234e', (PLATFORM / 'track5_aux.s').read_text(encoding='utf-8'))

    def test_fx3_units_are_linked_in_every_build(self):
        for unit in ('track5_aux', 'track5_fx', 'track5_fx_table', 'aux_controller', 'aux_controller_hooks'):
            with self.subTest(unit=unit):
                self.assertIn(unit, native.SDK_UNITS)
                self.assertNotIn(unit, native.UNIT_SERVICES)
        units = {step for _name, _m, step in native.plan([fixture('probesynth')])}
        self.assertTrue({'track5_aux', 'track5_fx', 'track5_fx_table'} <= units)

    def test_every_hook_symbol_is_defined_in_its_unit(self):
        sources = ''.join((PLATFORM / name).read_text(encoding='utf-8')
                          for name in ('track5_aux.s', 'aux_controller_hooks.s'))
        for symbol in {**FX3_HOOKS, **SHARED}:
            if symbol in ('sdk_track5_vector', 'sdk_track5_effect', 'sdk_track5_shown', 'sdk_track5_find_inputs',
                          'sdk_track5_find_inactive', 'sdk_track5_icons'):
                # C++ entry points called directly.
                self.assertIn(f'__asm__("{symbol}")', (PLATFORM / 'track5_fx.cpp').read_text(encoding='utf-8'))
            else:
                self.assertRegex(sources, rf'\.global {symbol}\n')


@unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
class Behaviour(unittest.TestCase):
    def test_real_fx3_against_stock_doubles(self):
        with tempfile.TemporaryDirectory(prefix='fx3-') as directory:
            folder = Path(directory)
            # Substitute only the stock-address boundary, as other SDK fixtures do.
            stock = (PLATFORM / 'stock.hpp').read_text(encoding='utf-8')
            stock = stock.replace('typedef __UINTPTR_TYPE__ Address;',
                                  'typedef __UINTPTR_TYPE__ Address;\nAddress host_stock_address(Address);')
            for pointer in ('T*', 'const volatile u32*', 'const volatile unsigned char*'):
                stock = stock.replace('reinterpret_cast<' + pointer + '>(address)',
                                      'reinterpret_cast<' + pointer + '>(host_stock_address(address))')
            (folder / 'stock.hpp').write_text(stock, encoding='utf-8')
            for name in ('track5_fx.cpp', 'track5_fx.hpp', 'stock_browser.hpp'):
                (folder / name).write_bytes((PLATFORM / name).read_bytes())
            binary = folder / ('test.exe' if os.name == 'nt' else 'test')
            hidden = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
            result = subprocess.run([HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror',
                '-I' + str(folder), '-I' + str(ROOT / 'sdk/include'),
                str(ROOT / 'tests/fixtures/track5_fx.cpp'), '-o', str(binary)],
                capture_output=True, text=True, timeout=120, **hidden)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30, **hidden)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
