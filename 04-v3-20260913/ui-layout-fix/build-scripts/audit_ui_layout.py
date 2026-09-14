"""Check actual LVGL object bounds; ignore parent/child containment and artwork."""
from pathlib import Path
import argparse
import json
from itertools import combinations
from PIL import Image, ImageDraw

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('directory', type=Path)
args = parser.parse_args()
interactive = {'button', 'switch', 'slider', 'dropdown', 'input', 'keyboard'}
reports = []
for path in sorted(args.directory.glob('*.json')):
    objects = json.loads(path.read_text(encoding='utf-8'))
    if not isinstance(objects, list) or not objects or 'id' not in objects[0]:
        continue
    by_id = {obj['id']: obj for obj in objects}
    def ancestors(obj):
        result = set()
        while obj['parent']:
            result.add(obj['parent'])
            obj = by_id[obj['parent']]
        return result
    def description(obj):
        return {'id': obj['id'], 'type': obj['type'], 'text': obj['text'],
                'rect': [obj[k] for k in ['x','y','w','h']]}
    def rect(obj):
        return (obj['x'], obj['y'], obj['x']+obj['w'], obj['y']+obj['h'])
    def intersect(a, b):
        a, b = rect(a), rect(b)
        return min(a[2],b[2])-max(a[0],b[0]), min(a[3],b[3])-max(a[1],b[1])
    items = [obj for obj in objects if obj['type'] in interactive or
             obj['type'] == 'label' and obj['text']]
    overlaps, overflow = [], []
    for a,b in combinations(items, 2):
        if a['id'] in ancestors(b) or b['id'] in ancestors(a):
            continue
        w,h = intersect(a,b)
        if w > 2 and h > 2:
            overlaps.append([description(a),description(b)])
    for obj in items:
        if not obj['parent']:
            continue
        parent = by_id[obj['parent']]
        if parent['scroll']:
            continue
        a,b = rect(obj),rect(parent)
        if a[0] < b[0]-2 or a[1] < b[1]-2 or a[2] > b[2]+2 or a[3] > b[3]+2:
            overflow.append([description(obj), description(parent)])
    reports.append({'page':path.stem, 'objects':len(objects), 'overlaps':overlaps, 'overflow':overflow})
    print(path.stem, 'overlaps',len(overlaps),'overflow',len(overflow))
output = args.directory / 'layout-audit.json'
output.write_text(json.dumps(reports,ensure_ascii=False,indent=2),encoding='utf-8')
images = sorted(args.directory.glob('*.png'))
for start in range(0,len(images),6):
    batch = images[start:start+6]
    sheet = Image.new('RGB',(1024,330*((len(batch)+1)//2)), '#dce0e5')
    draw = ImageDraw.Draw(sheet)
    for i,path in enumerate(batch):
        x,y = i%2*512, i//2*330
        im = Image.open(path).convert('RGB').resize((512,300))
        sheet.paste(im,(x,y+25))
        draw.text((x+8,y+7),path.stem,fill='black')
    sheet.save(args.directory / ('contact-%02d.jpg' % (start//6)))
