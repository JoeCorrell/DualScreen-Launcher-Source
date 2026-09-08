"""Export native pause-player item presentation selection (BOTW CD57B23F).

The ordered tag checks and CookResult fallback are from main+D4279C..D42A58.
Only presentation metadata is exported; ownership/consumption remain game-owned.
"""
import argparse
import json
from pathlib import Path
import oead


def selection(actor):
    if str(actor['name']) == 'Item_Cook_O_02':
        return 'EatTooHard'
    tags = {int(value) for value in actor['tags'].values()} if 'tags' in actor else set()
    for tag, result in ((0xE1DDE8D1, 'UseFairy'), (0xD75DA434, 'Drink'),
                        (0xB1A90330, 'EatBizarre'), (0x0A98FABB, 'EatCookingSkewer'),
                        (0xBC852122, 'EatCookingDish')):
        if tag in tags:
            return result
    return 'EatCookingDish' if 'profile' in actor and str(actor['profile']) == 'CookResult' else 'EatFood'


def main():
    p = argparse.ArgumentParser()
    p.add_argument('actor_info', type=Path)
    p.add_argument('output', type=Path)
    a = p.parse_args()
    data = a.actor_info.read_bytes()
    doc = oead.byml.from_binary(oead.yaz0.decompress(data) if data[:4] == b'Yaz0' else data)
    actors = {str(actor['name']): selection(actor) for actor in doc['Actors']
              if str(actor['name']).startswith(('Item_', 'Animal_Insect_'))}
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(json.dumps({'version': 1, 'source': 'main+D4279C..D42A58',
                                    'actors': dict(sorted(actors.items()))}, indent=2)+'\n')
    print(f'Exported {len(actors)} native item presentation mappings')


if __name__ == '__main__':
    main()
