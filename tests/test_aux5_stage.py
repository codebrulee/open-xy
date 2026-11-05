"""Aux 5's stage: the SDK's one hook at the mixer's call of Aux 5's processor, and
the slots features fill in place of hooking it themselves."""
import re
import tempfile
import unittest

from tooling import aux5_stage, native
from tooling.pipeline import ROOT, config

PLATFORM = ROOT / 'sdk/platform/opxy-1.2.1'
SITE = 0x8d51234e


class Site(unittest.TestCase):
    def test_the_sdk_owns_the_site_once_in_every_build(self):
        patches = [p for p in config()['patches'] if p.get('address') and int(p['address'], 16) == SITE]
        self.assertEqual(len(patches), 1, 'Features reach the site through aux 5 stage slots')
        hook = patches[0]
        self.assertEqual((hook['kind'], hook['symbol'], hook['call'], hook['before']),
                         ('hook', 'sdk_aux5_call', True, 'ffe339ed'))
        self.assertNotIn('service', hook)
        for unit in ('aux5_stage', 'aux5_hooks'):
            self.assertIn(unit, native.SDK_UNITS)
            self.assertNotIn(unit, native.UNIT_SERVICES)

    def test_the_hook_calls_stock_between_the_slots_with_the_callers_arguments(self):
        source = (PLATFORM / 'aux5_hooks.s').read_text(encoding='utf-8')
        self.assertRegex(source, r'\.global sdk_aux5_call\n')
        body = source[source.index('sdk_aux5_call:'):]
        order = [body.index(s) for s in ('R5 = CYCLES;', 'R2 = [FP + 0x5c];', 'CALL sdk_aux5_effect;',
                                         'R2 = R4;', 'P1.H = 0x8d50; P1.L = 0xfdc0;', 'CALL (P1);',
                                         'R1 = CYCLES;', 'CALL sdk_aux5_time;', 'RTS;')]
        self.assertEqual(order, sorted(order))
        # The caller's R0-R2 reach the processor, and its result returns, through callee-saved registers.
        self.assertIn('[--SP] = (R7:4);', body)
        for line in ('R7 = R0;', 'R6 = R1;', 'R4 = R2;', 'R0 = R7;', 'R1 = R6;'):
            self.assertIn(line, body)


class Slots(unittest.TestCase):
    def render(self, units, effect=None, timer=None):
        return aux5_stage.render(set(units), effect, timer)

    def body(self, source, name):
        return re.search(name + r'\([^)]*\) \{\n(.*?)\n\}', source, re.S).group(1)

    def test_empty_slots_do_nothing(self):
        source = self.render(native.SDK_UNITS)
        self.assertEqual(self.body(source, 'void sdk_aux5_effect'), '    (void)a; (void)b; (void)bus;\n')
        self.assertEqual(self.body(source, 'void sdk_aux5_time'), '    (void)start; (void)end;\n')

    def test_a_linked_units_entry_points_fill_their_slots(self):
        source = self.render({'fx', 'meter'}, ('fx', 'fx_stage'), ('meter', 'meter_time'))
        self.assertIn('void fx_stage(u32, u32, const i32*) __asm__("fx_stage");', source)
        self.assertIn('void meter_time(u32, u32) __asm__("meter_time");', source)
        self.assertIn('    fx_stage(a, b, bus);', self.body(source, 'void sdk_aux5_effect'))
        self.assertIn('    meter_time(start, end);', self.body(source, 'void sdk_aux5_time'))

    def test_a_slot_whose_unit_is_not_linked_stays_empty(self):
        source = self.render({'meter'}, ('fx', 'fx_stage'), ('meter', 'meter_time'))
        self.assertNotIn('fx_stage', source)
        self.assertIn('meter_time(start, end);', source)

    def test_entry_points_must_be_identifiers(self):
        with self.assertRaisesRegex(ValueError, 'Invalid aux 5 stage entry point'):
            self.render({'fx'}, ('fx', 'fx(); evil'))

    def test_the_build_generates_the_unit(self):
        with tempfile.TemporaryDirectory() as directory:
            path = aux5_stage.generate(directory, native.SDK_UNITS)
            self.assertEqual(path.name, 'aux5_stage.cpp')
            self.assertNotIn('@', path.read_text(encoding='utf-8'))


if __name__ == '__main__':
    unittest.main()
