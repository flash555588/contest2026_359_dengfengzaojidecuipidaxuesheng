"""Audition controls must preserve identity and remain tied to selected units."""
from pathlib import Path
import tempfile
import unittest
import numpy as np

from . import test_bank_review as fixtures
from .bank_audition import controlled_span
from .controls_audition import duration_tier,map_time
from .synthesize import Inventory


class RenderingTests(unittest.TestCase):
    def test_identity_is_exact_and_cannot_bypass_source_identity_checks(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary)
            _,bank=fixtures.BankContextTests().make_legacy(root)
            inventory=Inventory(bank/'bank.sqlite');spans,_=inventory.select(['hao3'])
            unit=inventory.by_token['hao3'][0]
            control=dict(unit_id=unit.uid,token=unit.token,phones=['h','ao3'],
                         duration_factors=[1.,1.],source_durations=[.01,.03],pitch_delta_semitones=[0.]*24)
            expected,rate=inventory.materialize(spans[0])
            result,_,detail=controlled_span(inventory,spans[0],dict(units=[control]),'both')
            np.testing.assert_array_equal(result,expected);self.assertFalse(detail['modified'])
            control['unit_id']+=100
            with self.assertRaisesRegex(ValueError,'identity'):
                controlled_span(inventory,spans[0],dict(units=[control]),'both')

    def test_time_map_protects_outer_handles_and_combines_phone_factors_once(self):
        times,factors=duration_tier([(.2,.4,1.1),(.4,.7,.9)],1.)
        values=[map_time(t,times,factors) for t in np.linspace(0,1,101)]
        self.assertTrue(np.all(np.diff(values)>0))
        self.assertAlmostEqual(map_time(.2,times,factors),.2,places=4)
        self.assertAlmostEqual(map_time(1.,times,factors),.99,places=4)


if __name__=='__main__':unittest.main()
