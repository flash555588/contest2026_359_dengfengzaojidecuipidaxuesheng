"""Small contextual prosody planner, trained only on the inventory's train split.

Predicts unit duration, three voiced log-F0 regions, and log energy. It never
generates waveform samples. Validation utterances are excluded from training.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import random
import sqlite3
import time

import numpy as np
import torch
from torch import nn
from torch.nn.utils.rnn import pad_sequence, pack_padded_sequence, pad_packed_sequence
from torch.utils.data import DataLoader
from .corpus import decode_features

PARAMETER_LIMIT = 25_000_000


class Planner(nn.Module):
    def __init__(self, vocabulary: int, embedding: int = 96, hidden: int = 192):
        super().__init__()
        self.embedding = nn.Embedding(vocabulary, embedding, padding_idx=0)
        self.encoder = nn.GRU(embedding + 3, hidden, num_layers=2, batch_first=True,
                              bidirectional=True, dropout=.15)
        self.head = nn.Sequential(nn.Linear(2*hidden, 128), nn.SiLU(), nn.Linear(128, 5))
        if sum(p.numel() for p in self.parameters()) > PARAMETER_LIMIT:
            raise ValueError('Planner exceeds the active-parameter budget')

    def forward(self, tokens, lengths):
        n = tokens.shape[1]
        position = torch.arange(n, device=tokens.device)[None, :].expand(tokens.shape)
        relative = position / lengths.to(tokens.device)[:, None].clamp_min(1)
        context = torch.stack((relative, 1-relative, (position == 0).float()), dim=-1)
        inputs = torch.cat((self.embedding(tokens), context), dim=-1)
        packed = pack_padded_sequence(inputs, lengths.cpu(), batch_first=True, enforce_sorted=False)
        hidden, _ = self.encoder(packed)
        hidden, _ = pad_packed_sequence(hidden, batch_first=True, total_length=n)
        return self.head(hidden)


def load_sequences(database: Path):
    connection = sqlite3.connect(f'file:{database.resolve().as_posix()}?mode=ro', uri=True)
    vocabulary = {'<pad>': 0, '<unk>': 1}
    sequences = {'train': [], 'validation': [], 'test': []}
    # Vocabulary is built using train labels only; no held-out targets/statistics are fitted.
    for row in connection.execute("SELECT DISTINCT token FROM units JOIN utterances ON units.utterance=utterances.id WHERE split='train' ORDER BY token"):
        vocabulary[row[0]] = len(vocabulary)
    for uid, split in connection.execute('SELECT id,split FROM utterances ORDER BY id'):
        rows = list(connection.execute('SELECT token,features FROM units WHERE utterance=? ORDER BY ordinal', (uid,)))
        labels, targets, masks = [], [], []
        for token, encoded in rows:
            f = decode_features(encoded)
            labels.append(vocabulary.get(token, 1))
            targets.append([np.log(f['duration']), *[np.log(max(x, 75)) for x in f['f0']], f['log_rms']])
            masks.append([1., *([float(f['voiced'] > 0)]*3), 1.])
        if labels:
            sequences[split].append((uid, np.array(labels, np.int64),
                                     np.array(targets, np.float32), np.array(masks, np.float32)))
    connection.close()
    return vocabulary, sequences


def collate(examples):
    tokens = pad_sequence([torch.from_numpy(x[1]) for x in examples], batch_first=True)
    targets = pad_sequence([torch.from_numpy(x[2]) for x in examples], batch_first=True)
    masks = pad_sequence([torch.from_numpy(x[3]) for x in examples], batch_first=True)
    lengths = torch.tensor([len(x[1]) for x in examples])
    return tokens, lengths, targets, masks


def train(database: Path, output: Path, epochs: int = 20, device: str = 'cuda', seed: int = 20261029,
          batch_size: int = 64):
    if output.exists():
        raise FileExistsError('Refusing to overwrite a planner run')
    output.mkdir(parents=True)
    random.seed(seed)
    np.random.seed(seed)
    torch.manual_seed(seed)
    torch.set_num_threads(4)
    vocabulary, data = load_sequences(database)
    if not data['train'] or not data['validation']:
        raise ValueError('Both train and validation utterances are required')
    values = np.concatenate([x[2] for x in data['train']])
    masks = np.concatenate([x[3] for x in data['train']])
    mean = (values*masks).sum(0)/masks.sum(0).clip(1)
    std = np.sqrt(((values-mean)**2*masks).sum(0)/masks.sum(0).clip(1)).clip(.1)
    norm_mean = torch.tensor(mean, device=device)
    norm_std = torch.tensor(std, device=device)
    model = Planner(len(vocabulary)).to(device)
    optimizer = torch.optim.AdamW(model.parameters(), lr=3e-4, weight_decay=1e-4)
    loaders = {s: DataLoader(data[s], batch_size=batch_size, shuffle=s == 'train',
                            num_workers=0, collate_fn=collate) for s in ('train', 'validation')}
    best, history = float('inf'), []
    start = time.monotonic()
    for epoch in range(1, epochs+1):
        metrics = {}
        for split, loader in loaders.items():
            model.train(split == 'train')
            total, count = 0., 0.
            with torch.set_grad_enabled(split == 'train'):
                for tokens, lengths, targets, mask in loader:
                    tokens, targets, mask = tokens.to(device), targets.to(device), mask.to(device)
                    prediction = model(tokens, lengths)
                    truth = (targets-norm_mean)/norm_std
                    losses = torch.nn.functional.smooth_l1_loss(prediction, truth, reduction='none')
                    loss = (losses*mask).sum()/mask.sum().clamp_min(1)
                    if not torch.isfinite(loss):
                        raise RuntimeError('Nonfinite planner loss')
                    if split == 'train':
                        optimizer.zero_grad(set_to_none=True)
                        loss.backward()
                        torch.nn.utils.clip_grad_norm_(model.parameters(), 1.)
                        optimizer.step()
                    total += (losses*mask).sum().item()
                    count += mask.sum().item()
            metrics[split] = total/max(count, 1)
        row = dict(epoch=epoch, **metrics, elapsed_seconds=time.monotonic()-start)
        history.append(row)
        print(json.dumps(row), flush=True)
        if metrics['validation'] < best:
            best = metrics['validation']
            checkpoint = dict(state={k:v.cpu() for k,v in model.state_dict().items()},
                              vocabulary=vocabulary, mean=mean.tolist(), std=std.tolist(),
                              architecture=dict(embedding=96, hidden=192),
                              parameters=sum(p.numel() for p in model.parameters()),
                              epoch=epoch, validation_loss=best, seed=seed,
                              training_utterances=[x[0] for x in data['train']],
                              validation_utterances=[x[0] for x in data['validation']],
                              waveform_generator=False, quality_passed=False)
            torch.save(checkpoint, output/'best.pt')
        (output/'history.json').write_text(json.dumps(history, indent=2), encoding='utf-8')
    report = dict(parameters=sum(p.numel() for p in model.parameters()), parameter_limit=PARAMETER_LIMIT,
                  splits={k:len(v) for k,v in data.items()}, best_validation_loss=best,
                  elapsed_seconds=time.monotonic()-start, quality_passed=False)
    (output/'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    return report


class Predictor:
    def __init__(self, checkpoint: Path):
        torch.set_num_threads(4)
        state = torch.load(checkpoint, map_location='cpu', weights_only=True)
        self.vocabulary = state['vocabulary']
        self.mean, self.std = np.array(state['mean']), np.array(state['std'])
        self.model = Planner(len(self.vocabulary), **state['architecture'])
        self.model.load_state_dict(state['state'], strict=True)
        self.model.eval()
        self.parameters = state['parameters']
        self.sha256 = hashlib.sha256(checkpoint.read_bytes()).hexdigest()

    @torch.inference_mode()
    def __call__(self, tokens: list[str]) -> np.ndarray:
        ids = torch.tensor([[self.vocabulary.get(t, 1) for t in tokens]])
        values = self.model(ids, torch.tensor([len(tokens)]))[0].numpy()
        return values*self.std + self.mean


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--database', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--epochs', type=int, default=20)
    parser.add_argument('--batch-size', type=int, default=64)
    parser.add_argument('--device', default='cuda')
    args = parser.parse_args()
    print(json.dumps(train(args.database, args.output, args.epochs, args.device,
                           batch_size=args.batch_size), indent=2))
