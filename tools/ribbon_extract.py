#!/usr/bin/env python3
"""Extract Mastercam 2017's ribbon (BCGP_RIBBON_XML in ribbonuires.dll) into JSON + individual icon PNGs.

Usage: ribbon_extract.py RIBBONUIRES_DIR OUT.json ICON_DIR [UI_COMMANDS.tsv]
  RIBBONUIRES_DIR = output of `pe_resources.py ribbonuires.dll --extract DIR`
JSON: {modes, qat, tabs: [{name, keys, context?, groups: [{name, keys, elements: [...]}]}], backstage, file_tab}
element: {kind, id, text, tooltip, description, keys, ft: {app, function, dll, entry}, icon_small, icon_large,
          children: [...], gallery: {icon_size, columns, items: [{name, description, keys, icon}], groups: [...]}}
Icons are written as ICON_DIR/<strip>_<index>.png; the JSON stores the file name.
"""
import csv, json, os, re, sys
import xml.etree.ElementTree as ET
from PIL import Image

def load_xml(path):
    t = open(path, encoding='utf-8-sig').read()
    t = re.sub(r'&#([0-9A-Fa-f]{2});', lambda m: '&#x%s;' % m.group(1), t)   # BCG writes hex refs without 'x'
    return ET.fromstring(t)

class Icons:
    def __init__(self, png_dir, out_dir):
        self.png_dir, self.out_dir, self.cache = png_dir, out_dir, {}
        os.makedirs(out_dir, exist_ok=True)
    def get(self, strip, index, w=None, h=None):
        if strip is None or index is None or index < 0:
            return None
        p = os.path.join(self.png_dir, strip)
        if not os.path.exists(p):
            return None
        if strip not in self.cache:
            self.cache[strip] = Image.open(p).convert('RGBA')
        im = self.cache[strip]
        h = h or im.height
        w = w or h
        if (index + 1) * w > im.width:
            return None
        name = f'{strip}_{index}.png'
        out = os.path.join(self.out_dir, name)
        if not os.path.exists(out):
            im.crop((index * w, 0, (index + 1) * w, h)).save(out)
        return name

def txt(e, tag):
    v = e.findtext(tag)
    return v.strip() if v else None

def main():
    rdir, out_json, icon_dir = sys.argv[1], sys.argv[2], sys.argv[3]
    cmds = {}
    if len(sys.argv) > 4:
        for r in csv.DictReader(open(sys.argv[4]), delimiter='\t'):
            cmds.setdefault((r['application'], r['fname']), (r['dll'], r['entry']))
            cmds.setdefault((r['application'], r['entry']), (r['dll'], r['entry']))
    root = load_xml(os.path.join(rdir, 'BCGP_RIBBON_XML', 'IDR_BCGP_RIBBON_XML'))
    icons = Icons(os.path.join(rdir, 'PNG'), icon_dir)
    rb = root.find('RIBBON_BAR')
    stats = {'elements': 0, 'icons': 0, 'ft_linked': 0}

    def element(e, small, large):
        stats['elements'] += 1
        d = {'kind': txt(e, 'ELEMENT_NAME')}
        idn = e.find('ID')
        if idn is not None:
            d['id'] = txt(idn, 'VALUE') or txt(idn, 'NAME')
            if idn.findtext('NAME') and not idn.findtext('NAME').isdigit():
                d['id_name'] = txt(idn, 'NAME')
            app, fn = txt(idn, 'FT_APPLICATION'), txt(idn, 'FT_FUNCTION')
            if fn:
                d['ft'] = {'app': app, 'function': fn}
                hit = cmds.get((app, fn))
                if hit:
                    d['ft']['dll'], d['ft']['entry'] = hit
                    stats['ft_linked'] += 1
        for k in ('TEXT', 'TOOLTIP', 'DESCRIPTION', 'KEYS'):
            v = txt(e, k)
            if v:
                d[k.lower()] = v
        si, li = e.findtext('INDEX_SMALL'), e.findtext('INDEX_LARGE')
        if si is not None:
            d['icon_small'] = icons.get(small, int(si)); stats['icons'] += bool(d['icon_small'])
        if li is not None:
            d['icon_large'] = icons.get(large, int(li)); stats['icons'] += bool(d['icon_large'])
        if d['kind'] == 'Button_Palette':
            img = e.findtext('IMAGE/NAME')
            sz = [int(x) for x in (e.findtext('SIZE_ICON') or '32, 32').split(',')]
            tips = (e.findtext('TOOLTIPS') or '').split('\t')
            keys = (e.findtext('ICONKEYS') or '').split('\t')
            items = []
            for i, tp in enumerate(tips):
                if not tp:
                    continue
                name, _, desc = tp.partition('\r')
                items.append({'name': name, 'description': desc, 'keys': keys[i] if i < len(keys) else None,
                              'icon': icons.get(img, i, sz[0], sz[1])})
            groups = []
            for g in e.iter('GROUP'):
                groups.append({k.tag.lower(): (k.text or '').strip() for k in g if len(k) == 0})
            d['gallery'] = {'icon_size': sz, 'columns': int(e.findtext('ICONS_IN_ROW') or 4), 'items': items,
                            'groups': groups}
        sub = e.find('ELEMENTS')
        if sub is not None:
            d['children'] = [element(c, small, large) for c in sub.findall('ELEMENT')]
        return d

    def category(cat):
        small = cat.findtext('IMAGE_SMALL/NAME'); large = cat.findtext('IMAGE_LARGE/NAME')
        tab = {'name': txt(cat, 'NAME'), 'keys': txt(cat, 'KEYS'), 'groups': []}
        modes = [m.text for m in cat.iter('MODE')]
        if modes:
            tab['modes'] = modes
        panels = cat.find('PANELS')
        for p in (panels.findall('PANEL') if panels is not None else []):
            g = {'name': txt(p, 'NAME'), 'keys': txt(p, 'KEYS'), 'elements': []}
            els = p.find('ELEMENTS')
            for e in (els.findall('ELEMENT') if els is not None else []):
                g['elements'].append(element(e, small, large))
            tab['groups'].append(g)
        els = cat.find('ELEMENTS')
        if els is not None and not tab['groups']:
            tab['elements'] = [element(e, small, large) for e in els.findall('ELEMENT')]
        return tab

    out = {'modes': [m.text for m in root.iter('MODE')][:5]}
    out['file_tab'] = category(rb.find('CATEGORY_MAIN'))
    bs = rb.find('CATEGORY_BACKSTAGE')
    if bs is not None:
        out['backstage'] = category(bs)
    q = rb.find('QAT_ELEMENTS')
    if q is not None:
        out['qat'] = [element(e, 'IDR_BCGP_IMAGE_RIBBON_BAR', None) for e in q.iter('ELEMENT')]
    out['tabs'] = [category(c) for c in rb.find('CATEGORIES').findall('CATEGORY')]
    for ctx in rb.iter('CONTEXT'):
        cname = txt(ctx, 'TEXT') or txt(ctx, 'NAME')
        for c in ctx.iter('CATEGORY'):
            t = category(c); t['context'] = cname
            out['tabs'].append(t)
    json.dump(out, open(out_json, 'w'), indent=1)
    print(f"{len(out['tabs'])} tabs, {stats['elements']} elements, {stats['icons']} icons, "
          f"{stats['ft_linked']} commands linked to DLL entry points -> {out_json}")

if __name__ == '__main__':
    main()
