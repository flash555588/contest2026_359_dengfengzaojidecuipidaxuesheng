"""Control target timing, padding, gradients, and known-signal DSP checks."""
import unittest
import numpy as np
import parselmouth
import torch

from .controls_data import sample_pitch,text_alignment,KNOTS,MAX_PHONES
from .controls import ControlPlanner,objective
from .dsp_controls import render_controls


class TargetTests(unittest.TestCase):
    def test_unvoiced_hole_is_not_removed_from_time_axis(self):
        times=np.arange(10)*.005
        hz=np.array([200,210,0,0,0,0,250,260,270,280])
        values,mask=sample_pitch(times,hz,np.ones(10),times)
        np.testing.assert_array_equal(values,hz)
        np.testing.assert_array_equal(mask,hz>0)

    def test_character_alignment_mismatch_is_explicit(self):
        chars,boundaries,aligned=text_alignment('很好#4。',2)
        self.assertTrue(aligned);self.assertEqual(chars,['很','好'])
        self.assertEqual(boundaries.tolist(),[0,4])
        chars,_,aligned=text_alignment('很好。',3)
        self.assertFalse(aligned);self.assertEqual(chars,['','',''])

    def test_controls_have_context_gradients_and_ignore_padding(self):
        torch.manual_seed(12);torch.set_num_threads(2)
        model=ControlPlanner(12,12,12,hidden=32,layers=2)
        model.set_tones([0,0,1,2,3,4,5,1,2,3,4,5]);model.eval()
        tokens=torch.tensor([[2,3,4],[5,6,0]])
        characters=tokens.clone();phones=tokens[:,:,None].expand(-1,-1,MAX_PHONES)
        lengths=torch.tensor([3,2])
        outputs=model(tokens,characters,phones,lengths)
        self.assertEqual(outputs[0].shape,(2,3,MAX_PHONES))
        self.assertEqual(outputs[1].shape,(2,3,KNOTS))
        dmask=torch.ones_like(outputs[0]);dmask[1,2]=0
        batch=dict(dmask=dmask,lengths=lengths,duration_target=torch.ones_like(outputs[0])*.2,
                   pitch_target=torch.ones_like(outputs[1])*.3,voiced=torch.ones_like(outputs[1]),
                   dbase=torch.ones_like(outputs[0])*-2,fbase=torch.ones_like(outputs[1])*5.3)
        loss,_=objective(outputs,batch);loss.backward()
        self.assertTrue(all(torch.isfinite(p.grad).all() for p in model.parameters() if p.grad is not None))
        self.assertGreater(float(model.pitch[-1].weight.grad.abs().sum()),0)
        # Padded targets do not alter the objective.
        altered={k:v.clone() for k,v in batch.items()}
        altered['pitch_target'][1,2]=100;altered['duration_target'][1,2]=100
        other,_=objective(outputs,altered)
        self.assertAlmostEqual(float(loss.detach()),float(other.detach()),places=6)


class DSPTests(unittest.TestCase):
    def source(self):
        t=np.arange(48000)/48000
        return np.rint(8000*(np.sin(2*np.pi*240*t)+.25*np.sin(2*np.pi*480*t))).astype(np.int16)

    def pitch(self,audio):
        pitch=parselmouth.Sound(audio.astype(float)/32768,48000).to_pitch_ac(pitch_floor=75,pitch_ceiling=600)
        hz=pitch.selected_array['frequency'];return np.median(hz[hz>0])

    def test_identity_is_bit_exact(self):
        original=self.source();result,detail=render_controls(original,48000)
        np.testing.assert_array_equal(result,original);self.assertFalse(detail['modified'])

    def test_pitch_control_retains_duration(self):
        audio=self.source();times=np.array([0.,.5,1.])
        result,_=render_controls(audio,48000,times,np.full(3,240*2**(1/12)))
        self.assertLessEqual(abs(len(result)-len(audio)),1)
        self.assertLess(abs(12*np.log2(self.pitch(result)/(240*2**(1/12)))),.15)

    def test_duration_control_retains_pitch(self):
        audio=self.source()
        result,_=render_controls(audio,48000,duration_times=[0.,1.],duration_factors=[1.1,1.1])
        self.assertLess(abs(len(result)/48000-1.1),.003)
        self.assertLess(abs(12*np.log2(self.pitch(result)/240)),.15)

    def test_excessive_duration_is_rejected(self):
        with self.assertRaises(ValueError):
            render_controls(self.source(),48000,duration_times=[0.,1.],duration_factors=[1.,0.])


if __name__=='__main__':unittest.main()
