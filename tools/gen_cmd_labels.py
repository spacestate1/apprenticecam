#!/usr/bin/env python3
"""cmd_labels.tsv: Mastercam command entry -> menu text, read by the app at run time (never committed: the texts are
Mastercam's). Source: mastercam.ft rows (ui_commands.tsv from tools/ft_index.py, application Mastercam) and the tip
string (RT_STRING id) in the command's resource DLL; the menu text is the line after the first newline (BCG command
string 'tooltip\\nmenu text'), '&' mnemonics removed.
Usage: gen_cmd_labels.py FILESDIR [UI_COMMANDS.tsv] [OUT.tsv]   (defaults formats/ui_commands.tsv, viewer/ui/cmd_labels.tsv)"""
import csv, os, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from debrand import debrand
files = sys.argv[1]
HERE = os.path.dirname(os.path.abspath(__file__))
CMDS = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, '..', 'formats', 'ui_commands.tsv')
OUT = sys.argv[3] if len(sys.argv) > 3 else os.path.join(HERE, '..', 'viewer', 'ui', 'cmd_labels.tsv')
idx = {}
for root, _, fs in os.walk(files):
    for f in fs:
        idx.setdefault(f.lower(), os.path.join(root, f))
cache = {}
def strings(dll):
    p = idx.get(os.path.basename(dll.replace('\\', '/')).lower())
    if not p: return {}
    if p not in cache:
        out = subprocess.run([sys.executable, os.path.join(os.path.dirname(__file__), 'pe_strings.py'), p], capture_output=True, text=True).stdout
        t = {}
        for line in out.splitlines():
            if '\t' in line:
                k, v = line.split('\t', 1); t[k] = v
        cache[p] = t
    return cache[p]
labels = {}
for r in csv.DictReader(open(CMDS), delimiter='\t'):
    if r['application'] != 'Mastercam' or not r['entry'] or r['entry'] in labels: continue
    s = strings(r['res_dll']).get(r['tip'], '') if r['tip'] else ''
    s = s.replace('\\n', '\n')
    text = s.split('\n')[1] if '\n' in s else ''
    labels[r['entry']] = (text.replace('&', '') or r['fname']).strip()
with open(OUT, 'w') as f:                     # entry TAB menu text
    for k in sorted(labels):
        if debrand(k) is None or debrand(labels[k]) is None: continue   # product name out
        f.write('%s\t%s\n' % (k, labels[k].replace('\t', ' ')))
for k in ('OnScreenZoom', 'OnScreenUnzoom', 'OnViewDynamicRotation', 'OnScreenFit', 'OnViewTop', 'delete_entities', 'AnalyzeBtwnPtStart', 'AnalyzeStart', 'OnViewBack'):
    print(k, repr(labels.get(k)))
