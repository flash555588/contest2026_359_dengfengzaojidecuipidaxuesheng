"""Check real waveform continuity, source-handle bounds and onset protection."""
import unittest
from types import SimpleNamespace
import numpy as np
from .cycle_join import choose_join,join_blocks


class CycleJoinTests(unittest.TestCase):
    def tone(self,phase=0):
        t=np.arange(24000)/48000
        return np.rint(6000*(np.sin(2*np.pi*300*t+phase)+.3*np.sin(2*np.pi*600*t+phase*2))).astype(np.int16)

    def test_continuous_original_boundary_stays_continuous(self):
        a=self.tone()
        out,r=choose_join(a,12000,a,12000,48000,300,300)
        h=r['half_window_samples']
        np.testing.assert_allclose(out,a[12000-h:12000+h],atol=1)
        self.assertEqual(r['left_cut_shift'],r['right_cut_shift'])

    def test_phase_search_matches_misaligned_cycles_without_pitch_warp(self):
        a=self.tone();b=self.tone(1.5)
        out,r=choose_join(a,12000,b,12000,48000,300,300)
        self.assertGreater(r['correlation'],.98)
        self.assertLessEqual(abs(r['timing_change_samples']),120)
        self.assertFalse(r['pitch_warp'])
        self.assertLess(np.max(np.abs(out.astype(int))),8000)

    def test_consonant_onset_is_never_shifted(self):
        a=self.tone();b=self.tone(1.5)
        _,r=choose_join(a,12000,b,12000,48000,300,300,True)
        self.assertEqual(r['method'],'onset_protected_handles')
        self.assertEqual(r['left_cut_shift'],0)
        self.assertEqual(r['right_cut_shift'],0)
        self.assertLessEqual(r['half_window_samples'],36)

    def test_missing_context_does_not_read_past_buffers(self):
        with self.assertRaisesRegex(ValueError,'handles'):
            choose_join(self.tone()[:12],6,self.tone()[:12],6,48000,300,300)

    def test_complete_natural_run_is_not_shortened_or_resynthesized(self):
        a=self.tone();bounds=[(2400,12000),(12000,21600)];blocks=[]
        for i,(start,end) in enumerate(bounds):
            blocks.append(dict(core=a[start:end],context=dict(audio=a,start=start,end=end),
                span=SimpleNamespace(target_start=i),positions=[dict(target_index=i,unit_id=i,
                    output_start=0,output_end=end-start)]))
        plan=dict(tokens=['a1','a1'],phones=[['a1'],['a1']],phone_durations=[[.2],[.2]],
                  f0=[[300.]*24]*2,voiced=[[1.]*24]*2)
        out,report=join_blocks(blocks,48000,plan,True)
        np.testing.assert_array_equal(out,a[2400:21600])
        self.assertEqual(report['positions'][-1]['output_end'],len(out))
        self.assertLess(max(r['mean_f0_error_st'] for r in report['units_after_join']),.1)


if __name__=='__main__':unittest.main()
