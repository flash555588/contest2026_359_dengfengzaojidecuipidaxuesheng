"""Check bank-bound learned interfaces and the limits of waveform edits."""
from pathlib import Path
import tempfile
import unittest

import numpy as np
import torch

from .corpus import encode_features,decode_features
from .join_control import ACTION_NAMES,JoinNet,action_join,inputs
from .selector import Ranker


class SpliceTests(unittest.TestCase):
    def test_zero_action_keeps_the_recording_bit_exact(self):
        wave=np.arange(-1000,1000,dtype=np.int16)
        joined,_=action_join(wave[:1000],wave[1000:],48000,0)
        np.testing.assert_array_equal(joined,wave)

    def test_offset_removes_value_jump_without_editing_interiors(self):
        a=np.full(2000,3000,np.int16);b=np.full(2000,-4000,np.int16)
        joined,detail=action_join(a,b,48000,6)
        self.assertEqual(len(joined),4000)
        self.assertLessEqual(abs(int(joined[1999])-int(joined[2000])),1)
        np.testing.assert_array_equal(joined[:1904],a[:1904])
        np.testing.assert_array_equal(joined[2096:],b[96:])

    def test_onset_transient_is_not_cut(self):
        a=np.ones(2000,np.int16)*100
        b=np.zeros(2000,np.int16);b[:48]=15000
        joined,detail=action_join(a,b,48000,8)
        self.assertEqual(detail['removed_samples'],0)
        self.assertEqual(len(joined),4000)

    def test_clipping_action_is_rejected_without_normalizing(self):
        a=np.ones(2000,np.int16)*32000;a[-1]=-30000
        b=np.ones(2000,np.int16)*32000
        result,detail=action_join(a,b,48000,7)
        self.assertEqual(detail.get('rejected'),'clipping')
        np.testing.assert_array_equal(result,np.concatenate((a,b)))

    def test_feature_resampling_is_not_applied_to_the_pcm_output(self):
        a=np.arange(-1000,1000,dtype=np.int16);b=a[::-1].copy()
        saved=a.copy()
        wave,metrics,ids=inputs(a,b,48000,{'a1':1,'b2':2},('a1','b2'))
        self.assertEqual(wave.shape,(1,768));self.assertEqual(metrics.shape,(8,))
        np.testing.assert_array_equal(a,saved)

    def test_compact_features_round_trip(self):
        f=dict(duration=.2,f0=[200.,210.,220.],voiced=.8,log_rms=-3.,gap_before=.01,gap_after=.02,
               left=list(np.linspace(-2,2,24)),right=list(np.linspace(2,-2,24)))
        restored=decode_features(encode_features(f))
        np.testing.assert_allclose(restored['f0'],f['f0'])
        np.testing.assert_allclose(restored['left'],f['left'],atol=1e-6)


class LearnedModelTests(unittest.TestCase):
    @unittest.skipUnless(torch.cuda.is_available(),'CUDA unavailable')
    def test_both_models_have_actual_cuda_gradients(self):
        ranker=Ranker(40).cuda()
        cost=ranker(torch.ones((3,4,5),dtype=torch.long,device='cuda'),torch.randn(3,4,70,device='cuda'))
        cost.mean().backward()
        joiner=JoinNet(40).cuda()
        logits=joiner(torch.randn(3,1,768,device='cuda'),torch.randn(3,8,device='cuda'),
                      torch.ones((3,2),dtype=torch.long,device='cuda'))
        self.assertEqual(logits.shape,(3,len(ACTION_NAMES)))
        logits.square().mean().backward()
        for model in (ranker,joiner):
            self.assertTrue(all(torch.isfinite(p.grad).all().item() for p in model.parameters() if p.grad is not None))
            self.assertGreater(sum(p.grad.abs().sum().item() for p in model.parameters() if p.grad is not None),0.)


if __name__=='__main__':unittest.main()
