"""Produce matched source/teacher/retrieval audition files with explicit provenance."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sqlite3

from .synthesize import Inventory, synthesize

CASES = [
    ('welcome', '欢迎使用语音助手。', 'huan1 ying2 shi3 yong4 yu3 yin1 zhu4 shou3'),
    ('weather', '今天的天气很好。', 'jin1 tian1 de5 tian1 qi4 hen2 hao3'),
    ('light', '我已经打开客厅的灯。', 'wo2 yi3 jing1 da3 kai1 ke4 ting1 de5 deng1'),
    ('music', '请问你想听什么音乐。', 'qing3 wen4 ni2 xiang3 ting1 shen2 me5 yin1 yue4'),
    ('walk', '如果明天有空，我们一起去公园散步。',
     'ru2 guo3 ming2 tian1 you3 kong4 | wo3 men5 yi4 qi3 qu4 gong1 yuan2 san4 bu4'),
    ('time', '现在是下午三点二十五分。', 'xian4 zai4 shi4 xia4 wu3 san1 dian3 er4 shi2 wu3 fen1'),
]


def audition(database: Path, output: Path, planner: Path | None = None, teacher_root: Path | None = None):
    if output.exists():
        raise FileExistsError('Audition output already exists')
    output.mkdir(parents=True)
    inventory = Inventory(database)
    predictor = None
    if planner:
        from .prosody import Predictor
        predictor = Predictor(planner)
    reports = []
    teacher = None
    if teacher_root:
        from .teacher import Teacher, phones_from_pinyin
        teacher = Teacher(next(teacher_root.rglob('fastspeech2_csmsc.onnx')),
                          next(teacher_root.rglob('hifigan_csmsc.onnx')),
                          next(teacher_root.rglob('phone_id_map.txt')))
    for key, text, pinyin in CASES:
        report = synthesize(database, output/key, pinyin=pinyin, inventory=inventory, predictor=predictor)
        report['display_text'] = text
        (output/key/'selection.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
        if teacher:
            phones = []
            for phrase in pinyin.split('|'):
                phones.extend(phones_from_pinyin(phrase.split())+['sp'])
            teacher.synthesize(phones, output/(key+'-teacher'))
        reports.append(dict(key=key, text=text, seconds=report['seconds'],
                            joins=len(report['joins']), parameters=report['active_parameters'],
                            selection_seconds=report['selection_seconds'], quality_passed=False))
        print(json.dumps(reports[-1], ensure_ascii=False), flush=True)
    connection = sqlite3.connect(f'file:{database.resolve().as_posix()}?mode=ro', uri=True)
    heldout = list(connection.execute("SELECT id,path FROM utterances WHERE split='test' ORDER BY id LIMIT 3"))
    for uid, source in heldout:
        tokens = [r[0] for r in connection.execute('SELECT token FROM units WHERE utterance=? ORDER BY ordinal', (uid,))]
        report = synthesize(database, output/('heldout-'+uid), pinyin=' '.join(tokens),
                            inventory=inventory, predictor=predictor)
        import soundfile as sf
        original, rate = sf.read(source, dtype='int16')
        sf.write(output/('heldout-'+uid)/'original.wav', original, rate, subtype='PCM_16')
        reports.append(dict(key='heldout-'+uid, tokens=tokens, source_utterance=uid,
                            source_excluded_from_library=True, seconds=report['seconds'],
                            joins=len(report['joins']), parameters=report['active_parameters'], quality_passed=False))
        print(json.dumps(reports[-1], ensure_ascii=False), flush=True)
    connection.close()
    summary = dict(cases=reports, planner=str(planner) if planner else None,
                   text_frontend_qualified=False, controlled_explicit_pinyin=True,
                   teacher_backend='official ONNX export' if teacher else None,
                   quality_passed=False, board_tested=False)
    (output/'audition.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding='utf-8')
    return summary


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--database', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--planner', type=Path)
    parser.add_argument('--teacher-root', type=Path)
    args = parser.parse_args()
    audition(args.database, args.output, args.planner, args.teacher_root)
