#!/usr/bin/env python3
"""gallery_tips.tsv: gallery|item -> the item's description (the palette tooltip of the original ribbon),
from ui_ribbon.json (tools/ribbon_extract.py). Written into the user's config dir by setup_from_install.py
(like icons.pack): the texts are Mastercam's, so nothing is shipped - the file is rebuilt from the user's
own install / the research JSON. Repeated item names within one gallery get a #2 / #3 suffix (same keying
as gen_icon_map.py). Usage: gen_gallery_tips.py [UI_RIBBON.json] [OUT.tsv]"""
import json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from debrand import debrand
HERE = os.path.dirname(os.path.abspath(__file__))
d = json.load(open(sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, '..', 'formats', 'ui_ribbon.json')))
OUT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, '..', 'viewer', 'ui', 'gallery_tips.tsv')

rows = []
def gal(x):
    if isinstance(x, dict):
        g = x.get('gallery')
        if g:
            for gn in dict.fromkeys((x.get('text'), x.get('tooltip'))):
                if not gn: continue
                seen = {}
                for it in g.get('items', []):
                    name = it.get('name', '')
                    seen[name] = seen.get(name, 0) + 1
                    k = gn + '|' + name + ('#%d' % seen[name] if seen[name] > 1 else '')
                    desc = (it.get('description') or '').replace('\t', ' ').replace('\r', ' ').replace('\n', ' ').strip()
                    if desc:
                        b = debrand(desc)
                        if b is not None: rows.append((k, b))
        for v in x.values(): gal(v)
    elif isinstance(x, list):
        for v in x: gal(v)
gal(d)
with open(OUT, 'w') as f:
    for k, v in rows: f.write('%s\t%s\n' % (k, v))
print(len(rows))
