import sys, wave, numpy as np

BANDS = [20,25,31.5,40,50,63,80,100,125,160,200,250,315,400,500,630,800,1000,
         1250,1600,2000,2500,3150,4000,5000,6300,8000,10000,12500,16000,20000]

def read_wav(path):
    w = wave.open(path, 'rb')
    sr = w.getframerate(); ch = w.getnchannels(); n = w.getnframes()
    raw = w.readframes(n); w.close()
    a = np.frombuffer(raw, dtype=np.int16).astype(np.float32) / 32768.0
    a = a.reshape(-1, ch)
    L = a[:,0]; R = a[:,1] if ch > 1 else a[:,0]
    return sr, L, R

def avg_power(x, sr, nfft=8192, hop=4096):
    win = np.hanning(nfft).astype(np.float32)
    acc = np.zeros(nfft//2+1); cnt = 0
    for i in range(0, len(x)-nfft, hop):
        S = np.fft.rfft(x[i:i+nfft]*win)
        acc += (S.real**2 + S.imag**2); cnt += 1
    return acc/max(cnt,1), sr/nfft

def fold_db(power, binhz):
    half = len(power); out = np.zeros(len(BANDS))
    for b,fc in enumerate(BANDS):
        lo = 0 if b==0 else (BANDS[b-1]*fc)**0.5
        hi = binhz*(half-1) if b==len(BANDS)-1 else (fc*BANDS[b+1])**0.5
        lob = max(1, int(lo/binhz)); hib = min(half-1, max(lob, int(hi/binhz)))
        out[b] = 10*np.log10(power[lob:hib+1].mean()+1e-12)
    return out

def crest(x):
    rms = np.sqrt(np.mean(x**2))+1e-9
    return 20*np.log10((np.max(np.abs(x))+1e-9)/rms)

def lowband_crest(x, sr, cutoff_block=100):
    n = (len(x)//cutoff_block)*cutoff_block
    low = x[:n].reshape(-1, cutoff_block).mean(axis=1)  # boxcar LP ~ sr/(2*block) Hz
    return crest(low), 20*np.log10(np.sqrt(np.mean(low**2))+1e-9)

def analyze(path):
    sr, L, R = read_wav(path)
    mono = 0.5*(L+R); side = 0.5*(L-R)
    p, binhz = avg_power(mono, sr)
    db = fold_db(p, binhz)
    lc, lrms = lowband_crest(mono, sr)
    midrms = 20*np.log10(np.sqrt(np.mean(mono**2))+1e-9)
    siderms = 20*np.log10(np.sqrt(np.mean(side**2))+1e-9)
    return dict(db=db, crest=crest(mono), lowcrest=lc, lowrms=lrms,
                widthdb=siderms-midrms)

def group(db, f0, f1):
    idx = [i for i,f in enumerate(BANDS) if f0<=f<=f1]
    return db[idx].mean()

def midref(db):
    idx = [i for i,f in enumerate(BANDS) if 400<=f<=2000]  # effect leaves mids flat -> anchor here
    return db - db[idx].mean()

o = analyze(sys.argv[1]); y = analyze(sys.argv[2])
od = midref(o['db']); yd = midref(y['db'])  # absolute tonal change vs the untouched mids
diff = yd - od

print("=== DIFFERENCE CURVE (YouTube - Original), tonal, dB per band ===")
for i,f in enumerate(BANDS):
    bar = '#'*int(max(0,diff[i])*3) + '-'*int(max(0,-diff[i])*3)
    print(f"{f:>7.0f} Hz : {diff[i]:+5.1f}  {bar}")

print("\n=== BAND GROUPS (relative dB, YT vs ORIG) ===")
for name,a,b in [("sub 20-45",20,45),("bass 50-100",50,100),("punch 110-180",110,180),
                 ("lowmid 200-350",200,350),("mid 400-2k",400,2000),("high 3k-12k",3150,12500)]:
    print(f"{name:>16}: orig {group(od,a,b):+5.1f}  yt {group(yd,a,b):+5.1f}  -> {group(yd,a,b)-group(od,a,b):+5.1f}")

print("\n=== DYNAMICS / WIDTH ===")
print(f"full crest factor : orig {o['crest']:5.1f} dB   yt {y['crest']:5.1f} dB   (lower = more compressed)")
print(f"low-band crest    : orig {o['lowcrest']:5.1f} dB   yt {y['lowcrest']:5.1f} dB")
print(f"stereo width(S/M) : orig {o['widthdb']:5.1f} dB   yt {y['widthdb']:5.1f} dB")
print(f"low-band RMS      : orig {o['lowrms']:5.1f} dB   yt {y['lowrms']:5.1f} dB   (louder bass in YT?)")
