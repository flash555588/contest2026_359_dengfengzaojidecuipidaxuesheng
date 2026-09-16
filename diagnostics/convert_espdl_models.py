"""Convert pinned P4 int8 packed filters to ESP-DL's portable C kernel layout.

Only filter bytes and their descriptive layout strings change. Shapes, graph,
quantization, biases, alignment and the EDL2 envelope retain their exact bytes.
"""
from pathlib import Path
from collections import Counter
import hashlib
import struct
import sys
import numpy as np

WS = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(WS / 'diagnostics/espdl-reference/esp-ppq/esp_ppq/parser/espdl'))
from FlatBuffers.Dl.Model import Model

OPERATORS = {'Conv', 'PRelu', 'Concat', 'Add', 'RequantizeLinear',
             'GlobalAveragePool', 'Transpose', 'Flatten', 'Gemm'}

def unpack_filter(raw, h, w, c, n, depthwise):
    a = n // 16 * 16
    values = np.frombuffer(raw, dtype=np.int8)
    normal = np.empty((n, h, w, c), dtype=np.int8)
    normal[:a] = values[:a*h*w*c].reshape(n//16,h,w,c,16).transpose(0,4,1,2,3).reshape(a,h,w,c)
    tail = values[a*h*w*c:]
    normal[a:] = (tail.reshape(c,h,w,n-a).transpose(3,1,2,0) if depthwise
                  else tail.reshape(n-a,h,w,c))
    # Independent forward exporter check before writing any converted weights.
    repacked = normal[:a].reshape(n//16,16,h,w,c).transpose(0,2,3,4,1).tobytes()
    repacked += (normal[a:].transpose(3,1,2,0) if depthwise else normal[a:]).tobytes()
    assert repacked == raw
    return (normal.transpose(1,2,0,3) if depthwise else normal).tobytes()

def convert(source):
    original = source.read_bytes()
    assert original[:4] == b'EDL2'
    encryption, size, padding = struct.unpack_from('<III', original, 4)
    assert not encryption and not padding and size <= len(original)-16
    graph = Model.GetRootAs(original, 16).Graph()
    tensors = {graph.Initializer(i).Name():graph.Initializer(i)
               for i in range(graph.InitializerLength())}
    output = bytearray(original)
    converted = set()
    records, operations = [], Counter()
    for i in range(graph.NodeLength()):
        node = graph.Node(i)
        op = node.OpType().decode()
        assert op in OPERATORS, op
        operations[op] += 1
        if op not in ('Conv', 'Gemm'):
            continue
        attrs = {node.Attribute(j).Name():node.Attribute(j) for j in range(node.AttributeLength())}
        group = attrs[b'group'].I().I() if b'group' in attrs else 1
        tensor = tensors[node.Input(1)]
        if tensor.Name() in converted:
            raise ValueError('Shared filters require explicit layout ownership review')
        converted.add(tensor.Name())
        assert tensor.DataType() == 3, 'Only int8 filters are supported'
        shape = list(map(int,tensor.DimsAsNumpy()))
        assert len(shape) == 4
        depthwise = group > 1
        if depthwise:
            h,w,n,c = shape
            assert c == 1 and group == n, (shape,group)
        else:
            h,w,c,n = shape
        doc = tensor.DocString()
        assert doc in (b'layout ==> N16HWC16',b'layout ==> N16HWC16_UNALIGNED'), doc
        start = tensor.RawData(0)._tab.Pos
        count = h*w*c*n
        assert count <= tensor.RawDataLength()*16
        raw = original[start:start+count]
        packed = unpack_filter(raw,h,w,c,n,depthwise)
        output[start:start+count] = packed
        # Strings may be shared. Use the same descriptive marker for both layouts.
        offset = tensor._tab.Offset(18) + tensor._tab.Pos
        doc_start = offset + struct.unpack_from('<I',original,offset)[0] + 4
        output[doc_start:doc_start+len(doc)] = b'layout ==> C'.ljust(len(doc),b' ')
        records.append({'tensor':tensor.Name().decode(),'shape':shape,'group':group,
                        'layout':'HWNC' if depthwise else 'NHWC','bytes':count})
    assert len(output) == len(original)
    return bytes(output), {'source_sha256':hashlib.sha256(original).hexdigest(),
                          'operators':dict(operations),'filters':records}
