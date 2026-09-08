"""Export owner's native PauseMenuPlayer SLink frame cues and BARS samples.

Input text is produced by dt-12345/xlink2 --game UKing --platform NX.
FWAV is decoded by vgmstream, not by Android or the gameplay thread.
"""
import argparse
import json
import re
import struct
import subprocess
from pathlib import Path
import oead


def block(text, expression):
    match = re.search(expression + r'\s*\{', text, re.M)
    if not match:
        raise ValueError('Missing native block: ' + expression)
    start = match.end()
    depth = 1
    for i in range(start, len(text)):
        depth += (text[i] == '{') - (text[i] == '}')
        if depth == 0:
            return text[start:i]
    raise ValueError('Unclosed block')


def waves(path):
    data = path.read_bytes()
    assert data[:4] == b'BARS' and data[8:10] == b'\xff\xfe'
    u32 = lambda offset: struct.unpack_from('<I', data, offset)[0]
    count = u32(12)
    result = {}
    for i in range(count):
        meta, audio = struct.unpack_from('<II', data, 16 + count*4 + i*8)
        assert data[meta:meta+4] == b'AMTA'
        string = meta + u32(meta+24)
        assert data[string:string+4] == b'STRG'
        name = data[string+8:data.index(b'\0', string+8)].decode()
        if data[audio:audio+4] == b'FWAV':
            size = u32(audio+12)
            assert audio+size <= len(data)
            result[name] = data[audio:audio+size]
    return result


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--slink', type=Path, required=True)
    p.add_argument('--banks', type=Path, required=True)
    p.add_argument('--animations', type=Path, required=True)
    p.add_argument('--decoder', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--work', type=Path, required=True)
    p.add_argument('--actor-pack', type=Path, required=True)
    a = p.parse_args()
    user = block(a.slink.read_text(encoding='utf-8'), r'^  PauseMenuPlayer')
    tables = block(user, r'AssetCallTables')
    clips = json.loads(a.animations.read_text())['clips']
    pack = a.actor_pack.read_bytes()
    archive = oead.Sarc(oead.yaz0.decompress(pack) if pack[:4] == b'Yaz0' else pack)
    faces = oead.aamp.ParameterIO.from_binary(
        archive.get_file('Actor/AS/PauseMenuPlayer_FaceUseItem.bas').data).to_text()
    cues = {}
    for key, clip in clips.items():
        if key.startswith('Hold'):
            continue
        animation = block(user, r'^        ' + re.escape(clip['source']))
        entries = []
        for event in re.findall(r'0x[0-9a-f]+\s*\{([^{}]+)\}', animation):
            fields = dict(re.findall(r'(\w+)\s*=\s*([^\n]+)', event))
            if fields.get('Type') != 'FrameWindow':
                raise ValueError('Unsupported sound trigger')
            asset = block(tables, r'^      '+re.escape(fields['Asset']))
            # Eating_BBQ and EatingFairy blend several original samples.
            assert 'Execute = Random' not in asset and 'Execute = Switch' not in asset
            for leaf in re.findall(r'Execute = Asset\s*\{([^{}]+)\}', asset):
                params = dict(re.findall(r'(\w+)\s*=\s*([^\n]+)', leaf))
                begin = float(fields['Start']) + float(params.get('Delay', '0'))
                end = min(float(fields['End']), clip['durationMs'] * 30/1000)
                entries.append({'file': params['RuntimeAssetName'].strip('"')+'.wav',
                                'timeMs': round(begin*1000/30),
                                'endMs': round(end*1000/30),
                                'volume': float(params.get('Volume', '1')),
                                'pitch': float(params.get('Pitch', '1'))})
        face_name = 'Face_' + clip['source'].replace('_Repeat', '')
        voices = {}
        for element in re.split(r'Element\d+: !list', faces):
            if 'FileName: !str64 '+face_name+'\n' not in element:
                continue
            for frame, voice in re.findall(r'TypeIndex: 28\s+Frame: ([\d.]+)\s+Value: (\w+)', element):
                voices.setdefault(float(frame), set()).add(voice)
        for frame, variants in voices.items():
            # Fairy's PV619 is a random container; the other AS events select
            # explicit PV606_00/01 leaves. Flatten only these verified voice leaves.
            leaves = []
            for voice in sorted(variants):
                voice_block = block(tables, r'\b'+voice+r'\[0x[0-9a-f]+\]')
                for leaf in re.findall(r'Execute = Asset\s*\{([^{}]+)\}', voice_block):
                    leaves.append(dict(re.findall(r'(\w+)\s*=\s*([^\n]+)', leaf)))
            assert leaves
            # All alternatives for these AS branches share the same gain/delay.
            assert len({(v.get('Volume','1'),v.get('Delay','0')) for v in leaves}) == 1
            entries.append({'file': leaves[0]['RuntimeAssetName'].strip('"')+'.wav',
                            'alternatives': sorted({v['RuntimeAssetName'].strip('"')+'.wav' for v in leaves}),
                            'timeMs': round((frame+float(leaves[0].get('Delay','0')))*1000/30),
                            'endMs': clip['durationMs'],
                            'volume': float(leaves[0].get('Volume','1')), 'pitch':1.0})
        cues[clip['source']] = sorted(entries, key=lambda v:v['timeMs'])
    all_waves = {}
    for bank in a.banks.glob('*.bars'):
        all_waves.update(waves(bank))
    a.output.mkdir(parents=True, exist_ok=True)
    a.work.mkdir(parents=True, exist_ok=True)
    for filename in sorted({name for entries in cues.values() for cue in entries
                            for name in cue.get('alternatives',[cue['file']])}):
        name = Path(filename).stem
        if name not in all_waves:
            raise ValueError('Missing native sound '+name)
        source = a.work/(name+'.bfwav')
        source.write_bytes(all_waves[name])
        subprocess.run([str(a.decoder), '-i', '-o', str(a.output/filename), str(source)],
                       check=True, stdout=subprocess.DEVNULL)
    (a.output/'cues.json').write_text(json.dumps({'version':1, 'clips':cues}, indent=2)+'\n')
    print('Exported', sum(map(len,cues.values())), 'native timed sound cues')


if __name__ == '__main__':
    main()
