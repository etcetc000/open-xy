"""The player backport cannot drop or alter an existing private firmware edit."""
import hashlib
import json
import unittest
from tooling.pipeline import ROOT, config
from tooling.startup import render

class PrivatePlayerIntegration(unittest.TestCase):
    def test_private_profile_contract_is_preserved(self):
        profile = config()
        player_sites = {int(p['address'], 16) for p in profile['patches'] if p.get('service') == 'players'}
        self.assertEqual(player_sites, {0x8d57ee6c, 0x8d588d94, 0x8c0af13a, 0x8c0af43e, 0x8d5bddd8, 0x8d5bde60, 0x8c084d18})
        profile['patches'] = [p for p in profile['patches'] if p.get('service') != 'players']
        octaves = [p for p in profile['patches'] if p.get('service') == 'octaves']
        self.assertEqual(len(octaves), 1)
        self.assertEqual(octaves[0]['symbol'], 'sdk_octave_store')
        octaves[0].update(symbol='sdk_sample_octave_store', service='samples')
        profile['identities'].pop('players')
        profile['stock_catalog_inserts'].pop('seq_types')
        profile['services'].pop('players'); profile['services'].pop('octaves')
        # The custom menu artwork evolves independently of the firmware ABI.
        self.assertEqual(profile['arranger_svg_sha256'], hashlib.sha256(
            (ROOT / 'sdk/platform/opxy-1.2.1/arranger.svg').read_bytes()).hexdigest())
        profile['arranger_svg_sha256'] = 'f6900fee318e304cbe80cd3deba89ef230a084678cbbe504499022be88421bce'
        self.assertEqual(hashlib.sha256(json.dumps(profile, sort_keys=True, separators=(',', ':')).encode()).hexdigest(), 'fb17bba0389f23dce36c9ad742fc8a048ebc0fd874172408b7004db4d27f1680')

    def test_fixed_player_pages_precede_dynamic_sample_registration(self):
        source = render(['instrument', 'granular'], ['effect'], ['granular'], ['granular'], ['effect'], player_pages=['chords'])
        self.assertLess(source.index('    chords_screen_register();'), source.index('    granular_screen_register();'))
        self.assertIn('sdk_arranger_screen_startup();', source)
        self.assertIn('effect_screen_register();', source)
