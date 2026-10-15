"""Read embedded UI dimensions and field order from the approved HTML preview.

Only presentation metadata is exported; mock values and capabilities stay out of
the firmware. A changed preview must be packaged again to update the contract.
"""
import re


def read_design_contract(path):
    text = path.read_text(encoding='utf-8')

    def rule(selector):
        match = re.search(re.escape('#ha-unified59-devices ' + selector) + r'\{([^}]+)\}', text)
        assert match, selector
        return match[1]

    def number(selector, prop):
        match = re.search(r'(?:^|;)' + re.escape(prop) + r':([\d.]+)(?:px)?(?:;|$)', rule(selector))
        assert match, (selector, prop)
        return float(match[1]) if '.' in match[1] else int(match[1])

    result = {'padding':number('.dc-device-card','padding'),
              'minHeight':number('.dc-device-card','min-height'),
              'headerHeight':number('.dc-card-head','min-height'),
              'headerGap':number('.dc-card-head','margin-bottom'),
              'icon':number('.dc-device-icon','width'),
              'title':number('.dc-card-title','font-size'),
              'columnGap':number('.dc-controls','gap'),
              'primaryMax':number('.dc-primary .dc-control','max-width'),
              'fieldGap':number('.dc-primary,#ha-unified59-devices .dc-secondary','gap'),
              'label':number('.dc-label','font-size'),
              'value':number('.dc-label output','font-size'),
              'labelGap':number('.dc-label','margin-bottom'),
              'rangeHeight':number('.dc-range','height'),
              'rangeRadius':number('.dc-range','border-radius'),
              'thumbHeight':number('.dc-range::-webkit-slider-thumb','height'),
              'buttonHeight':number('.dc-buttons button','min-height'),
              'buttonFont':number('.dc-buttons button','font-size'),
              'buttonGap':number('.dc-buttons','gap'),
              'stepHeight':number('.dc-stepper button','min-height'),
              'stepFont':number('.dc-stepper input','font-size'),
              'metricFont':number('.dc-readout output','font-size'),
              'layouts':{}}
    ratio=re.search(r'minmax\(0,([\d.]+)fr\)',rule('.dc-controls'))
    assert ratio;result['columnRatio']=float(ratio[1])
    # Read just the UI field IDs and labels, never the sample values.
    for match in re.finditer(r"^  (\w+):\{([^\n]+)\},?$",text,re.M):
        name,line=match.groups()
        if 'primary:[' not in line:continue
        primary,rest=line.split('primary:[',1)[1].split('],secondary:[',1)
        secondary,readings=rest.split('],readings:[',1)
        readings=readings.split('],quick:',1)[0]
        fields=lambda s:re.findall(r"\b[RCNDSTA]\('([^']+)','([^']+)'",s)
        result['layouts'][name]={'left':fields(primary),'right':fields(secondary),'readings':fields(readings)}
    assert len(result['layouts'])==11
    return result
