"""Real transition grafts must preserve timing, pitch bounds and outside PCM."""
import unittest
import numpy as np
from .native_bridge import align_patch


class BridgeTests(unittest.TestCase):
    def tone(self):
        t=np.arange(24000)/48000
        return np.rint(5000*np.sin(2*np.pi*300*t)).astype(np.int16)

    def test_natural_transition_identity(self):
        a=self.tone();patch,detail=align_patch(a,a,12000,9600,14400,48000,12000)
        self.assertLessEqual(np.max(abs(patch.astype(int)-a[9600:14400].astype(int))),1)
        self.assertEqual(len(patch),4800)

    def test_graft_preserves_outer_samples_and_bounds_local_time_map(self):
        a=self.tone();b=np.roll(a,51)
        patch,detail=align_patch(a,b,12000,9600,14400,48000,12000)
        out=a.copy();out[9600:14400]=patch
        self.assertTrue(np.array_equal(a[:9601],out[:9601]))
        self.assertTrue(np.array_equal(a[14399:],out[14399:]))
        self.assertLessEqual(abs(detail['local_time_map_slope']-1),.015+1e-9)
        self.assertEqual(len(out),len(a))
        self.assertFalse(np.any((out==-32768)|(out==32767)))

    def test_missing_context_is_rejected(self):
        a=self.tone()
        with self.assertRaisesRegex(ValueError,'handles'):
            align_patch(a,a[:500],100,9600,14400,48000,12000)


if __name__=='__main__':unittest.main()
