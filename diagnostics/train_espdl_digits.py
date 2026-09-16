"""Train a real MNIST MLP and export int8 ESP-DL Gemm parameters.

Requires only NumPy. The test split is evaluated once, after choosing an epoch
on the 5,000-example validation split; it is never used for training/calibration.
"""
from pathlib import Path
import os
os.environ['OPENBLAS_NUM_THREADS'] = '4'
os.environ['OMP_NUM_THREADS'] = '4'
import hashlib
import json
import struct
import time
import numpy as np

ws = Path(__file__).resolve().parent.parent
delivery = ws / '04-v3-20260913/espdl-quickapp'
models = delivery / 'models'
models.mkdir(parents=True, exist_ok=True)
datafile = ws / 'diagnostics/downloads/mnist.npz'
data = np.load(datafile, allow_pickle=False)
rng = np.random.default_rng(20260915)
order = rng.permutation(60000)
x = data['x_train'].reshape(60000, 784).astype(np.float32) / 255
y = data['y_train'].astype(np.int64)
trainx, trainy = x[order[:55000]], y[order[:55000]]
validx, validy = x[order[55000:]], y[order[55000:]]
params = [rng.normal(0, np.sqrt(2/784), (784, 128)).astype(np.float32),
          np.zeros(128, dtype=np.float32),
          rng.normal(0, np.sqrt(2/128), (128, 10)).astype(np.float32),
          np.zeros(10, dtype=np.float32)]
moment = [np.zeros_like(p) for p in params]
variance = [np.zeros_like(p) for p in params]
history, step, best = [], 0, -1
started = time.monotonic()
for epoch in range(20):
    permutation = rng.permutation(len(trainx))
    for start in range(0, len(trainx), 256):
        ids = permutation[start:start+256]
        bx = trainx[ids]
        by = trainy[ids]
        # Translate whole minibatches; blank padding, never wrap pixels.
        if epoch % 3 != 0:
            dx, dy = rng.integers(-2, 3, 2)
            image = bx.reshape(-1,28,28)
            padded = np.pad(image, ((0,0),(2,2),(2,2)))
            bx = padded[:,2+dy:30+dy,2+dx:30+dx].reshape(-1,784)
        w1,b1,w2,b2 = params
        hidden = np.maximum(bx @ w1 + b1, 0)
        logits = hidden @ w2 + b2
        probs = np.exp(logits - logits.max(axis=1, keepdims=True))
        probs /= probs.sum(axis=1, keepdims=True)
        probs[np.arange(len(by)), by] -= 1
        probs /= len(by)
        grad2 = hidden.T @ probs + 1e-5 * w2
        gradb2 = probs.sum(axis=0)
        grad_h = (probs @ w2.T) * (hidden > 0)
        grads = [bx.T @ grad_h + 1e-5*w1, grad_h.sum(axis=0), grad2, gradb2]
        step += 1
        rate = 0.001 if epoch < 14 else 0.0004
        for p,m,v,g in zip(params,moment,variance,grads):
            m *= 0.9
            m += 0.1*g
            v *= 0.999
            v += 0.001*g*g
            p -= rate * (m/(1-0.9**step)) / (np.sqrt(v/(1-0.999**step)) + 1e-8)
    accuracy = float((np.argmax(np.maximum(validx@params[0]+params[1],0)@params[2]+params[3],axis=1)==validy).mean())
    history.append({'epoch': epoch+1, 'validation_accuracy': accuracy})
    if accuracy > best:
        best = accuracy
        best_params = [p.copy() for p in params]
        best_epoch = epoch+1
    print(f'Epoch {epoch+1:02d}: validation={accuracy:.4%}', flush=True)

w1,b1,w2,b2 = best_params
def exponent(a):
    return int(np.ceil(np.log2(max(float(np.abs(a).max()), 1e-7)/127)))
def quantize(a, exp, dtype=np.int8):
    limits = np.iinfo(dtype)
    return np.clip(np.rint(a/(2.0**exp)),limits.min,limits.max).astype(dtype)

e_in = -7
e_w1, e_w2 = exponent(w1), exponent(w2)
calib = trainx[:4096]
hidden = np.maximum(calib@w1+b1,0)
e_h = exponent(hidden)
e_out = exponent(hidden@w2+b2)
q_w1,q_w2 = quantize(w1,e_w1),quantize(w2,e_w2)
q_b1,q_b2 = quantize(b1,e_in+e_w1,np.int32),quantize(b2,e_h+e_w2,np.int32)
def infer_int8(images):
    qx = quantize(images,e_in).astype(np.float32)
    acc1 = qx@q_w1.astype(np.float32)+q_b1
    qh = np.clip(np.rint(acc1 * (2.0**(e_in+e_w1-e_h))),0,127)
    acc2 = qh@q_w2.astype(np.float32)+q_b2
    return np.clip(np.rint(acc2 * (2.0**(e_h+e_w2-e_out))),-128,127).astype(np.int8)

testx = data['x_test'].reshape(10000,784).astype(np.float32)/255
testy = data['y_test']
test_float = float(((np.maximum(testx@w1+b1,0)@w2+b2).argmax(axis=1)==testy).mean())
test_scores = infer_int8(testx)
test_quant = float((test_scores.argmax(axis=1)==testy).mean())
assert test_quant >= 0.975, f'Quantized model accuracy is insufficient: {test_quant}'

# QDM1: 64-byte little-endian header, 784x128 + 128x10 int8 filters in
# [1,1,input,output] ESP-DL layout and int32 biases at input+weight exponents.
header = struct.pack('<4sIII5i7I',b'QDM1',784,128,10,e_in,e_w1,e_h,e_w2,e_out,*([0]*7))
blob = header + q_w1.tobytes() + q_b1.astype('<i4').tobytes() + q_w2.tobytes() + q_b2.astype('<i4').tobytes()
model = models/'mnist-mlp-int8.qdm'
model.write_bytes(blob)
fixtures = delivery/'tests/digit-fixtures'
fixtures.mkdir(parents=True,exist_ok=True)
# Public dataset fixtures, not captured user handwriting.
for digit in range(10):
    index = int(np.flatnonzero(testy==digit)[0])
    (fixtures/f'{digit}.gray').write_bytes(data['x_test'][index].tobytes())
    (fixtures/f'{digit}.scores').write_bytes(test_scores[index].tobytes())

report = {'dataset':'MNIST (Keras mirror)',
          'dataset_url':'https://storage.googleapis.com/tensorflow/tf-keras-datasets/mnist.npz',
          'dataset_sha256':hashlib.sha256(datafile.read_bytes()).hexdigest(),
          'seed':20260915,'train_examples':55000,'validation_examples':5000,
          'test_examples':10000,'architecture':[784,128,10],'epochs':20,
          'chosen_epoch':best_epoch,'validation_accuracy':best,
          'test_float_accuracy':test_float,'test_int8_accuracy':test_quant,
          'rounding':'nearest ties to even; symmetric int8; ReLU after first Gemm',
          'exponents':[e_in,e_w1,e_h,e_w2,e_out],'model_bytes':len(blob),
          'model_sha256':hashlib.sha256(blob).hexdigest(),
          'elapsed_seconds':round(time.monotonic()-started,2),'history':history}
(delivery/'evidence/digit-training.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k!='history'},indent=2),flush=True)
