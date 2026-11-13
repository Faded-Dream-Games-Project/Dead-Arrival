#!/usr/bin/env python3
"""Synthesize all SFX WAVs + ambient music loops for Dead Arrival (numpy)."""
import os, struct, math, random
import numpy as np

SFX = '/home/z/my-project/Dead-Arrival/sfx'
os.makedirs(SFX, exist_ok=True)
random.seed(7)
np.random.seed(7)
SR = 44100

def norm(x, peak=0.9):
    m = np.max(np.abs(x)) or 1.0
    return (x / m) * peak

def env_ad(n, a, d):
    e = np.ones(n)
    an = max(1, int(a * SR)); dn = max(1, int(d * SR))
    an = min(an, n); dn = min(dn, n)
    e[:an] = np.linspace(0, 1, an)
    e[n-dn:] *= np.linspace(1, 0, dn)
    return e

def save(name, x, vol=1.0):
    x = norm(x) * vol
    data = (x * 32767).astype('<i2')
    with open(f'{SFX}/{name}.wav', 'wb') as f:
        f.write(b'RIFF'); f.write(struct.pack('<I', 36 + len(data)*2, )); f.write(b'WAVE')
        f.write(b'fmt '); f.write(struct.pack('<IHHIIHH', 16, 1, 1, SR, SR*2, 2, 16))
        f.write(b'data'); f.write(struct.pack('<I', len(data)*2))
        f.write(data.tobytes())
    print(name)

def t(dur): return np.arange(int(dur*SR)) / SR

# --- gunshots ---
def gunshot(dur, noise_amt, lowfreq, punch):
    n = t(dur)
    body = np.random.uniform(-1, 1, len(n)) * np.exp(-n * 22)
    thump = np.sin(2*np.pi*lowfreq*n) * np.exp(-n * 14) * punch
    crack = np.random.uniform(-1, 1, len(n)) * np.exp(-n * 90) * noise_amt
    x = body*0.6 + thump + crack*0.5
    return norm(x)

save('shot_light',  gunshot(0.22, 0.8, 190, 1.0))
save('shot_heavy',  gunshot(0.30, 1.0, 130, 1.3))
save('shot_shotgun', gunshot(0.38, 1.2, 95, 1.5))
save('shot_sniper', gunshot(0.55, 1.0, 110, 1.4))

# knife swing (whoosh)
n = t(0.28)
sw = np.sin(2*np.pi*(300 + 900*n)*n) * env_ad(len(n), 0.08, 0.14)
sw += np.random.uniform(-1, 1, len(n)) * 0.18 * env_ad(len(n), 0.05, 0.2)
save('knife', norm(sw) * 0.7)

# hits
n = t(0.12)
save('hit', norm(np.random.uniform(-1, 1, len(n)) * np.exp(-n*50) + np.sin(2*np.pi*800*n)*np.exp(-n*60)*0.5) * 0.55)
n = t(0.3)   # squish
sq = np.random.uniform(-1, 1, len(n)) * np.exp(-n*18)
sq += np.sin(2*np.pi*70*n) * np.exp(-n*12)
save('squish', norm(sq) * 0.7)

# groans (low formant wobble)
def groan(base):
    dur = 1.4
    n = t(dur)
    f = base * (1 + 0.12*np.sin(2*np.pi*2.2*n) + 0.2*n/dur)
    x = np.sin(2*np.pi*np.cumsum(f)/SR)
    x += 0.5*np.sin(2*np.pi*2.01*np.cumsum(f)/SR)
    x += 0.3*np.sin(2*np.pi*3.02*np.cumsum(f)/SR)
    x += np.random.uniform(-1, 1, len(n)) * 0.12
    e = np.minimum(1, n*3) * np.exp(-((n - 0.5)**2) * 2.2)
    return norm(x * e) * 0.6
save('groan1', groan(95))
save('groan2', groan(78))
save('groan3', groan(112))

# zombie attack snarl
n = t(0.4)
gr = np.random.uniform(-1, 1, len(n)) * np.exp(-n*7) * 0.6
f = 140 + 60*np.sin(2*np.pi*7*n)
gr += np.sin(2*np.pi*np.cumsum(f)/SR) * np.exp(-n*9)
save('zattack', norm(gr) * 0.7)

# player hurt
n = t(0.3)
x = np.sin(2*np.pi*(220 - 120*n)*n) * env_ad(len(n), 0.02, 0.25)
save('hurt', norm(x) * 0.7)

# down (descending tone)
n = t(1.2)
x = np.sin(2*np.pi*np.cumsum(np.linspace(300, 60, len(n)))/SR) * env_ad(len(n), 0.05, 0.9)
save('down', norm(x) * 0.8)

# door (wooden creak + thud)
n = t(0.8)
cr = np.sin(2*np.pi*np.cumsum(np.linspace(90, 55, len(n)))/SR) * env_ad(len(n), 0.1, 0.5)
th = np.random.uniform(-1, 1, len(n)) * np.exp(-np.maximum(n-0.55, 0)*40) * (n > 0.55)
save('door', norm(cr*0.6 + th) * 0.8)

# buy (cash register ding)
n = t(0.5)
x = (np.sin(2*np.pi*1320*n) + 0.6*np.sin(2*np.pi*1980*n)) * np.exp(-n*9)
save('buy', norm(x) * 0.55)

# denied (low buzz)
n = t(0.35)
x = np.sign(np.sin(2*np.pi*110*n)) * 0.4 * env_ad(len(n), 0.01, 0.3)
save('denied', norm(x) * 0.5)

# box open (rising shimmer)
n = t(1.1)
f = np.linspace(200, 900, len(n))
x = np.sin(2*np.pi*np.cumsum(f)/SR) * env_ad(len(n), 0.25, 0.5)
x += np.sin(2*np.pi*np.cumsum(f*1.5)/SR) * 0.4 * env_ad(len(n), 0.3, 0.5)
save('box_open', norm(x) * 0.6)

# box spin tick
n = t(0.09)
x = np.sin(2*np.pi*620*n) * np.exp(-n*70)
save('box_spin', norm(x) * 0.4)

# box win (chord fanfare)
n = t(1.3)
e = env_ad(len(n), 0.04, 1.0)
x = np.zeros(len(n))
for i, fq in enumerate([262, 330, 392, 523]):
    x += np.sin(2*np.pi*fq*n + i) * (0.8 ** i)
save('box_win', norm(x * e) * 0.6)

# round start (deep horn)
n = t(1.6)
x = np.sin(2*np.pi*65*n) + 0.7*np.sin(2*np.pi*98*n) + 0.4*np.sin(2*np.pi*130*n + 1.3)
x *= env_ad(len(n), 0.25, 0.9)
save('round_start', norm(x) * 0.75)

# round end (resolve)
n = t(1.4)
x = np.sin(2*np.pi*196*n)*0.5 + np.sin(2*np.pi*294*n)*0.5
x *= env_ad(len(n), 0.15, 0.9)
save('round_end', norm(x) * 0.55)

# reload (clack clack)
n = t(0.5)
x = np.zeros(len(n))
for at in (0.06, 0.3):
    i = int(at*SR)
    seg = int(0.05*SR)
    x[i:i+seg] += np.random.uniform(-1, 1, seg) * np.exp(-np.arange(seg)/SR*80)
    x[i:i+seg] += np.sin(2*np.pi*500*np.arange(seg)/SR) * np.exp(-np.arange(seg)/SR*90) * 0.6
save('reload', norm(x) * 0.5)

# dry fire click
n = t(0.1)
x = np.sin(2*np.pi*1500*n) * np.exp(-n*90) + np.random.uniform(-1,1,len(n))*np.exp(-n*120)*0.3
save('dry', norm(x) * 0.4)

# footsteps
for name, freq in (('step1', 130), ('step2', 105)):
    n = t(0.16)
    x = np.random.uniform(-1, 1, len(n)) * np.exp(-n*35) * 0.7 + np.sin(2*np.pi*freq*n)*np.exp(-n*40)
    save(name, norm(x) * 0.35)

# heartbeat
n = t(0.9)
x = np.zeros(len(n))
for at in (0.05, 0.32):
    i = int(at*SR); seg = int(0.12*SR)
    tt = np.arange(seg)/SR
    x[i:i+seg] += np.sin(2*np.pi*55*tt) * np.exp(-tt*18)
save('heartbeat', norm(x) * 0.6)

# zombie rise (dirt rumble)
n = t(1.5)
x = np.random.uniform(-1, 1, len(n)) * env_ad(len(n), 0.5, 0.8) * 0.5
x += np.sin(2*np.pi*48*n) * env_ad(len(n), 0.4, 0.9) * 0.8
save('zrise', norm(x) * 0.65)

# jump pad (boing)
n = t(0.45)
f = np.cumsum(np.linspace(160, 420, len(n)))
x = np.sin(2*np.pi*f/SR) * np.exp(-n*7)
save('jump_pad', norm(x) * 0.6)

# zipline (whirr)
n = t(2.0)
x = np.sin(2*np.pi*70*n) * 0.4 + np.random.uniform(-1, 1, len(n))*0.25*np.exp(-n*0.8)
x *= env_ad(len(n), 0.15, 0.6)
save('zipline', norm(x) * 0.5)

# perk drink (fizzy)
n = t(1.0)
x = np.random.uniform(-1, 1, len(n)) * env_ad(len(n), 0.3, 0.6) * 0.5
fz = np.cumsum(np.linspace(400, 800, len(n)))
x += np.sin(2*np.pi*fz/SR) * 0.3 * env_ad(len(n), 0.2, 0.7)
save('perk', norm(x) * 0.55)

# --- ambient loops per map ---
def ambient(name, base, wind, dark):
    dur = 24.0
    n = t(dur)
    x = np.random.uniform(-1, 1, len(n))
    # low drone
    x = np.sin(2*np.pi*base*n) * 0.5 + np.sin(2*np.pi*base*1.5*n) * 0.25
    # slow wind (filtered noise)
    windn = np.random.uniform(-1, 1, len(n))
    kernel = np.ones(int(0.04*SR)) / int(0.04*SR)
    windf = np.convolve(windn, kernel, 'same')
    wob = 0.5 + 0.5*np.sin(2*np.pi*0.05*n + random.random()*6)
    x += windf * wind * wob
    if dark:
        # distant moan
        mo = np.sin(2*np.pi*(base*0.5)*n) * (0.5+0.5*np.sin(2*np.pi*0.033*n))
        x += mo * 0.3
    # fade loop edges
    fe = int(1.5*SR)
    x[:fe] *= np.linspace(0, 1, fe); x[-fe:] *= np.linspace(1, 0, fe)
    save(name, norm(x) * 0.42)

ambient('ambient0', 55, 0.5, True)    # theater: hollow hall
ambient('ambient1', 48, 0.8, False)   # bunker: wind over ruins
ambient('ambient2', 44, 0.6, True)    # graveyard: moaning dark
ambient('ambient3', 62, 0.9, False)   # die arise: high wind
print("ALL SFX DONE")
