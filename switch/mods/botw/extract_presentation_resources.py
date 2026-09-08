"""Extract only the audio banks/database needed for local presentation research."""
import argparse
from pathlib import Path
import oead

p = argparse.ArgumentParser()
p.add_argument('--bootup', type=Path, required=True)
p.add_argument('--title', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
for pack, names in ((a.bootup, ('SLink2/SLink2DB.sbslnk', 'Sound/Resource/M_UI.bars')),
                    (a.title, ('Sound/Resource/PlayerVoice.bars', 'Sound/Resource/M_SceneStatic.bars'))):
    archive = oead.Sarc(pack.read_bytes())
    for name in names:
        data = bytes(archive.get_file(name).data)
        if data[:4] == b'Yaz0':
            data = bytes(oead.yaz0.decompress(data))
        target = a.output / Path(name).name
        target.write_bytes(data)
        print(target, len(data))
