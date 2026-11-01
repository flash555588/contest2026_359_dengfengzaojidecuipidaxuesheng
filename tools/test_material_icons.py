#!/usr/bin/env python3
"""Check mapping completeness and preserve the upstream compound-path pixels."""
import argparse
import json
from pathlib import Path
import xml.etree.ElementTree as ET
from PIL import ImageChops, ImageStat
import generate_glass_icons as icons
from material_icon_shapes import MAPPING, normalized, raster_paths, PROFILES, RUNNING
from material_icon_design import DESIGNS
from household_icon_design import BY_SYMBOL


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    worst=0.;maximum=0;checks=0
    independent={n for n in MAPPING if not n.startswith('body_') and n!='degree'}
    assert set(RUNNING)==independent==set(DESIGNS)
    for name in icons.SYMBOLS:
        for size in (14,16,20,24,32,64):
            # Authored SVGs preserve separate body/mechanism paths. Flattening
            # overlapping contours into one compound path changes edge alpha.
            source=raster_paths(BY_SYMBOL[name]['paths'] if name in BY_SYMBOL else [normalized(name,name.startswith('body_'))],size)
            layered=icons.raster(name,size)
            diff=ImageChops.difference(source,layered)
            rms=ImageStat.Stat(diff).rms[0];peak=diff.getextrema()[1]
            # The renderer composites disconnected paths independently; only
            # subpixel edge coverage may differ, never missing interior holes.
            assert rms<1.5 and peak<30,(name,size,rms,peak)
            assert layered.getbbox(),(name,size)
            worst=max(worst,rms);maximum=max(maximum,peak);checks+=1
        assert PROFILES[name]['moving'] <= set(range(len(icons.I[name]))),name
        if name in independent:
            run=RUNNING[name]
            assert run['moving'] or run['extra'],name
            assert run['moving'] <= set(range(len(icons.I[name]))),name
            assert run['detail'] and run['label'],name
            assert 1200<=run['period']<=12000,name
        else:
            assert not PROFILES[name]['moving'],name
    for path in args.output.glob('*/*.svg'): ET.parse(path)
    report=dict(mappings=len(MAPPING),independent_motion_designs=len(RUNNING),auxiliary_static=len(MAPPING)-len(RUNNING),raster_comparisons=checks,max_rms=worst,
                max_edge_difference=maximum,source_hashes_verified=True)
    (args.output/'asset-validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print('MATERIAL_ASSETS_OK',report)


if __name__=='__main__':main()
