#!/usr/bin/env python3
"""icon_map.tsv: ribbon label / tooltip / FT function -> icon file names (small, large) from ui_ribbon.json
(tools/ribbon_extract.py). Names only; the images come from the user's own icon pack (tools/extract_icons.py).
Usage: gen_icon_map.py [UI_RIBBON.json] [OUT.tsv]   (defaults formats/ui_ribbon.json, viewer/ui/icon_map.tsv)"""
import json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from debrand import debrand
HERE = os.path.dirname(os.path.abspath(__file__))
d = json.load(open(sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, '..', 'formats', 'ui_ribbon.json')))
OUT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, '..', 'viewer', 'ui', 'icon_map.tsv')
rows = {}
def add(key, s, l):
    if key and (s or l) and key not in rows: rows[key] = (s or '', l or '')
def walk(x):
    if isinstance(x, dict):
        s, l = x.get('icon_small'), x.get('icon_large')
        if s or l:
            for k in (x.get('text'), x.get('tooltip'), (x.get('ft') or {}).get('function')): add(k, s, l)
        for v in x.values(): walk(v)
    elif isinstance(x, list):
        for v in x: walk(v)
def gal(x):                                   # gallery items first: their own (64 x 32) images win over buttons
    if isinstance(x, dict):
        for gn in dict.fromkeys((x.get('text'), x.get('tooltip'))):    # key "gallery|item": names repeat across galleries
            if not gn: continue
            seen = {}
            for it in (x.get('gallery') or {}).get('items', []):
                name = it.get('name', '')
                k = gn + '|' + name
                seen[name] = seen.get(name, 0) + 1                    # names repeat WITHIN a gallery too (2D Mill Drill x 2,
                if seen[name] > 1: k += '#%d' % seen[name]            # 3D Mill Project x 2, lathe Rough / Finish / Groove)
                add(k, it.get('icon'), it.get('icon'))
        for v in x.values(): gal(v)
    elif isinstance(x, list):
        for v in x: gal(v)
gal(d)
walk(d)
with open(OUT, 'w') as f:
    for k in sorted(rows):
        if debrand(k) is None: continue           # product name out
        f.write('%s\t%s\t%s\n' % (k, *rows[k]))
print(len(rows))
