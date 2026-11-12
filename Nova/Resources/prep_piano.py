# Priprava klavirnych vzoriek (Salamander Grand Piano V3, CC-BY 3.0) do jedneho balika.
import soundfile as sf, numpy as np, subprocess, struct, os, sys, io
SRC='/home/claude/sfzinstruments/salamandergrandpiano/Samples'
OUT='/home/claude/nova/prep/out'; os.makedirs(OUT,exist_ok=True)
names=['A0','C1','D#1','F#1','A1','C2','D#2','F#2','A2','C3','D#3','F#3','A3','C4','D#4','F#4','A4','C5','D#5','F#5','A5','C6','D#6','F#6','A6','C7','D#7','F#7','A7','C8']
vels=[4,8,12,16]
def midi(n):
    nm,o=n[:-1],int(n[-1]); return 12*(o+1)+['C','C#','D','D#','E','F','F#','G','G#','A','A#','B'].index(nm)
def cap(m):  # max dlzka podla vysky
    return float(np.interp(m,[21,48,60,72,84,96,108],[14,11,9,7,5,3.5,2.5]))
entries=[]; total=0; tdur=0
for n in names:
    m=midi(n)
    for vi,v in enumerate(vels):
        x,sr=sf.read(f'{SRC}/{n}v{v}.flac',dtype='float32')
        mono=np.abs(x).max(axis=1)
        pk=mono.max()
        # zaciatok: prvy vzorok nad -50 dB spicky, s 2 ms rezervou
        st=max(0,int(np.argmax(mono>pk*0.003))-int(0.002*sr))
        x=x[st:]; mono=mono[st:]
        # koniec: obalka pod -66 dB alebo maximum podla vysky
        win=int(0.05*sr); env=np.sqrt(np.convolve(mono**2,np.ones(win)/win,mode='same'))
        thr=pk*10**(-66/20); above=np.where(env>thr)[0]
        end=above[-1] if len(above) else len(x)
        end=min(end,int(cap(m)*sr),len(x))
        x=x[:end].copy(); fl=min(int(0.4*sr),len(x)//3)
        x[-fl:]*=np.linspace(1,0,fl)[:,None]**2
        tmp=f'{OUT}/t.wav'; sf.write(tmp,x,sr,subtype='FLOAT')
        og=f'{OUT}/{n.replace("#","s")}v{v}.ogg'
        subprocess.run(['ffmpeg','-y','-loglevel','error','-i',tmp,'-c:a','libvorbis','-q:a','5',og],check=True)
        data=open(og,'rb').read(); entries.append((m,vi,data)); total+=len(data); tdur+=len(x)/sr
print('files',len(entries),'ogg MB %.1f'%(total/1e6),'total sec %.0f'%tdur,'RAM int16 MB %.0f'%(tdur*48000*2*2/1e6))
# balik: 'KNPK' ver count, potom (note u8, vel u8, size u32) + data
with open('/home/claude/nova/prep/piano.knpk','wb') as f:
    f.write(b'KNPK'); f.write(struct.pack('<II',1,len(entries)))
    for m,vi,d in entries: f.write(struct.pack('<BBI',m,vi,len(d)))
    for m,vi,d in entries: f.write(d)
print('pack MB %.1f'%(os.path.getsize('/home/claude/nova/prep/piano.knpk')/1e6))
