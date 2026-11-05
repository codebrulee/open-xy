"""The SDK's overlay over every page: its startup, artwork and client list."""
import re
import tempfile
import unittest
import xml.etree.ElementTree as ET

from tooling import native, overlay_clients
from tooling.pipeline import ROOT

PLATFORM = ROOT / 'sdk/platform/opxy-1.2.1'


class Overlay(unittest.TestCase):
    def test_every_build_links_it_and_installs_its_artwork(self):
        for unit in ('overlay', 'overlay_clients'):
            self.assertIn(unit, native.SDK_UNITS)
            self.assertNotIn(unit, native.UNIT_SERVICES)
        artwork = (ROOT / native.OVERLAY_ARTWORK).read_bytes()
        root = ET.fromstring(artwork)
        self.assertEqual((root.get('width'), root.get('height')), ('480', '220'))
        source = (PLATFORM / 'overlay.cpp').read_text(encoding='utf-8')
        self.assertIn('const char kAsset[] = "openxy_overlay.svg";', source)

    def test_it_takes_the_last_page_which_no_machine_is_given(self):
        source = (PLATFORM / 'registration_stock.cpp.in').read_text(encoding='utf-8')
        self.assertIn('opxy::u32 sdk_overlay_page() { return @PAGE_LIMIT@ - 1; }', source)
        self.assertIn('n === rules.pages - 1', (ROOT / 'builder/core.mjs').read_text(encoding='utf-8'))

    def test_clients_reach_it_through_the_list_or_at_run_time(self):
        source = (PLATFORM / 'overlay.cpp').read_text(encoding='utf-8')
        self.assertNotRegex(source, r'sdk_cpu_|extension')  # it names no client
        self.assertEqual(source.count('sdk_overlay_clients'), 2)
        self.assertIn('bool sdk_overlay_add(const Client* client)', source)


class Clients(unittest.TestCase):
    def test_none_linked_leaves_an_empty_list(self):
        source = overlay_clients.render({'overlay'}, (('meter', 'sdk_meter_overlay'),))
        self.assertIn('const Client* const sdk_overlay_clients[] = {nullptr};', source)
        self.assertNotIn('sdk_meter_overlay', source)

    def test_a_linked_unit_lists_its_client_in_order(self):
        clients = (('a', 'sdk_a_overlay'), ('b', 'sdk_b_overlay'))
        source = overlay_clients.render({'a', 'b'}, clients)
        self.assertIn('extern const Client sdk_a_overlay __asm__("sdk_a_overlay");', source)
        self.assertIn('{&sdk_a_overlay, &sdk_b_overlay, nullptr};', source)

    def test_a_client_must_be_an_identifier(self):
        with self.assertRaises(Exception):
            overlay_clients.render({'a'}, (('a', 'x); evil('),))

    def test_generate_writes_the_unit(self):
        with tempfile.TemporaryDirectory() as folder:
            path = overlay_clients.generate(folder, set())
            self.assertEqual(path.name, 'overlay_clients.cpp')
            self.assertRegex(path.read_text(encoding='utf-8'), re.escape('{nullptr};'))


if __name__ == '__main__':
    unittest.main()
