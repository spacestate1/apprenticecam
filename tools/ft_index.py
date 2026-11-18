#!/usr/bin/env python3
"""Index Mastercam function-table (.ft) files: UI command -> DLL + entry point + resources.

Usage: ft_index.py DIR OUT.tsv
Columns: ft_file, application, category, fname, dll, entry, kind, res_dll, spng, lpng, tip, exported
`exported` is checked against the DLL's export names (mangled or plain) when the DLL is present in DIR.
"""
import os, re, struct, subprocess, sys

def exports(path):
    try:
        out = subprocess.run(['objdump', '-p', path], capture_output=True, text=True).stdout
    except Exception:
        return set()
    names = set()
    for m in re.finditer(r'^\s*\[\s*\d+\] \+base\[\s*\d+\]\s+\S+\s+(\S+)', out, re.M):
        n = m.group(1)
        names.add(n)
        mm = re.match(r'\?([^@]+)@', n)      # mangled: ?name@@...
        if mm:
            names.add(mm.group(1))
    return names

def parse(path):
    rows, app, fdll, rdll = [], '', '', ''
    cur = None
    for raw in open(path, encoding='latin-1'):
        line = raw.split('#', 1)[0].strip()
        if not line:
            continue
        tok = line.split(None, 1)
        key, val = tok[0].upper(), (tok[1].strip().strip('"') if len(tok) > 1 else '')
        if key == 'APPLICATION':
            app = val
        elif key == 'FUNC_DLL' and cur is None:
            fdll = val
        elif key == 'RES_DLL' and cur is None:
            rdll = val
        elif key == 'FUNCTION':
            kind, _, name = val.partition(' ')
            cur = {'kind': kind, 'entry': name.strip().strip('"'), 'dll': fdll, 'res_dll': rdll,
                   'fcat': '', 'fname': '', 'spng': '', 'lpng': '', 'tip': ''}
        elif key == 'END_FUNCTION' and cur is not None:
            cur['app'] = app; rows.append(cur); cur = None
        elif cur is not None:
            k = key.lower()
            if k in ('fcat', 'fname', 'spng', 'lpng', 'tip'):
                cur[k] = val
            elif k == 'res_dll':
                cur['res_dll'] = val
            elif k == 'func_dll':
                cur['dll'] = val
    return rows

def main():
    d, out = sys.argv[1], sys.argv[2]
    files = sorted(f for f in os.listdir(d) if re.search(r'\.ft\d*$', f, re.I))
    lower = {f.lower(): f for f in os.listdir(d)}
    cache = {}
    n = 0
    with open(out, 'w') as o:
        o.write('ft_file\tapplication\tcategory\tfname\tdll\tentry\tkind\tres_dll\tspng\tlpng\ttip\texported\n')
        for f in files:
            for r in parse(os.path.join(d, f)):
                dll = r['dll'].lower()
                if dll not in cache:
                    cache[dll] = exports(os.path.join(d, lower[dll])) if dll in lower else None
                ex = cache[dll]
                exported = '' if ex is None else ('yes' if r['entry'] in ex else 'NO')
                o.write('\t'.join([f, r['app'], r['fcat'], r['fname'], r['dll'], r['entry'], r['kind'], r['res_dll'],
                                   r['spng'], r['lpng'], r['tip'], exported]) + '\n')
                n += 1
    print(f'{n} UI functions from {len(files)} .ft files -> {out}')

if __name__ == '__main__':
    main()
