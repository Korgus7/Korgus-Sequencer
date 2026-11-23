# Priprava klavirnych vzoriek: VCSL "Grand Piano, Steinway B" (Versilian Studios, CC0), blizky mikrofon, bez pedalu.
# 42 not (kazdy druhy poltón) x 3 sily uderu; najtichsia vrstva (0) = vl2 jemnejsie (filter, mäkší nabeh), 1..3 = vl2..vl4.
import soundfile as sf, numpy as np, subprocess, struct, os, re, glob
from scipy.signal import lfilter, stft, istft
SRC='/home/claude/vcsl_src/Chordophones/Zithers/Grand Piano, Steinway B/NoSus'
OUT='/home/claude/nova/prep3'; os.makedirs(OUT,exist_ok=True)
NAMES=['C','C#','D','D#','E','F','F#','G','G#','A','A#','B']
def midi(n): m=re.match(r'([A-G]#?)(-?\d)',n); return 12*(int(m.group(2))+1)+NAMES.index(m.group(1))
def cap(m): return float(np.interp(m,[21,48,60,72,84,96,108],[14,11,9,7,5,3.5,2.5]))
def bq_lp(f,sr,q):
    w=2*np.pi*f/sr; al=np.sin(w)/(2*q); c=np.cos(w); b=[(1-c)/2,1-c,(1-c)/2]; a=[1+al,-2*c,1-al]; return np.array(b)/a[0],np.array(a)/a[0]
files={}
for f in glob.glob(SRC+'/*.wav'):
    m=re.search(r'Close_([A-G]#?-?\d)_vl(\d)_rr1',f)
    if m: files[(midi(m.group(1)),int(m.group(2)))]=f
notes=sorted(set(n for n,_ in files))
# odstranenie sumu nahravky: profil sumu z najtichsich miest (koniec dozvuku) nizkych a strednych not
NPER,HOP=2048,512
# nahravky na konci zoslabuju do ticha, preto profil: 10. percentil kazdeho pasma v useku 0.5 s .. 9 s, median cez vsetky subory
prof=[]
for (n,vl),f in files.items():
    x,sr=sf.read(f,dtype='float32',always_2d=True)
    a0,a1=int(0.5*sr),min(int(9*sr),len(x)-int(2*sr))
    if a1-a0<sr: continue
    for c in range(2):
        _,_,Z=stft(x[a0:a1,c],nperseg=NPER,noverlap=NPER-HOP); prof.append(np.percentile(np.abs(Z)**2,25,axis=1)/0.288)   # 25. percentil -> stredny vykon sumu
NOISE=np.median(np.array(prof),axis=0)
print('profil sumu z',len(prof),'useciek')
def denoise(x):
    out=np.zeros_like(x)
    for c in range(x.shape[1]):
        _,_,Z=stft(x[:,c],nperseg=NPER,noverlap=NPER-HOP); P=np.abs(Z)**2
        Ps=P.copy()
        for k in range(1,Ps.shape[1]): Ps[:,k]=np.maximum(P[:,k],0.6*Ps[:,k-1]+0.4*P[:,k])   # vyhladenie v case
        Ps=(np.roll(Ps,1,0)+2*Ps+np.roll(Ps,-1,0))/4                                          # a vo frekvencii
        G=np.sqrt(np.clip(1-2.5*NOISE[:,None]/np.maximum(Ps,1e-20),10**(-24/10),1))
        for k in range(1,G.shape[1]): G[:,k]=np.maximum(G[:,k],0.85*G[:,k-1])               # pomaly pokles zisku (bez "bublania")
        _,y=istft(Z*G,nperseg=NPER,noverlap=NPER-HOP); y=y[:len(x)]
        out[:len(y),c]=y
    return out.astype(np.float32)
entries=[]; total=0
for n in notes:
    for layer,(vl,soft) in enumerate([(2,True),(2,False),(3,False),(4,False)]):
        x,sr=sf.read(files[(n,vl)],dtype='float32',always_2d=True); x=denoise(x)
        mono=np.abs(x).max(axis=1); pk=mono.max()
        st=max(0,int(np.argmax(mono>pk*0.003))-int(0.002*sr)); x=x[st:]; mono=mono[st:]
        win=int(0.05*sr); env=np.sqrt(np.convolve(mono**2,np.ones(win)/win,mode='same'))
        above=np.where(env>pk*10**(-66/20))[0]; end=above[-1] if len(above) else len(x)
        end=min(end,int(cap(n)*sr),len(x)); x=x[:end].copy()
        if soft:
            for fq,q in ((2200,0.6),(3200,0.7)): b,a=bq_lp(fq,sr,q); x=lfilter(b,a,x,axis=0).astype(np.float32)
            r=int(0.006*sr); x[:r]*=np.linspace(0.4,1,r)[:,None]; x*=0.6
        fl=min(int(0.4*sr),len(x)//3); x[-fl:]*=np.linspace(1,0,fl)[:,None]**2
        tmp=f'{OUT}/t.wav'; sf.write(tmp,x,sr,subtype='FLOAT')
        og=f'{OUT}/{n}_{layer}.ogg'
        subprocess.run(['ffmpeg','-y','-loglevel','error','-i',tmp,'-ar','48000','-c:a','libvorbis','-q:a','6',og],check=True)
        d=open(og,'rb').read(); entries.append((n,layer,d)); total+=len(d)
print('vzoriek',len(entries),'not',len(notes),'MB %.1f'%(total/1e6))
with open(OUT+'/piano.knpk','wb') as f:
    f.write(b'KNPK'); f.write(struct.pack('<II',1,len(entries)))
    for m,l,d in entries: f.write(struct.pack('<BBI',m,l,len(d)))
    for m,l,d in entries: f.write(d)
print('balik MB %.1f'%(os.path.getsize(OUT+'/piano.knpk')/1e6))
