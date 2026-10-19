#!/usr/bin/env python3
"""Build a lossless optimization branch beside the immutable JIZURA reference."""
from pathlib import Path
import argparse
import json
import shutil
from hashlib import sha256
from jizura_full import ROOT, SOURCE, REPO, build as build_reference, serve


def once(text, old, new):
    if text.count(old) != 1:
        raise ValueError('Pinned patch anchor changed: ' + old[:70])
    return text.replace(old, new, 1)


def patch(text, stage=3):
    start = text.index('  const N = S * S, A = new Uint8Array(N);')
    end = text.index('  const pieces = [];', start)
    text = text[:start] + '''  const K = JizuraGlyphKernel.segment(img, S, S, res);
  const { labels: L, boxes, remap, nl } = K;
  J.portLastGlyph = K.stats;
''' + text[end:]
    start = text.index("    const pc = document.createElement('canvas'); pc.width = w; pc.height = h;")
    end = text.index('    pieces.push({', start)
    text = text[:start] + '''    const alpha = new Uint8Array(w * h);
    for (let yy = 0; yy < h; yy++) for (let xx = 0; xx < w; xx++) {
      const p = (b.y0 + yy) * S + (b.x0 + xx);
      if (remap[L[p]] === l && L[p]) alpha[yy * w + xx] = img[p * 4 + 3];
    }
    const pc = J.portSprites.registerMask(alpha, w, h);
''' + text[end:]
    text = once(text, 'id: ++_pid, cv: pc, res,', 'id: ++_pid, get cv() { return pc.get(); }, res, portMask: pc,')
    start = text.index('  sprite(piece, color) {')
    end = text.index('\n}\nJ.glyphs = new GlyphCache();', start)
    text = text[:start] + '''  sprite(piece, color) { return J.portSprites.tint(piece, color); }
''' + text[end:]
    text = once(text, 'J.glyphs = new GlyphCache();', 'J.portSprites = new JizuraPortSprites();\nJ.glyphs = new GlyphCache();')
    text = once(text, 'clear() { this.map.clear(); this.tint.clear(); }',
                'clear() { this.map.clear(); this.tint.clear(); J.portSprites.clear(); }')
    text = once(text, 'if (++n > 600) break; } this.tint.clear(); }',
                'if (++n > 600) break; } this.tint.clear(); J.portSprites.clear(); }')
    if stage == 1:
        return text
    text = once(text, '''for (let i = 0; i < events.length; i++) {
      const ev = events[i]; if (ev.t > t) break; const dt = (t - ev.t) * 24;''',
                '''for (let i = JizuraFrameHot.recentStart(events, t); i < events.length; i++) {
      const ev = events[i]; if (ev.t > t) break; const dt = (t - ev.t) * 24;''')
    text = once(text, 'const active = plan.events.filter(ev => t >= ev.t && t < ev.t + Math.max(ev.dur, 1 / plan.fps));',
                'const active = JizuraFrameHot.activeEvents(plan.events, t, plan.fps);')
    text = once(text, '''      const g = ctx.createRadialGradient(W / 2, H * 0.45, 0, W / 2, H / 2, Math.hypot(W, H) * 0.6);
      const lift = J.lum(sc.bg) < 0.5 ? 'rgba(255,255,255,0.045)' : 'rgba(255,255,255,0.10)';
      g.addColorStop(0, lift); g.addColorStop(1, 'rgba(0,0,0,0)');''',
                '''      const lift = J.lum(sc.bg) < 0.5 ? 'rgba(255,255,255,0.045)' : 'rgba(255,255,255,0.10)';
      const g = JizuraFrameHot.background(this, ctx, W, H, scale, lift);''')
    text = once(text, '''      const vg = ctx.createRadialGradient(cw / 2, ch / 2, Math.min(cw, ch) * 0.35, cw / 2, ch / 2, Math.hypot(cw, ch) * 0.62);
      vg.addColorStop(0, 'rgba(0,0,0,0)'); vg.addColorStop(1, `rgba(0,0,0,${0.28 * (fx.texture ?? 0.6)})`);''',
                '''      const vg = JizuraFrameHot.vignette(this, ctx, cw, ch, 0.28 * (fx.texture ?? 0.6));''')
    if stage == 2:
        return text
    text = once(text, " || (!opt.fast && (st.glow || 0) > 0);", ";")
    text = once(text, "const pat = ctx.createPattern(this.scan, 'repeat');", "const pat = JizuraCanvasHot.pattern(this, ctx, this.scan);")
    text = once(text, "const pat = ctx.createPattern(img, 'repeat');", "const pat = JizuraCanvasHot.pattern(this, ctx, img);")
    text = once(text, '/* layout: glyph centres', '''const portTypeset = J.typesetLine, portSmallKana = J.isSmallKana;
J.portTextLines = new JizuraCanvasHot.Lines();
const portPrepareLine = line => J.typesetLine === portTypeset && J.isSmallKana === portSmallKana
  ? J.portTextLines.get(line, J.TYPESET, portTypeset)
  : (() => { const arr = [...line]; return { arr, ts: J.typesetLine(arr) }; })();

/* layout: glyph centres''')
    anchor = 'const sets = lines.map(line => { const arr = [...line]; return { arr, ts: J.typesetLine(arr) }; });'
    if text.count(anchor) != 2:
        raise ValueError('Pinned horizontal/vertical text preparation changed.')
    text = text.replace(anchor, 'const sets = lines.map(portPrepareLine);')
    return text


def build(target):
    build_reference(target)
    sources = sorted((SOURCE / 'src').glob('*.js'))
    original = '\n'.join(p.read_text(encoding='utf-8') for p in sources)
    helper_names = ['glyph-kernel.js', 'sprites.js', 'frame-hot.js', 'canvas-hot.js']
    helpers = '\n'.join((ROOT / 'port' / n).read_text(encoding='utf-8') for n in helper_names)
    optimized = helpers + '\n' + patch(original)
    (target / 'reference-engine.js').write_text(original, encoding='utf-8')
    (target / 'optimized-engine.js').write_text(optimized, encoding='utf-8')
    fixture = ROOT/'tests/fixtures'
    lock = json.loads((fixture/'stage1.lock.json').read_text(encoding='utf-8'))
    stage1 = (fixture/'stage1-glyph-kernel.js').read_text(encoding='utf-8')+'\n'+(fixture/'stage1-sprites.js').read_text(encoding='utf-8')+'\n'+patch(original, stage=1)
    if sha256(stage1.encode()).hexdigest() != lock['engine_sha256_lf']:
        raise ValueError('Frozen stage-1 renderer changed; do not compare against a moving baseline.')
    (target/'stage1-engine.js').write_text(stage1, encoding='utf-8')
    stage2 = '\n'.join((fixture/('stage2-'+n)).read_text(encoding='utf-8') for n in helper_names[:3])+'\n'+patch(original, stage=2)
    stage2_lock = json.loads((fixture/'stage2.lock.json').read_text(encoding='utf-8'))
    if sha256(stage2.encode()).hexdigest() != stage2_lock['engine_sha256_lf']:
        raise ValueError('Frozen stage-2 renderer changed.')
    (target/'stage2-engine.js').write_text(stage2, encoding='utf-8')
    port = target / 'port'; port.mkdir(exist_ok=True)
    for name in helper_names:
        shutil.copyfile(ROOT / 'port' / name, port / name)
    edition = target / 'optimized/zh-hans'; edition.mkdir(parents=True, exist_ok=True)
    page = patch((target / 'zh-hans/index.html').read_text(encoding='utf-8'))
    page = once(page, '<head>', '<head>\n'+''.join('<script src="../../port/'+n+'"></script>\n' for n in helper_names))
    (edition / 'index.html').write_text(page, encoding='utf-8')
    for name in ['optimization.html', 'optimization.js', 'optimization-frame.html', 'canvas-export.html']:
        shutil.copyfile(ROOT / 'tests' / name, target / name)
    manifest = dict(stage=3, upstream_unchanged=True, reference_engine_sha256=sha256((target/'reference-engine.js').read_bytes()).hexdigest(),
                    optimized_engine_sha256=sha256((target/'optimized-engine.js').read_bytes()).hexdigest(),
                    stage1_engine_sha256=sha256((target/'stage1-engine.js').read_bytes()).hexdigest(),
                    stage2_engine_sha256=sha256((target/'stage2-engine.js').read_bytes()).hexdigest(),
                    text_cache_max_entries=128, text_cache_max_chars=4096,
                    additional_frame_surfaces=0,
                    sprite_raster_budget_bytes=4*1024*1024,
                    preserved=['all effect registrations', 'original piece IDs/order', 'thresholds and dilation priority', 'font rasterization and resolution'])
    (target / 'optimization-build.json').write_text(json.dumps(manifest, indent=2)+'\n', encoding='utf-8')
    print('JIZURA_OPTIMIZED_READY', target)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=['build', 'serve'])
    parser.add_argument('--output', type=Path, default=REPO.parent/'output/jizura-optimized')
    parser.add_argument('--port', type=int, default=8768)
    args = parser.parse_args()
    if args.command == 'build':
        build(args.output.resolve())
    else:
        serve(args.port, args.output)
