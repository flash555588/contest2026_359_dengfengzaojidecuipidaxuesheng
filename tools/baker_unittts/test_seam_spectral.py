"""Verify preservation and physical bounds of sentence-context spectral controls."""
import unittest
import numpy as np
import torch

from .seam_spectral import SeamSpectralNet,filter_controls,proximity,repair_clicks


class SpectralTests(unittest.TestCase):
    def audio(self):
        t=np.arange(24000)/48000
        return np.rint(3000*np.sin(2*np.pi*1000*t)).astype(np.int16)

    def test_zero_controls_are_bit_exact_and_nonzero_gain_preserves_pitch_and_time(self):
        a=self.audio();times=np.linspace(0,.5,100);g=np.zeros((100,24))
        b,_=filter_controls(a,48000,times,g)
        self.assertTrue(np.array_equal(a,b))
        g[:]=3*np.log(10)/10
        b,_=filter_controls(a,48000,times,g)
        self.assertEqual(len(a),len(b))
        self.assertAlmostEqual(np.sqrt(np.mean(b.astype(float)[4000:-4000]**2)/np.mean(a.astype(float)[4000:-4000]**2)),10**(3/20),places=3)
        for x in (a,b):
            spectrum=abs(np.fft.rfft(x))
            self.assertEqual(int(np.argmax(spectrum)),500)

    def test_controls_cannot_exceed_bounds_or_silently_normalize(self):
        a=self.audio();times=np.linspace(0,.5,10)
        with self.assertRaisesRegex(ValueError,'Invalid spectral'):
            filter_controls(a,48000,times,np.full((10,24),5*np.log(10)/10))
        a=(a.astype(np.int32)*10).astype(np.int16)
        with self.assertRaisesRegex(ValueError,'clip'):
            filter_controls(a,48000,times,np.full((10,24),3*np.log(10)/10))

    def test_local_click_repair_preserves_every_sample_outside_logged_window(self):
        a=self.audio();a[11988:12012]=0
        b,rows=repair_clicks(a,48000,[.25],['microfade'])
        self.assertEqual(len(a),len(b));self.assertEqual(len(rows),1)
        first,last=rows[0]['first'],rows[0]['last']
        self.assertTrue(np.array_equal(a[:first],b[:first]))
        self.assertTrue(np.array_equal(a[last+1:],b[last+1:]))
        c,r=repair_clicks(a,48000,[.25],['correlated_voiced_overlap'])
        self.assertTrue(np.array_equal(c,a));self.assertFalse(r)

    def test_sentence_padding_does_not_change_valid_outputs_and_empty_seams_are_identity(self):
        torch.manual_seed(37);net=SeamSpectralNet().eval()
        torch.nn.init.normal_(net.head[-1].weight,std=.1)
        x=torch.randn(1,30,27);x[:,:,24]=.8
        with torch.inference_mode():
            a=net(x,torch.tensor([30]))
            padded=torch.cat((x,torch.randn(1,20,27)),1)
            b=net(padded,torch.tensor([30]))[:,:30]
        self.assertTrue(torch.allclose(a,b,atol=1e-6))
        self.assertTrue(np.array_equal(proximity(np.arange(20),[]),np.zeros(20)))
        x[:,:,24]=0
        with torch.inference_mode():self.assertEqual(int(torch.count_nonzero(net(x,torch.tensor([30])))),0)


if __name__=='__main__':unittest.main()
