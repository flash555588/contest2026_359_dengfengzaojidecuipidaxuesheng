"""Frozen-bank controller consumes complete paired context and bounded controls."""
import unittest
import numpy as np
import torch

from .bank_controls import BankControlNet,bounds,phase_map,loss_function


class BankControlTests(unittest.TestCase):
    def test_phase_mapping_preserves_phone_boundaries_and_order(self):
        values=phase_map(np.array([.1,.3]),np.array([.2,.2]))
        self.assertTrue(np.all(np.diff(values)>0))
        self.assertGreater(values[5],.15)
        center,spread=bounds(['sh','ao3'])
        np.testing.assert_allclose(np.exp(center[:2]-spread[:2]),[.95,.85])
        np.testing.assert_allclose(np.exp(center[:2]+spread[:2]),[1.05,1.18])

    def test_both_neighbors_and_full_center_have_gradients_but_padding_does_not(self):
        torch.manual_seed(12);torch.set_num_threads(2)
        model=BankControlNet(20).eval()
        with torch.no_grad():model.layers[-1].weight.normal_(0,.01)
        frames=torch.randn(2,3,17,33,requires_grad=True)
        lengths=torch.tensor([[17,15,12],[9,16,11]])
        names=torch.randint(1,20,(2,3,3))
        args=(lengths,names,torch.randn(2,512),torch.randn(2,54),torch.rand(2,30)*1.5-.75)
        output=model(frames,*args)
        output.sum().backward()
        for b in range(2):
            for u in range(3):
                n=int(lengths[b,u])
                self.assertGreater(float(frames.grad[b,u,0].abs().sum()),0)
                self.assertGreater(float(frames.grad[b,u,n-1].abs().sum()),0)
                self.assertEqual(float(frames.grad[b,u,n:].abs().sum()),0)
        changed=frames.detach().clone()
        for b in range(2):
            for u in range(3):changed[b,u,int(lengths[b,u]):]=1000
        torch.testing.assert_close(output,model(changed,*args))
        self.assertLessEqual(float(output.detach().abs().max()),1)

    def test_initial_head_reproduces_baseline_and_masks_control_targets(self):
        model=BankControlNet(10).eval();base=torch.ones(2,30)*.4
        output=model(torch.randn(2,3,9,33),torch.full((2,3),9),torch.ones(2,3,3,dtype=torch.long),
                     torch.randn(2,512),torch.randn(2,54),base)
        torch.testing.assert_close(output,base)
        mask=torch.ones_like(output);mask[:,0]=0
        target=output.detach().clone();target[:,0]=100
        self.assertEqual(float(loss_function(output,target,mask).detach()),0)


if __name__=='__main__':unittest.main()
