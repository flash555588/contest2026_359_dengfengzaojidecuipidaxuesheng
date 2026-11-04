"""Actual forward/backward and padding invariance checks for the planner."""
import unittest
import torch

from .prosody import Planner, PARAMETER_LIMIT


class PlannerTests(unittest.TestCase):
    def test_padding_does_not_change_valid_predictions(self):
        torch.manual_seed(7)
        model = Planner(40).eval()
        one = model(torch.tensor([[2,3,4]]), torch.tensor([3]))[0]
        batch = model(torch.tensor([[2,3,4,0,0], [5,6,7,8,9]]), torch.tensor([3,5]))[0,:3]
        torch.testing.assert_close(one, batch, atol=2e-6, rtol=1e-5)
        self.assertLess(sum(p.numel() for p in model.parameters()), PARAMETER_LIMIT)

    @unittest.skipUnless(torch.cuda.is_available(), 'CUDA not available')
    def test_real_cuda_forward_and_backward_with_cpu_sequence_lengths(self):
        model = Planner(40).cuda()
        prediction = model(torch.tensor([[2,3,4],[5,6,0]], device='cuda'), torch.tensor([3,2]))
        loss = prediction.square().mean()
        self.assertTrue(torch.isfinite(loss).item())
        loss.backward()
        gradients = [p.grad for p in model.parameters() if p.grad is not None]
        self.assertTrue(gradients)
        self.assertTrue(all(torch.isfinite(g).all().item() for g in gradients))
        self.assertGreater(sum(g.abs().sum().item() for g in gradients), 0)


if __name__ == '__main__':
    unittest.main()
