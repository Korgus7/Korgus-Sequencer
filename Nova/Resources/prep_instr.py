# Nastroje pre Korgus Inspiria: vyber vzoriek, orezanie, slucky, ladenie, balik KNIN
import glob, re, os, sys, struct, subprocess, json
import numpy as np, soundfile as sf

S = '/home/claude/smp'
NN = {'c':0,'c#':1,'db':1,'d':2,'d#':3,'eb':3,'e':4,'f':5,'f#':6,'gb':6,'g':7,'g#':8,'ab':8,'a':9,'a#':10,'bb':10,'b':11}
def midi(name):
    m = re.match(r'([A-Ga-g][#b]?)(-?\d)$', name)
    return 12 * (int(m.group(2)) + 1) + NN[m.group(1).lower()]

# id, nazov, zdroje [(glob, regex so skupinami note a vel)], poradie sil uderu, drzany ton, max. dlzka (s), oktavovy posun nazvov
I = [
 dict(id=1, name='Pianino', src=[('vcsl/Chordophones/Zithers/Upright Piano, Yamaha/Sustains/*.wav', r'_Sus_([A-G]#?-?\d)_vl(\d)_rr1')], vel=['1','2','3'], sus=False, maxlen=7, oct=1),
 dict(id=2, name='Rhodes', src=[('jlearman.jRhodes3d/jRhodes3d-mono/*.flac', r'__([A-G][b#]?\d)_(\d)')], vel=['5','4','3','2','1'], sus=False, maxlen=6.5, lf=[0.4,0.55,0.7,0.85,1.0]),
 dict(id=3, name='Cembalo', src=[('vcsl/Chordophones/Zithers/Harpsichord, French/Sustains/*.wav', r'_Normal_([A-G]#?\d)_rr1_')], vel=[None], sus=False, maxlen=6, oct=1),
 dict(id=4, name='Akusticka gitara', src=[('karoryfer.shinyguitar/Samples/acoustic/*.wav', r'/([a-g]b?\d)_vl(\d)_rr1_1')], vel=['1','2','3','4'], sus=False, maxlen=6),
 dict(id=5, name='Jazzova gitara', src=[('karoryfer.shinyguitar/Samples/electric/*.wav', r'/([a-g]b?\d)_vl(\d)_rr1_2')], vel=['1','2','3','4'], sus=False, maxlen=6),
 dict(id=6, name='Basgitara', src=[('karoryfer.growlybass/sustain/*.wav', r'/([a-g]b?\d)_(pp|p|f|ff)_rr1')], vel=['pp','p','f','ff'], sus=False, maxlen=5, oct=-1),
 dict(id=7, name='Kontrabas', src=[('VSCO-2-CE/Strings/Solo Contrabass/Pizz/*.wav', r'_Pizz_([A-G]#?\d)_v(\d)_rr1')], vel=['1','3'], sus=False, maxlen=3.5, oct=1),
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
 dict(id=33, name='Elektricka gitara', src=[('karoryfer.emilyguitar/notes/*.wav', r'/([a-g]b?\d)_(p|mp|mf|f)_rr1')], vel=['p','mp','mf','f'], sus=False, maxlen=6),
 dict(id=34, name='Okarina', src=[('vcsl/Aerophones/Edge-blown Aerophones/Ocarina, Typical/Sustains/Sus/*.wav', r'_Sus_([A-G]#?\d)\.wav')], vel=[None], sus=True, maxlen=4, oct=1),
 dict(id=35, name='Zobcova flauta', src=[('vcsl/Aerophones/Edge-blown Aerophones/Baroque Alto Recorder/Sustain/*.wav', r'_Sus_([A-G]#?\d)_rr1')], vel=[None], sus=True, maxlen=4, oct=1),
]

def collect(ins):
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

def process(ins, outdir):
    items = collect(ins)
    lay = sorted(set(l for _, l, _ in items)); lmap = {l: i for i, l in enumerate(lay)}
    recs = []
    for n, l, f in items:
        x, sr = sf.read(f, dtype='float32', always_2d=True)
        if x.shape[1] > 2: x = x[:, :2]
        mono = x.mean(axis=1)
        pk = np.abs(mono).max() + 1e-9
        # ladenie
        cents = 0
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
            x = x[:end].copy(); fl = min(int(0.35 * sr), len(x) // 3)
            x[-fl:] *= (np.linspace(1, 0, fl) ** 2)[:, None]
            body = mono[:min(len(mono), int(0.4 * sr))]
        rms = np.sqrt(np.mean(body ** 2)) + 1e-9
        recs.append(dict(n=n, l=lmap[l], cents=cents, x=x, sr=sr, loop=loop, rms=rms, f=f))
    # hlasitost: rovnaka po klaviature, medzi vrstvami prirodzeny pomer
    top = max(r['l'] for r in recs)
    byn = {}
    for r in recs: byn.setdefault(r['n'], {})[r['l']] = r['rms']
    lf = {}
    for l in range(top + 1):
        rs = [d[l] / d[top] for d in byn.values() if l in d and top in d]
        lf[l] = float(np.median(rs)) if rs else 1.0
    if 'lf' in ins: lf = {i: v for i, v in enumerate(ins['lf'])}
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
        ents.append((r['n'], r['l'], r['cents'], r['loop'][0], r['loop'][1], d))
    print('%-18s %3d vzoriek, vrstvy %d, %.1f MB, zisk %.1f dB' % (ins['name'], len(ents), top + 1, tot / 1e6, 20 * np.log10(extra)))
    blob = b'INST' + struct.pack('<BBHf', 1 if ins['sus'] else 0, top + 1, len(ents), extra)
    for n, l, c, ls, le, d in ents: blob += struct.pack('<BBbBIII', n, l, c, 0, ls, le, len(d))
    for *_, d in ents: blob += d
    return blob

def build(ids, out):
    blobs = []
    for ins in I:
        if ids and ins['id'] not in ids: continue
        blobs.append((ins['id'], process(ins, '/home/claude/smp/out')))
    hdr = b'KNIN' + struct.pack('<II', 1, len(blobs))
    off = len(hdr) + 12 * len(blobs)
    tab = b''; body = b''
    for i, b in blobs:
        tab += struct.pack('<III', i, off + len(body), len(b)); body += b
    open(out, 'wb').write(hdr + tab + body)
    print('balik %.1f MB' % (os.path.getsize(out) / 1e6))

if __name__ == '__main__':
    if sys.argv[1] == 'analyze': analyze()
    elif sys.argv[1] == 'build': build([int(a) for a in sys.argv[3:]], sys.argv[2])
