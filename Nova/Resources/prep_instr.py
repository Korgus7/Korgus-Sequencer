# Nastroje pre Korgus Inspiria: vyber vzoriek, orezanie, slucky, ladenie, balik KNIN
import glob, re, os, sys, struct, subprocess, json
import numpy as np, soundfile as sf

S = '/home/claude/smp'
NN = {'c':0,'c#':1,'db':1,'d':2,'d#':3,'eb':3,'e':4,'f':5,'f#':6,'gb':6,'g':7,'g#':8,'ab':8,'a':9,'a#':10,'bb':10,'b':11}
def midi(name):
    m = re.match(r'([A-Ga-g][#b]?)(-?\d)$', name)
    return 12 * (int(m.group(2)) + 1) + NN[m.group(1).lower()]

OCT_BB = -1; OCT_MB = 0   # nazvy Black And Blue Basses su o oktavu vyssie

# --- jednoduchy zosilnovac a reprobox pre gitary nahrane priamo z snimaca (bez zosilnovaca znia nerealne)
def _bq(kind, f, sr, q=0.707, db=0.0):
    A = 10 ** (db / 40); w = 2 * np.pi * f / sr; al = np.sin(w) / (2 * q); c = np.cos(w)
    if kind == 'lp': b = [(1 - c) / 2, 1 - c, (1 - c) / 2]; a = [1 + al, -2 * c, 1 - al]
    elif kind == 'hp': b = [(1 + c) / 2, -(1 + c), (1 + c) / 2]; a = [1 + al, -2 * c, 1 - al]
    else: b = [1 + al * A, -2 * c, 1 - al * A]; a = [1 + al / A, -2 * c, 1 - al / A]
    return np.array(b) / a[0], np.array(a) / a[0]
def amp(x, sr, kind):
    from scipy.signal import lfilter
    def f(y, *a): b, aa = _bq(*a[:2], sr, *a[2:]); return lfilter(b, aa, y, axis=0)
    pk = np.abs(x).max() + 1e-9; y = x / pk
    if kind == 'jazz':    # teply jazzovy zosilnovac, snimac pri krku
        y = f(y, 'hp', 70); y = f(y, 'pk', 220, 0.9, 3.5); y = f(y, 'pk', 1100, 1.0, -2.5)
        g = 1.4; y = np.tanh(g * y) / np.tanh(g)
        y = f(y, 'lp', 3400, 0.6); y = f(y, 'lp', 4200, 0.7); y = f(y, 'pk', 110, 0.8, 1.5)
    else:                 # cisty zosilnovac (Fender), reprobox 1x12
        y = f(y, 'hp', 85); y = f(y, 'pk', 110, 0.8, 2.0); y = f(y, 'pk', 480, 0.8, -3.5); y = f(y, 'pk', 2400, 1.0, 3.0)
        g = 1.6; y = np.tanh(g * y) / np.tanh(g)
        y = f(y, 'lp', 5200, 0.6); y = f(y, 'lp', 6500, 0.7); y = f(y, 'pk', 3200, 1.5, 1.5)
    return (y * pk).astype(np.float32)

# id, nazov, zdroje [(glob, regex so skupinami note a vel)], poradie sil uderu, drzany ton, max. dlzka (s), oktavovy posun nazvov
I = [
 dict(id=1, name='Pianino', src=[('vcsl/Chordophones/Zithers/Upright Piano, Yamaha/Sustains/*.wav', r'_Sus_([A-G]#?-?\d)_vl(\d)_rr1')], vel=['1','2','3'], sus=False, maxlen=7, oct=1),
 dict(id=2, name='Rhodes', src=[('jlearman.jRhodes3d/jRhodes3d-mono/*.flac', r'__([A-G][b#]?\d)_(\d)')], vel=['5','4','3','2','1'], sus=False, maxlen=6.5, lf=[0.4,0.55,0.7,0.85,1.0]),
 dict(id=3, name='Cembalo', src=[('vcsl/Chordophones/Zithers/Harpsichord, French/Sustains/*.wav', r'_Normal_([A-G]#?\d)_rr1_')], vel=[None], sus=False, maxlen=6, oct=1),
 dict(id=4, name='Akusticka gitara', src=[('martin/*.wav', r'MartinGM2_\d{3}_+([A-G]b?\d)_1\.wav')], vel=[None], sus=False, maxlen=6, soft=1, lf=[0.5, 1.0]),
 dict(id=5, name='Jazzova gitara', src=[('karoryfer.shinyguitar/Samples/electric/*.wav', r'/([a-g]b?\d)_vl(\d)_rr1_2')], vel=['1','2','3','4'], sus=False, maxlen=6, amp='jazz', mic=0.25),
 dict(id=6, name='Basgitara', src=[('bbbass/Samples/darkblack/reg/*.wav', r'darkblack_([a-g]b?\d)_(p|mp|mf|f)_rr1')], vel=['p','mp','mf','f'], sus=False, maxlen=5, oct=OCT_BB),
 dict(id=7, name='Kontrabas', src=[('meatbass/Samples/pizz/*.wav', r'/([a-g]b?\d)_vl(\d)_rr1')], vel=['1','2','3','4'], sus=False, maxlen=4, oct=OCT_MB),
 dict(id=8, name='Slacikova sekcia', src=[('VSCO-2-CE/Strings/Cello Section/susvib/*.wav', r'susvib_([A-G]#?\d)_v(\d)_1', 0, 55),
                                          ('VSCO-2-CE/Strings/Viola Section/susvib/*.wav', r'_susvib_([A-G]#?\d)_v(\d)_1', 56, 64),
                                          ('VSCO-2-CE/Strings/Violin Section/susVib/*.wav', r'_susVib_([A-G]#?\d)_v(\d)', 65, 127)], vel=['1','2|3'], sus=True, maxlen=5, oct=1),
 dict(id=9, name='Husle', src=[('VSCO-2-CE/Strings/Solo Violin/Arco Vib/*.wav', r'_ArcoVib_([A-G]#?\d)_([pf])')], vel=['p','f'], sus=True, maxlen=5),
 dict(id=10, name='Violoncela', src=[('VSCO-2-CE/Strings/Cello Section/susvib/*.wav', r'susvib_([A-G]#?\d)_v(\d)_1')], vel=['1','3'], sus=True, maxlen=5, oct=1),
 dict(id=11, name='Trubka s dusitkom', src=[('VSCO-2-CE/Brass/Trumpet/harmonM-sus/*.wav', r'-sus_([A-G]#?\d)_v(\d)_rr1')], vel=['1','3'], sus=True, maxlen=4, oct=1),
 dict(id=12, name='Trubka', src=[('VSCO-2-CE/Brass/Trumpet/susvib/*.wav', r'_susvib_([A-G]#?\d)_v(\d)_rr1')], vel=['1','2'], sus=True, maxlen=4, oct=1),
 dict(id=13, name='Tenor saxofon', src=[('vcsl/Aerophones/Reed Aerophones/Tenor Saxophone/Vibrato/*.wav', r'_Vib_Main_([A-G]#?\d)_var')], vel=[None], sus=True, maxlen=4, oct=1),
 dict(id=14, name='Trombon', src=[('VSCO-2-CE/Brass/Tenor Trombone/sus/*.wav', r'_sus_([A-G]#?\d)_v(\d)_1')], vel=['1','2'], sus=True, maxlen=4, oct=1),
 dict(id=15, name='Flauta', src=[('VSCO-2-CE/Woodwinds/Flute/susvib/*.wav', r'_susvib_([A-G]#?\d)_v(\d)_1')], vel=['1'], sus=True, maxlen=4, oct=1),
 dict(id=16, name='Klarinet', src=[('VSCO-2-CE/Woodwinds/Clarinet/susLong/*.wav', r'_susLong_([A-G]#?\d)_v(\d)_rr1')], vel=['1','2','3'], sus=True, maxlen=4, oct=1),
 dict(id=17, name='Vibrafon', src=[('vcsl/Idiophones/Struck Idiophones/Vibraphone/Soft Mallets/*.wav', r'_soft_([A-G]#?\d)_v(\d)_rr1')], vel=['1','2'], sus=False, maxlen=7, oct=1),
 dict(id=18, name='Marimba', src=[('vcsl/Idiophones/Struck Idiophones/Marimba/*.wav', r'_Outrigger_([A-G]#?\d)_(soft|med|loud)_01')], vel=['soft','med','loud'], sus=False, maxlen=3, oct=1),
 dict(id=19, name='Zvonkohra', src=[('vcsl/Idiophones/Struck Idiophones/Glockenspiel/*.wav', r'glock_(soft|med|loud)_([A-G]#?\d)_01', 'swap')], vel=['soft','med','loud'], sus=False, maxlen=4, oct=1),
]

I += [
 dict(id=20, name='Pizzicato slaciky', src=[('VSCO-2-CE/Strings/Cello Section/pizzT/*.wav', r'pizzT_([A-G]#?\d)_v(\d)_RR1', 0, 55),
                                           ('VSCO-2-CE/Strings/Viola Section/pizz/*.wav', r'_pizz_([A-G]#?\d)_v(\d)_rr1', 56, 64),
                                           ('VSCO-2-CE/Strings/Violin Section/Pizz/*.wav', r'_Pizz_([A-G]#?\d)_v(\d)_rr1', 65, 127)], vel=['1','2|3'], sus=False, maxlen=2.5, oct=1),
 dict(id=21, name='Harmonika', src=[('vcsl/Aerophones/Free Aerophones/Harmonica-Hohner-Super64/Sustains/Normal/*.wav', r'_Normal _([A-G]#?\d)\.wav')], vel=[None], sus=True, maxlen=4, oct=1),
 dict(id=22, name='Harfa', src=[('vcsl/Chordophones/Composite Chordophones/Concert Harp/*.wav', r'KSHarp_([A-G]#?\d)_(p|mp|mf|f|ff)1')], vel=['p','mp','mf','f','ff'], sus=False, maxlen=5),
 dict(id=23, name='Kostolny organ', src=[('VSCO-2-CE/Keys/Organ/Loud/*.wav', 'DETECT')], vel=[None], sus=True, maxlen=5),
 dict(id=24, name='Lesny roh', src=[('VSCO-2-CE/Brass/F Horn/sus/*.wav', r'_sus_([A-G]#?\d)_v(\d)_1')], vel=['1','2','3'], sus=True, maxlen=4, oct=1),
 dict(id=25, name='Hoboj', src=[('VSCO-2-CE/Woodwinds/Oboe/Vib/*.wav', r'_Vib_([A-G]#?\d)_v(\d)_Main')], vel=['1','3'], sus=True, maxlen=4, oct=1),
 dict(id=26, name='Fagot', src=[('VSCO-2-CE/Woodwinds/Bassoon/vib/*.wav', r'PSBassoon_([A-G]#?\d)_v(\d)_1')], vel=['1','2'], sus=True, maxlen=4, oct=1),
 dict(id=27, name='Tuba', src=[('VSCO-2-CE/Brass/Tuba/sus/*.wav', r'_sus_([A-G]#?\d)_v(\d)_rr1')], vel=['1','2','3'], sus=True, maxlen=4, oct=1),
 dict(id=28, name='Slaciky tremolo', src=[('VSCO-2-CE/Strings/Cello Section/trem/*.wav', r'trem_([A-G]#?\d)_v(\d)_1', 0, 55),
                                          ('VSCO-2-CE/Strings/Viola Section/trem/*.wav', r'_trem_([A-G]#?\d)_v(\d)_rr1', 56, 64),
                                          ('VSCO-2-CE/Strings/Violin Section/Trem/*.wav', r'_Trem_([A-G]#?\d)_v(\d)', 65, 127)], vel=['1','2|3'], sus=True, maxlen=5, oct=1),
 dict(id=29, name='Slaciky staccato', src=[('VSCO-2-CE/Strings/Cello Section/spic/*.wav', r'spic_([A-G]#?\d)_v(\d)_RR1', 0, 55),
                                           ('VSCO-2-CE/Strings/Viola Section/spic/*.wav', r'_spic_([A-G]#?\d)_v(\d)_rr1', 56, 64),
                                           ('VSCO-2-CE/Strings/Violin Section/Spic/*.wav', r'_Spic_([A-G]#?\d)_v(\d)_rr1', 65, 127)], vel=['1','2|3'], sus=False, maxlen=1.5, oct=1),
 dict(id=30, name='Kalimba', src=[('vcsl/Idiophones/Plucked Idiophones/Kalimba, Tanzania/*.wav', r'_Main_([A-G]#?\d)_k')], vel=[None], sus=False, maxlen=4, oct=1, notune=1),
 dict(id=31, name='Ruerkove zvony', src=[('vcsl/Idiophones/Struck Idiophones/Tubular Bells 2/*.wav', r'TB_hit_([A-G]#?\d)_v(\d)_1')], vel=['1|2','3|4'], sus=False, maxlen=8, oct=1, notune=1),
 dict(id=32, name='Xylofon', src=[('vcsl/Idiophones/Struck Idiophones/Xylophone/Medium Mallets/*.wav', r'Xylo_Medium_([A-G]#?\d)_(pp|p|mf|f|ff)_01')], vel=['pp','p','mf','f','ff'], sus=False, maxlen=2, oct=1, notune=1),
 dict(id=33, name='Elektricka gitara', src=[('karoryfer.emilyguitar/notes/*.wav', r'/([a-g]b?\d)_(p|mp|mf|f)_rr1')], vel=['p','mp','mf','f'], sus=False, maxlen=6, amp='clean'),
 dict(id=34, name='Okarina', src=[('vcsl/Aerophones/Edge-blown Aerophones/Ocarina, Typical/Sustains/Sus/*.wav', r'_Sus_([A-G]#?\d)\.wav')], vel=[None], sus=True, maxlen=4, oct=1),
 dict(id=35, name='Zobcova flauta', src=[('vcsl/Aerophones/Edge-blown Aerophones/Baroque Alto Recorder/Sustain/*.wav', r'_Sus_([A-G]#?\d)_rr1')], vel=[None], sus=True, maxlen=4, oct=1),
]

# --- citanie jednoduchych SFZ (FreePats): polozky (nota, vrstva, subor, centy, klavesy pod, klavesy nad)
def _note(v):
    v = v.strip()
    if re.match(r'^-?\d+$', v): return int(v)
    return midi(v.lower())
def sfz_items(path, layers=None):
    base = os.path.dirname(path); txt = open(path, encoding='utf-8', errors='replace').read()
    txt = re.sub(r'//[^\n]*', '', txt)
    scopes = {'global': {}, 'master': {}, 'group': {}, 'control': {}}
    cur = None; regs = []
    for tok in re.split(r'(<[a-z]+>)', txt):
        m = re.match(r'<([a-z]+)>', tok)
        if m:
            h = m.group(1)
            if h in ('global', 'master', 'group', 'control'):
                scopes[h] = {}; cur = scopes[h]
                if h == 'global': scopes['master'] = {}; scopes['group'] = {}
                if h == 'master': scopes['group'] = {}
            elif h == 'region':
                cur = {}; regs.append((dict(scopes['global'], **scopes['master'], **scopes['group']), cur))
            continue
        if cur is None: continue
        for k, v in re.findall(r'([a-z_0-9]+)=(.*?)(?=\s+[a-z_0-9]+=|\s*$)', tok.replace('\n', ' ')):
            cur[k] = v.strip()
    out = []
    for inh, r in regs:
        o = dict(inh, **r)
        if 'sample' not in o: continue
        if o.get('seq_position', '1') != '1' or float(o.get('lorand', '0')) > 0.001: continue
        if 'key' in o: lo = hi = kc = _note(o['key'])
        else:
            lo = _note(o.get('lokey', '0')); hi = _note(o.get('hikey', '127')); kc = _note(o.get('pitch_keycenter', str(lo)))
        lv = int(o.get('lovel', '1'))
        f = os.path.join(base, scopes['control'].get('default_path', ''), o['sample'].replace('\\', '/'))
        out.append([kc, lv, f, -int(round(float(o.get('tune', '0')))), max(0, kc - lo), max(0, hi - kc)])
    vels = sorted(set(x[1] for x in out)); vm = {v: i for i, v in enumerate(vels)}
    res = []
    for kc, lv, f, c, b, a in out: res.append((kc, vm[lv], os.path.relpath(f, S), c, b, a))
    return res

# --- nove nastroje (stahuju sa zvlast): VSCO 2 CE, VCSL, ganjo (itsclipping) - vsetko CC0
VC = 'vcsl/Membranophones/Struck Membranophones/'; VI = 'vcsl/Idiophones/Struck Idiophones/'
def K(note, *files, cents=0):   # bici nastroj: nota a subory po vrstvach (od najjemnejsej)
    return [(note, l, f, cents, 0, 0) for l, f in enumerate(files)]
I += [
 dict(id=37, name='Viola', src=[('VSCO-2-CE/Strings/Viola Section/susvib/*.wav', r'_susvib_([A-G]#?\d)_v(\d)_1')], vel=['1','2'], sus=True, maxlen=5, oct=1),
 dict(id=38, name='Pikola', src=[('VSCO-2-CE/Woodwinds/Piccolo/Sus/*.wav', r'piccolo_([A-G]#?\d)_sustain1')], vel=[None], sus=True, maxlen=4, oct=1),
 dict(id=39, name='Banjo', src=[('ganjo/Common/*.wav', r'Banjo_Common - ([A-G]#?\d)\.wav')], vel=[None], sus=False, maxlen=4, oct=-1),
 dict(id=40, name='Tympany', sus=False, maxlen=5, notune=1, items=
      sum([K(n, *[VC + 'Timpani 1/Hit/Timpani%d_Hit_v%d_rr1_Sum.wav' % (d, v) for v in (2, 3, 4)], cents=c)
           for d, n, c in ((1, 49, 5), (2, 54, -47), (3, 56, 30), (4, 59, 0), (5, 61, 32))], [])),
 dict(id=41, name='Bicia suprava', sus=False, maxlen=6, notune=1, kit=1, items=
      K(36, *[VC + 'Bass Drum 1/BDrumNew_hit_v%d_rr1_Sum.wav' % v for v in (2, 5, 7)]) +
      K(37, VC + 'Snare Drum, Modern 1/Snare2_stick_v1_rr1_Mid.wav') +
      K(38, *[VC + 'Snare Drum, Modern 1/Snare2_HitSN_v%d_rr1_Mid.wav' % v for v in (3, 6, 9)]) +
      K(40, *[VC + 'Snare Drum, Modern 1/Snare2_HitNS_v%d_rr1_Mid.wav' % v for v in (2, 4, 6)]) +
      K(42, *[VI + 'Hi-Hat Cymbal/HiHat_HitC_v%d_rr1_Mid.wav' % v for v in (1, 3, 4)]) +
      K(44, VI + 'Hi-Hat Cymbal/HiHat_Close_rr1_Mid.wav') +
      K(46, VI + 'Hi-Hat Cymbal/HiHat_HitO_rr1_Mid.wav') +
      K(49, *[VI + 'Clash Cymbals 1/cymbal_crash1_%s.wav' % v for v in ('mp1', 'mf1', 'ff2')]) +
      K(51, *[VI + 'Suspended Cymbal 1/susCymb1_hit_stick_%s.wav' % v for v in ('pp1', 'mp1', 'f1')]) +
      K(53, *[VI + 'Suspended Cymbal 1/susCymb1_hit_bell_%s.wav' % v for v in ('pp1', 'mf1', 'fff1')]) +
      K(55, VI + 'Clash Cymbals 1/cymbal_crash1_short1.wav')),
 dict(id=42, name='Rucne bubny', sus=False, maxlen=3, notune=1, kit=1, items=
      K(60, *[VC + 'Bongos/BongoH_Hit1_v%d_rr1_Mid.wav' % v for v in (1, 2, 3)]) +
      K(61, *[VC + 'Bongos/BongoL_Hit1_v%d_rr1_Mid.wav' % v for v in (1, 2, 3)]) +
      K(62, *[VC + 'Conga/Quinto_HitFM1_v%d_rr1_Sum.wav' % v for v in (1, 2)]) +
      K(63, *[VC + 'Conga/Quinto_HitN_v%d_rr1_Sum.wav' % v for v in (1, 2, 3)]) +
      K(64, *[VC + 'Conga/Conga_HitN_v%d_rr1_Sum.wav' % v for v in (1, 2, 3)]) +
      K(65, *[VC + 'Conga/Tumba_HitN_v%d_rr1_Sum.wav' % v for v in (1, 2, 4)]) +
      K(66, *[VI + 'Cajon/Cajon_hit1_%s_rr1.wav' % v for v in ('mp', 'f', 'fff')]) +
      K(67, *[VI + 'Cajon/Cajon_hit2_%s_rr1.wav' % v for v in ('pp', 'mp', 'f')]) +
      K(68, *[VI + 'Cajon/Cajon_hit3_%s_rr1.wav' % v for v in ('p', 'mp', 'f')]) +
      K(69, VC + 'Bongos/BongoH_HitMuted1_v2_rr1_Mid.wav') +
      K(70, VC + 'Bongos/BongoL_HitMuted2_v2_rr1_Mid.wav')),
 dict(id=43, name='Perkusie', sus=False, maxlen=8, notune=1, kit=1, items=
      K(54, *[VI + 'Tambourine 1/Tamb1_Hit_v%d_rr1_Mid.wav' % v for v in (1, 2)]) +
      K(56, VI + 'Tambourine 1/Tamb1_Shake_rr1_Mid.wav') +
      K(57, *[VI + 'Gong 1/gong_%s.wav' % v for v in ('p', 'mf', 'f')]) +
      K(59, *[VI + 'Clash Cymbals 1/cymbal_crash1_%s.wav' % v for v in ('pp1', 'mf1', 'ff2')]) +
      K(70, VI + 'Shaker, Small/Mid_Shaker_Slap_rr1.wav') +
      K(72, VI + 'Shaker, Small/Mid_ShakerDouble_Down_rr1.wav') +
      K(75, *[VI + 'Claves/Claves1_Hit_v%d_rr1_Mid.wav' % v for v in (1, 2, 3)]) +
      K(76, *[VI + 'Woodblock/wood_click_%s.wav' % v for v in ('pp_rr1', 'mp', 'ff')]) +
      K(77, VI + 'Woodblock/wood_click3_vl1.wav', VI + 'Woodblock/wood_click3_vl2.wav') +
      K(80, VI + 'Triangles/Triangle1_HitM_v1_rr2_Mid.wav') +
      K(81, *[VI + 'Triangles/Triangle1_Hit_v%d_rr1_Mid.wav' % v for v in (1, 2)])),
]

FP = 'fp/'
def fpsfz(rel): return os.path.join(S, FP, rel)
def _fp_items(rel):
    p = fpsfz(rel)
    return sfz_items(p) if os.path.exists(p) else []
I = [x for x in I if x['id'] not in (40, 42)]   # tympany a rucne bubny z FreePats (lepsie mapovanie)
I += [
 dict(id=40, name='Tympany', sus=False, maxlen=5, notune=1, kit=1, items=_fp_items('Timpani-SFZ+FLAC-20240810/Timpani SFZ+FLAC-20240810/Timpani 20240809.sfz')),
 dict(id=42, name='Rucne bubny', sus=False, maxlen=3, notune=1, kit=1, items=_fp_items('WorldPercussion-SFZ+FLAC-20200905/WorldPercussion SFZ+FLAC-20200905/WorldPercussion 20200905.sfz')),
 dict(id=44, name='Akordeon', sus=True, maxlen=5, notune=1, items=_fp_items('ButtonAccordionHN-SFZ+FLAC-20240329/Button Accordion HN SFZ+FLAC-20240329/PRESET Button Accordion HN tuned.sfz')),
 dict(id=45, name='Ukulele', sus=False, maxlen=4, notune=1, items=_fp_items('Ukulele-SFZ+FLAC-20260811/Ukulele SFZ+FLAC-20260811/Ukulele 20260811.sfz')),
 dict(id=46, name='Gajdy', sus=True, maxlen=5, notune=1, items=_fp_items('Bagpipe-small-SFZ+FLAC-20260806/Bagpipe small SFZ+FLAC-20260806/Bagpipe small 20260806.sfz')),
 dict(id=47, name='Klasicka gitara', sus=False, maxlen=5, notune=1, items=_fp_items('scg/SpanishClassicalGuitar-20190618.sfz')),
]

def _muldjord():
    # MuldjordKit (Lars Muldjord, CC BY 4.0): klavesy podla General MIDI, 6 zo 14 sil uderu
    it = _fp_items('mk/MuldjordKit SFZ+FLAC-20201018/MuldjordKit 20201018.sfz')
    gm = {48: 36, 49: 35, 50: 38, 51: 40, 65: 37, 66: 39, 52: 42, 53: 46, 54: 51, 55: 53, 56: 59, 57: 56,
          58: 49, 59: 57, 60: 52, 61: 50, 62: 48, 63: 45, 64: 41}
    out = []
    for src in gm:
        ls = sorted(set(l for n, l, f, c, b, a in it if n == src))
        if not ls: continue
        k = min(6, len(ls))
        sel = [ls[round(i * (len(ls) - 1) / max(1, k - 1))] for i in range(k)] if k > 1 else ls
        for li, lv in enumerate(sel):
            f = next(f for n, l, f, c, b, a in it if n == src and l == lv)
            out.append((gm[src], li + (6 - k), f, 0, 0, 0))   # vrstvy zarovnane hore (najsilnejsia = 5)
    # pedal hi-hat (44) = zavrety hi-hat
    out += [(44, l, f, 0, 0, 0) for n, l, f, c, b, a in out if n == 42]
    return out
I = [x for x in I if x['id'] != 41]
I.append(dict(id=41, name='Bicia suprava', sus=False, maxlen=5, notune=1, kit=1, items=_muldjord()))

def collect(ins):
    if 'items' in ins:
        res = []
        for n, l, f, *_ in ins['items']:
            ff = os.path.join(S, f)
            if os.path.exists(ff): res.append((n, l, ff))
            else: print('  chyba subor', f)
        return res
    out = []
    for s in ins['src']:
        pat, rx = s[0], s[1]
        lo, hi = (s[2], s[3]) if len(s) >= 4 else (0, 127)
        swap = len(s) == 3 and s[2] == 'swap'
        for f in sorted(glob.glob(os.path.join(S, pat))):
            if rx == 'DETECT':
                x, sr = sf.read(f, dtype='float32', always_2d=True); hz, _ = f0(x.mean(axis=1), sr)
                if hz: out.append((int(round(12 * np.log2(hz / 440.0) + 69)), 0, f))
                continue
            m = re.search(rx, f)
            if not m: continue
            g = m.groups()
            if swap: g = (g[1], g[0])
            note = midi(g[0]) + 12 * ins.get('oct', 0); v = g[1] if len(g) > 1 else None
            layer = None
            for i, t in enumerate(ins['vel']):
                if t is None or (v is not None and v in t.split('|')): layer = i
            if layer is None: continue
            if not (lo <= note <= hi): continue
            out.append((note, layer, f))
    # jedna sila uderu: jemna vrstva vyrobena z tej istej vzorky (tmavsia, makky nabeh)
    if ins.get('soft'): out = [(n, 0, f + '|soft') for n, _, f in out] + [(n, 1, f) for n, _, f in out]
    # jedna vzorka na notu a vrstvu
    seen = {}; res = []
    for n, l, f in out:
        if (n, l) in seen: continue
        seen[(n, l)] = 1; res.append((n, l, f))
    return res

def f0(x, sr):
    # zakladna frekvencia (YIN-like, rozsah 25 Hz .. 2.5 kHz) zo stredu tonu
    seg = x[int(0.25 * len(x)):int(0.25 * len(x)) + min(len(x) // 2, int(0.5 * sr))]
    if len(seg) < 2048: seg = x[:min(len(x), 8192)]
    seg = seg - seg.mean()
    n = len(seg); maxlag = min(n // 2, int(sr / 25)); minlag = int(sr / 2500)
    d = np.zeros(maxlag)
    fx = np.fft.rfft(seg, 2 * n); ac = np.fft.irfft(fx * np.conj(fx))[:maxlag]
    e = np.cumsum(seg ** 2)
    for tau in range(1, maxlag):
        d[tau] = e[n - tau - 1] + (e[-1] - e[tau - 1]) - 2 * ac[tau]
    cm = np.cumsum(d[1:]); cmnd = np.ones(maxlag); cmnd[1:] = d[1:] * np.arange(1, maxlag) / np.maximum(cm, 1e-12)
    cand = np.where(cmnd[minlag:] < 0.15)[0]
    if len(cand) == 0: return None, 1.0
    t = cand[0] + minlag
    while t + 1 < maxlag and cmnd[t + 1] < cmnd[t]: t += 1
    if 1 <= t < maxlag - 1:
        a, b, c = cmnd[t - 1], cmnd[t], cmnd[t + 1]; den = a - 2 * b + c
        tt = t + (0.5 * (a - c) / den if den != 0 else 0)
    else: tt = t
    return sr / tt, cmnd[t]

def analyze():
    rep = {}
    for ins in I:
        items = collect(ins)
        offs = []
        for n, l, f in items:
            x, sr = sf.read(f, dtype='float32', always_2d=True)
            hz, conf = f0(x.mean(axis=1), sr)
            if hz: offs.append(12 * np.log2(hz / 440.0) + 69 - n)
        layers = sorted(set(l for _, l, _ in items))
        notes = sorted(set(n for n, _, _ in items))
        if not items: print(ins['name'], 'EMPTY'); continue
        o = np.array(offs)
        print('%-18s n=%3d layers=%s range=%d-%d  pitchoff med=%.2f (%.0f%% within .5)' % (ins['name'], len(items), layers, notes[0], notes[-1],
              np.median(o) if len(o) else 0, 100 * np.mean(np.abs(o - np.round(np.median(o))) < 0.5) if len(o) else 0))


def env_rms(m, sr, win=0.02):
    w = max(1, int(win * sr)); return np.sqrt(np.convolve(m ** 2, np.ones(w) / w, mode='same'))

def find_loop(x, sr, a, b, maxlen):
    """x: mono, [a,b) ustalena cast. Vrati (s, e) s najlepsou zhodou priebehu okolo s a e."""
    e = min(b, int(maxlen * sr))
    if e - a < int(0.35 * sr): return None
    best = None
    w = int(0.012 * sr)
    Lmin, Lmax = int(0.5 * sr), min(int(1.8 * sr), e - a)
    env = env_rms(x, sr, 0.05)
    tgt = x[e - w:e + w] if e + w <= len(x) else x[e - 2 * w:e]
    for L in range(Lmax, Lmin, -int(0.004 * sr)):
        s0 = e - L
        # jemne hladanie s v okoli (+-2 ms) podla korelacie
        for ds in range(-int(0.003 * sr), int(0.003 * sr), 2):
            s = s0 + ds
            if s - w < a: continue
            c = x[s - w:s + w] if e + w <= len(x) else x[s - 2 * w:s]
            if len(c) != len(tgt): continue
            den = np.sqrt((c * c).sum() * (tgt * tgt).sum()) + 1e-12
            corr = (c * tgt).sum() / den
            lev = abs(np.log((env[s] + 1e-9) / (env[e - 1] + 1e-9)))
            score = corr - 3.0 * lev + 0.15 * (L / Lmax)
            if best is None or score > best[0]: best = (score, s, e, corr)
    return best

# Celesta (pjcohen, Freesound, CC BY 4.0): chromaticky G2-G6, nazvy krizikov chybaju -> tony podla vysky
def _celesta():
    # tony podla skutocnej vysky; hra len vo vlastnom rozsahu (pod nim by najnizsia vzorka znela ako bubon)
    out = {}
    for f in sorted(glob.glob(os.path.join(S, 'celesta/*.wav'))):
        x, sr = sf.read(f, dtype='float32', always_2d=True); hz, _ = f0(x.mean(axis=1), sr)
        if hz: out.setdefault(int(round(12 * np.log2(hz / 440.0) + 69)), os.path.relpath(f, S))
    return [(n, 0, f, 0, 0, 0) for n, f in sorted(out.items())]
I.append(dict(id=48, name='Celesta', sus=False, maxlen=6, kit=1, items=_celesta()))


# SUKROMNE (Pianobook – nesmu sa sirit, nie su v manifeste ani v instalatore): 49 kridlovka, 50 djembe
def _private():
    fl = [(int(os.path.basename(f)[:-4]) - 12, 0, os.path.relpath(f, S), 0, 0, 0) for f in glob.glob(os.path.join(S, 'flugel/*.wav'))]
    dj = [(int(os.path.basename(f)[:-4]), 0, os.path.relpath(f, S), 0, 0, 0) for f in glob.glob(os.path.join(S, 'djembe/*.wav'))]
    return [dict(id=49, name='Kridlovka', sus=True, maxlen=6, items=sorted(fl)),
            dict(id=50, name='Djembe', sus=False, maxlen=3, notune=1, kit=1, items=sorted(dj))]
I.extend(_private())

def process(ins, outdir):
    items = collect(ins)
    lay = sorted(set(l for _, l, _ in items)); lmap = {l: i for i, l in enumerate(lay)}
    recs = []
    for n, l, f in items:
        soft = f.endswith('|soft'); f = f[:-5] if soft else f
        x, sr = sf.read(f, dtype='float32', always_2d=True)
        if x.shape[1] > 2: x = x[:, :2]
        if soft:
            from scipy.signal import lfilter
            for a in (('lp', 1700, sr, 0.6), ('lp', 2600, sr, 0.7)): b, aa = _bq(*a); x = lfilter(b, aa, x, axis=0).astype(np.float32)
            pk0 = int(np.argmax(np.abs(x).max(axis=1) > np.abs(x).max() * 0.05)); r = int(0.006 * sr)
            x[pk0:pk0 + r] *= np.linspace(0.35, 1, r)[:, None]
        if ins.get('amp'):
            x = amp(x, sr, ins['amp'])
            if ins.get('mic'):   # trochu mikrofonu pri gitare (ta ista nahravka)
                fm = f.replace('/electric/', '/acoustic/').replace('_2.wav', '_1.wav')
                if os.path.exists(fm):
                    m, _ = sf.read(fm, dtype='float32', always_2d=True); m = m[:, :x.shape[1]] if m.shape[1] >= x.shape[1] else np.repeat(m[:, :1], x.shape[1], 1)
                    n2 = min(len(m), len(x)); rx = np.sqrt(np.mean(x[:n2] ** 2)) + 1e-9; rm = np.sqrt(np.mean(m[:n2] ** 2)) + 1e-9
                    x = x[:n2] + ins['mic'] * m[:n2] * (rx / rm)
        mono = x.mean(axis=1)
        pk = np.abs(mono).max() + 1e-9
        # ladenie
        cents = 0
        span = 0
        if 'items' in ins:
            it = next((t for t in ins['items'] if t[0] == n and os.path.join(S, t[2]) == f), None)
            if it: cents = it[3]; span = (min(15, it[4]) & 15) | ((min(15, it[5]) & 15) << 4)
        if ins['name'] != 'Zvonkohra' and not ins.get('notune'):
            hz, conf = f0(mono, sr)
            if hz:
                c = 100 * (12 * np.log2(hz / 440.0) + 69 - n)
                if abs(c) < 45 and conf < 0.12: cents = int(round(c))
        # zaciatok
        thr = pk * (10 ** (-50 / 20) if ins['sus'] else 10 ** (-45 / 20))
        st = int(np.argmax(np.abs(mono) > thr))
        st = max(0, st - int((0.012 if ins['sus'] else 0.003) * sr))
        x = x[st:]; mono = mono[st:]
        env = env_rms(mono, sr, 0.03)
        loop = (0, 0)
        if ins['sus']:
            emax = env.max()
            a = int(np.argmax(env > 0.8 * emax)) + int(0.25 * sr)
            med = np.median(env[a:]) if a < len(env) else emax
            above = np.where(env > 0.6 * med)[0]
            b = (above[-1] if len(above) else len(env)) - int(0.15 * sr)
            bl = find_loop(mono, sr, a, b, ins['maxlen'])
            if bl is None:
                a = int(np.argmax(env > 0.5 * emax)) + int(0.15 * sr); b = len(mono) - int(0.1 * sr)
                bl = find_loop(mono, sr, a, b, ins['maxlen'])
            if bl is None:
                print('  no loop', os.path.basename(f), round(len(mono) / sr, 2)); continue
            _, s, e, corr = bl
            X = min(int(0.22 * sr), (e - s) // 3, s - a if s > a else int(0.05 * sr))
            X = max(X, int(0.03 * sr))
            r = np.linspace(0, 1, X)[:, None]
            g1, g2 = np.cos(r * np.pi / 2), np.sin(r * np.pi / 2)
            if corr > 0.7: g1, g2 = 1 - r, r
            y = x[:e].copy()
            y[e - X:e] = x[e - X:e] * g1 + x[s - X:s] * g2
            # za koniec slucky pokracovanie od zaciatku slucky (okraj OGG nebude v slucke)
            tail = x[s:s + int(0.08 * sr)]
            x = np.concatenate([y, tail]); loop = (s, e)
            body = mono[a:e]
        else:
            thr = pk * 10 ** (-62 / 20)
            ab = np.where(env > thr)[0]
            end = min(ab[-1] if len(ab) else len(x), int(ins['maxlen'] * sr), len(x))
            x = x[:end].copy()
            # odsumenie: sum v tichych miestach (doznievanie) stisit - jemna brana podla odhadu sumu
            e2 = env[:end]; nz = e2[e2 > 0]
            if len(nz) > 20:
                nf = np.percentile(nz, 8)
                if nf > pk * 10 ** (-80 / 20):
                    g = np.clip((e2 - 1.5 * nf) / (e2 + 1e-9), 0.06, 1.0) ** 0.7
                    k = max(1, min(int(0.02 * sr), len(g))); g = np.convolve(g, np.ones(k) / k, 'same')[:len(g)]
                    x *= g[:, None]
            fl = min(int(0.35 * sr), len(x) // 3)
            x[-fl:] *= (np.linspace(1, 0, fl) ** 2)[:, None]
            body = mono[:min(len(mono), int(0.4 * sr))]
        rms = np.sqrt(np.mean(body ** 2)) + 1e-9
        recs.append(dict(n=n, l=lmap[l], cents=cents, x=x, sr=sr, loop=loop, rms=rms, f=f, span=span if 'items' in ins else 0))
    # hlasitost: rovnaka po klaviature, medzi vrstvami prirodzeny pomer
    top = max(r['l'] for r in recs)
    byn = {}
    for r in recs: byn.setdefault(r['n'], {})[r['l']] = r['rms']
    lf = {}
    for l in range(top + 1):
        rs = [d[l] / d[top] for d in byn.values() if l in d and top in d]
        lf[l] = float(np.median(rs)) if rs else 1.0
    if 'lf' in ins: lf = {i: v for i, v in enumerate(ins['lf'])}
    if 'items' in ins:   # bicie: kazda nota zvlast, vrstvy si nechaju prirodzeny pomer (najsilnejsia = 1)
        topn = {}
        for r in recs: topn[r['n']] = max(topn.get(r['n'], 0), r['rms'])
        for r in recs: r['x'] = r['x'] / topn[r['n']]
    else:
        for r in recs: r['x'] = r['x'] * (lf[r['l']] / r['rms'])
    pk = max(np.abs(r['x']).max() for r in recs)
    # cielova hlasitost: horna vrstva v strede rozsahu ~ -14 dBFS RMS; vzorky max 0.89 (zvysok zosilni prehravac)
    topr = np.median([np.sqrt(np.mean((r['x'][:, 0] if r['loop'][1] == 0 else r['x'][:r['loop'][1], 0]) ** 2)) for r in recs if r['l'] == top]) + 1e-9
    want = 10 ** (-14 / 20) / topr
    norm = 0.89 / pk
    extra = float(want / norm)
    os.makedirs(outdir, exist_ok=True)
    ents = []; tot = 0
    for r in recs:
        y = r['x'] * norm
        ch = 1 if y.shape[1] == 1 or np.abs(y[:, 0] - y[:, -1]).max() < 1e-4 else 2
        if ch == 1: y = y[:, :1]
        tmp = os.path.join(outdir, 't.wav'); sf.write(tmp, y, r['sr'], subtype='FLOAT')
        og = os.path.join(outdir, '%d_%d_%d.ogg' % (ins['id'], r['n'], r['l']))
        subprocess.run(['ffmpeg', '-y', '-loglevel', 'error', '-i', tmp, '-c:a', 'libvorbis', '-q:a', '4', og], check=True)
        d = open(og, 'rb').read(); tot += len(d)
        ents.append((r['n'], r['l'], r['cents'], r['span'], r['loop'][0], r['loop'][1], d))
    print('%-18s %3d vzoriek, vrstvy %d, %.1f MB, zisk %.1f dB' % (ins['name'], len(ents), top + 1, tot / 1e6, 20 * np.log10(extra)))
    blob = b'INST' + struct.pack('<BBHf', (1 if ins['sus'] else 0) | (2 if ins.get('kit') else 0), top + 1, len(ents), extra)
    for n, l, c, sp, ls, le, d in ents: blob += struct.pack('<BBbBIII', n, l, c, sp, ls, le, len(d))
    for *_, d in ents: blob += d
    return blob

def build(ids, out):
    blobs = []
    for ins in I:
        if ids and ins['id'] not in ids: continue
        blobs.append((ins['id'], process(ins, '/home/claude/smp/out')))
    if os.path.isdir(out):   # priecinok: kazdy nastroj zvlast
        for i, b in blobs:
            one = b'KNIN' + struct.pack('<II', 1, 1) + struct.pack('<III', i, 24, len(b)) + b
            open(os.path.join(out, 'instr_%02d.knin' % i), 'wb').write(one)
        return
    hdr = b'KNIN' + struct.pack('<II', 1, len(blobs))
    off = len(hdr) + 12 * len(blobs)
    tab = b''; body = b''
    for i, b in blobs:
        tab += struct.pack('<III', i, off + len(body), len(b)); body += b
    open(out, 'wb').write(hdr + tab + body)
    print('balik %.1f MB' % (os.path.getsize(out) / 1e6))

def split(src, outdir):
    # rozdeli balik KNIN na samostatne subory (jeden nastroj = jeden subor instr_NN.knin)
    d = open(src, 'rb').read(); cnt = struct.unpack('<I', d[8:12])[0]
    os.makedirs(outdir, exist_ok=True)
    for k in range(cnt):
        i, off, sz = struct.unpack('<III', d[12 + 12 * k:24 + 12 * k])
        one = b'KNIN' + struct.pack('<II', 1, 1) + struct.pack('<III', i, 24, sz) + d[off:off + sz]
        open(os.path.join(outdir, 'instr_%02d.knin' % i), 'wb').write(one)

if __name__ == '__main__':
    if sys.argv[1] == 'split': split(sys.argv[2], sys.argv[3])
    elif sys.argv[1] == 'analyze': analyze()
    elif sys.argv[1] == 'build': build([int(a) for a in sys.argv[3:]], sys.argv[2])
