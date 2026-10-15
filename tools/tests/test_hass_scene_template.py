"""Render the actual fixed HA scene query with Jinja, including its bounds."""
import ast
import json
from pathlib import Path
import re
from types import SimpleNamespace
import unittest
from jinja2 import Environment, StrictUndefined

SOURCE = Path(__file__).resolve().parents[2] / 'app/espdl-quickapp/overlay/apps/system/hass/hass_scenes_template.h'
QUERY = json.loads(''.join(ast.literal_eval(part) for part in re.findall(r'"(?:\\.|[^"\\])*"', SOURCE.read_text(encoding='utf-8'))))['template']


class SceneTemplateTest(unittest.TestCase):
    def render(self, scenes, scripts):
        return json.loads(Environment(undefined=StrictUndefined).from_string(QUERY).render(states=SimpleNamespace(scene=scenes, script=scripts)))

    def entity(self, domain, index, name='回家场景'):
        return SimpleNamespace(entity_id=f'{domain}.preset_{index}', state='unknown' if domain == 'scene' else 'off', name=name, attributes={})

    def test_empty_catalog_and_both_domains(self):
        self.assertEqual(self.render([], []), {'items': [], 'total': 0})
        scene, script = self.entity('scene', 0, '回家 "灯光"'), self.entity('script', 0, '一键睡眠')
        script.attributes['last_triggered'] = '2026-10-15T12:00:00+00:00'
        result = self.render([scene], [script])
        self.assertEqual(result['total'], 2)
        self.assertEqual(result['items'][0]['attributes']['friendly_name'], scene.name)
        self.assertEqual(result['items'][1]['attributes']['last_triggered'], script.attributes['last_triggered'])

    def test_server_applies_record_and_string_bounds_but_preserves_total(self):
        scenes = [self.entity('scene', i, '灯' * 500) for i in range(140)]
        scenes[0].state = 'a' * 500
        scenes[0].attributes['last_triggered'] = 'b' * 500
        result = self.render(scenes, [])
        self.assertEqual(result['total'], 140)
        self.assertEqual(len(result['items']), 128)
        row = result['items'][0]
        self.assertEqual(len(row['state']), 128)
        self.assertEqual(len(row['attributes']['friendly_name']), 64)
        self.assertEqual(len(row['attributes']['last_triggered']), 128)
        self.assertEqual(set(row), {'entity_id', 'state', 'attributes'})


if __name__ == '__main__':
    unittest.main()
