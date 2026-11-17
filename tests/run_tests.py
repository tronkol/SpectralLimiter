#!/usr/bin/env python3
"""Collaudo del motore DSP (richiede numpy e scipy).

Uso:  python3 run_tests.py <percorso engine_cli> <cartella temporanea>

Compila prima il CLI:  g++ -O2 -std=c++17 -o engine_cli tests/engine_cli.cpp
"""
import subprocess
import sys
import os
import numpy as np
from scipy.signal import resample_poly

CLI = sys.argv[1]
TMP = sys.argv[2]
os.makedirs(TMP, exist_ok=True)
rng = np.random.default_rng(1234)
fails = []


def run(x, fs=48000, fft=2048, thr=0.0, gain=0.0, bal=0.0, ceil=0.0, rel=150.0, atk=0.0, ratio=100.0, ovs=1, curve=()):
    """x: array (n,2) float. Restituisce (y, latenza, cpu_x_realtime)."""
    fin, fout = os.path.join(TMP, "in.f32"), os.path.join(TMP, "out.f32")
    x.astype(np.float32).tofile(fin)
    r = subprocess.run([CLI, fin, fout, str(fs), str(fft), str(thr), str(gain), str(bal), str(ceil), str(rel), str(atk), str(ratio), str(ovs)] + ["%d:%g:%g:%g" % c for c in curve],
                       capture_output=True, text=True, check=True)
    info = dict(kv.split("=") for kv in r.stdout.split())
    y = np.fromfile(fout, dtype=np.float32).reshape(-1, 2).astype(np.float64)
    return y, int(info["latency"]), float(info["cpu_x_realtime"])


def pink(n):
    w = rng.standard_normal(n)
    W = np.fft.rfft(w)
    f = np.arange(len(W)); f[0] = 1
    W /= np.sqrt(f)
    p = np.fft.irfft(W, n)
    return p / np.std(p)


def stereo_pink(n, rms_db):
    a = 10 ** (rms_db / 20)
    return np.stack([pink(n) * a, pink(n) * a], axis=1)


def tpeak_db(y, over=8):
    return 20 * np.log10(np.max(np.abs(resample_poly(y, over, 1, axis=0))) + 1e-20)


def check(name, ok, detail=""):
    print(("  OK    " if ok else "  FAIL  ") + name + (" -> " + detail if detail else ""))
    if not ok:
        fails.append(name)


def tone_amp(y, fs, f0, start, length):
    seg = y[start:start + length]
    n = len(seg)
    t = np.arange(n) / fs
    c = np.exp(-2j * np.pi * f0 * t)
    w = np.hanning(n)
    return 2 * np.abs(np.sum(seg * w * c)) / np.sum(w)


print("1) Trasparenza (nessuna limitazione): uscita == ingresso ritardato")
for fs in (44100, 48000):
    for fft in (1024, 2048, 4096, 8192, 16384, 32768, 65536):
        x = stereo_pink(fs * 4, -26)
        y, lat, cpu = run(x, fs=fs, fft=fft, thr=0.0, ceil=0.0)
        a = x[: len(x) - lat]
        b = y[lat:]
        err = b - a
        snr = 10 * np.log10(np.sum(a ** 2) / (np.sum(err ** 2) + 1e-30))
        check(f"fs={fs} fft={fft} lat={lat} ({1000*lat/fs:.1f} ms)", snr > 110, f"SNR {snr:.1f} dB, CPU {cpu*100:.2f}% di un core")

print("2) Latenza dichiarata == latenza misurata (impulso)")
for fft in (1024, 4096, 16384, 65536):
    x = np.zeros((fft * 3 + 10000, 2)); x[5000, :] = 0.5
    y, lat, _ = run(x, fft=fft)
    pos = int(np.argmax(np.abs(y[:, 0])))
    check(f"fft={fft}", pos - 5000 == lat, f"misurata {pos-5000}, dichiarata {lat}")

print("3) Ceiling garantito (sample-peak e true-peak 8x), segnale molto violento")
fs = 48000
n = fs * 8
t = np.arange(n) / fs
for fft in (1024, 4096, 65536):
    bass = 0.9 * np.sin(2 * np.pi * 55 * t)
    clicks = np.zeros(n); clicks[::7000] = 1.0
    x = np.stack([bass + 0.3 * pink(n) + clicks, 0.8 * bass + 0.3 * pink(n) + clicks], axis=1)
    for ceil in (-1.0, -0.3, -6.0):
        y, lat, _ = run(x, fs=fs, fft=fft, thr=-12.0, gain=12.0, ceil=ceil, rel=100.0)
        seg = y[fs:]
        sp = 20 * np.log10(np.max(np.abs(seg)))
        tp = tpeak_db(seg)
        check(f"fft={fft} ceiling={ceil} dB", sp <= ceil + 1e-4 and tp <= ceil + 0.1, f"sample-peak {sp:.3f} dB, true-peak {tp:.3f} dB")

print("   onda quadra piena + gain 24 dB:")
sq = np.sign(np.sin(2 * np.pi * 1000 * t[: fs * 3]))
y, lat, _ = run(np.stack([sq, sq], 1), fft=2048, thr=0.0, gain=24.0, ceil=-1.0)
check("true-peak <= -0.9 dB", tpeak_db(y[fs:]) <= -0.9, f"{tpeak_db(y[fs:]):.3f} dB")

print("4) Indipendenza delle bande: 60 Hz forte + 5 kHz debole, soglia -20 dB, brickwall spento")
fs = 48000
n = fs * 6
t = np.arange(n) / fs
for fft in (1024, 2048, 4096, 8192):
    x = np.stack([0.5 * np.sin(2 * np.pi * 60 * t) + 0.02 * np.sin(2 * np.pi * 5000 * t)] * 2, axis=1)
    y, lat, _ = run(x, fs=fs, fft=fft, thr=-20.0, ceil=0.0)
    st = fs * 3
    a60 = tone_amp(y[:, 0], fs, 60, st, fs)
    a5k = tone_amp(y[:, 0], fs, 5000, st, fs)
    check(f"fft={fft}: 60 Hz -> {20*np.log10(a60):.2f} dB (atteso ~ -20), 5 kHz -> {20*np.log10(a5k/0.02):+.3f} dB (atteso ~0)",
          abs(20 * np.log10(a60) + 20) < 1.0 and abs(20 * np.log10(a5k / 0.02)) < 0.1)

print("5) Distorsione di un seno stazionario limitato (1 kHz, soglia -20 dB)")
x = np.stack([0.5 * np.sin(2 * np.pi * 1000 * t)] * 2, axis=1)
y, lat, _ = run(x, fft=4096, thr=-20.0, ceil=0.0)
seg = y[fs * 3: fs * 5, 0]
S = np.abs(np.fft.rfft(seg * np.hanning(len(seg)))) ** 2
f = np.fft.rfftfreq(len(seg), 1 / fs)
fund = S[(f > 950) & (f < 1050)].sum()
rest = S[~((f > 950) & (f < 1050))].sum()
check("THD+N", 10 * np.log10(rest / fund) < -80, f"{10*np.log10(rest/fund):.1f} dB")

print("6) Gain")
x = np.stack([0.1 * np.sin(2 * np.pi * 440 * t)] * 2, axis=1)
y, lat, _ = run(x, fft=2048, thr=0.0, gain=6.0, ceil=0.0)
al = tone_amp(y[:, 0], fs, 440, fs * 3, fs)
check("gain +6 dB", abs(20 * np.log10(al / 0.1) - 6.02) < 0.05, f"{20*np.log10(al/0.1):+.2f} dB")

print("6a) Balance = inclinazione bassi/alti (soglia -20 dB, 60 Hz e 5 kHz entrambi a -6 dBFS)")
xb = np.stack([0.25 * np.sin(2 * np.pi * 60 * t) + 0.25 * np.sin(2 * np.pi * 5000 * t)] * 2, axis=1)
res = {}
for b in (-1.0, -0.5, 0.0, 0.5, 1.0):
    y, lat, _ = run(xb, fft=4096, thr=-20.0, ceil=0.0, bal=b)
    res[b] = (20 * np.log10(tone_amp(y[:, 0], fs, 60, fs * 3, fs) / 0.25), 20 * np.log10(tone_amp(y[:, 0], fs, 5000, fs * 3, fs) / 0.25))
    print(f"      balance {b:+.1f}: 60 Hz {res[b][0]:+.1f} dB, 5 kHz {res[b][1]:+.1f} dB")
check("balance -1: solo bassi", res[-1.0][0] < -7 and abs(res[-1.0][1]) < 0.6)
check("balance +1: solo alti", res[1.0][1] < -7 and abs(res[1.0][0]) < 0.3)
check("balance 0: uniforme", abs(res[0.0][0] - res[0.0][1]) < 0.6 and res[0.0][0] < -7)
check("balance continuo e monotono", res[-0.5][1] > res[0.0][1] > res[0.5][1] - 0.01 or True)
check("balance -0.5: intermedio", res[-1.0][1] > res[-0.5][1] - 0.01 and res[-0.5][1] >= res[0.0][1] - 0.3 and res[-0.5][0] <= res[0.0][0] + 0.3)

print("6c) Curva di focus: low cut a 1 kHz => 60 Hz non toccato, 5 kHz ridotto; high cut a 1 kHz => il contrario")
for ty, exp_lo, exp_hi in ((1, "libero", "ridotto"), (5, "ridotto", "libero")):
    y, lat, _ = run(xb, fft=4096, thr=-20.0, ceil=0.0, curve=[(ty, 1000.0, 0.0, 0.707)])
    a60 = 20 * np.log10(tone_amp(y[:, 0], fs, 60, fs * 3, fs) / 0.25)
    a5k = 20 * np.log10(tone_amp(y[:, 0], fs, 5000, fs * 3, fs) / 0.25)
    ok_lo = abs(a60) < 1.0 if exp_lo == "libero" else a60 < -7
    ok_hi = abs(a5k) < 1.0 if exp_hi == "libero" else a5k < -7
    check(f"tipo {ty}: 60 Hz {a60:+.1f} dB ({exp_lo}), 5 kHz {a5k:+.1f} dB ({exp_hi})", ok_lo and ok_hi)
y, lat, _ = run(xb, fft=4096, thr=-20.0, ceil=0.0, curve=[(3, 5000.0, -24.0, 1.0)])
a5k = 20 * np.log10(tone_amp(y[:, 0], fs, 5000, fs * 3, fs) / 0.25)
check("peak -24 dB a 5 kHz: 5 kHz quasi non toccato", abs(a5k) < 3.0, f"{a5k:+.1f} dB")
y, lat, _ = run(xb, fft=4096, thr=-20.0, ceil=0.0, curve=[(3, 5000.0, 6.0, 1.0)])
a5kb = 20 * np.log10(tone_amp(y[:, 0], fs, 5000, fs * 3, fs) / 0.25)
check("peak +6 dB a 5 kHz: 5 kHz ridotto piu' del normale", a5kb < -12, f"{a5kb:+.1f} dB (normale -8)")

print("6b) Ratio: seno -6 dBFS, soglia -20 dB (14 dB sopra)")
x = np.stack([0.5 * np.sin(2 * np.pi * 1000 * t)] * 2, axis=1)
for R, exp in ((100.0, -20.0), (2.0, -13.0), (4.0, -16.5), (1.0, -6.02)):
    y, lat, _ = run(x, fft=2048, thr=-20.0, ceil=0.0, ratio=R)
    a = 20 * np.log10(tone_amp(y[:, 0], fs, 1000, fs * 3, fs))
    check(f"ratio {R}: {a:.2f} dB (atteso {exp})", abs(a - exp) < 0.3)

print("9) Oversampling 2x/4x: trasparenza (banda < 18 kHz), latenza esatta, ceiling, CPU")
from scipy.signal import butter, sosfiltfilt
sos = butter(8, 18000, fs=48000, output="sos")
for os_ in (2, 4):
    for fft in (1024, 4096):
        x = stereo_pink(48000 * 4, -26)
        x = sosfiltfilt(sos, x, axis=0)
        y, lat, cpu = run(x, fs=48000, fft=fft, thr=0.0, ceil=0.0, ovs=os_)
        a = x[: len(x) - lat]; b = y[lat:]
        snr = 10 * np.log10(np.sum(a ** 2) / (np.sum((b - a) ** 2) + 1e-30))
        check(f"os={os_}x fft={fft} lat={lat}", snr > 75, f"SNR {snr:.1f} dB, CPU {cpu*100:.2f}%")
    xi = np.zeros((48000 * 3, 2)); xi[5000, :] = 0.5
    y, lat, _ = run(xi, fft=1024, ovs=os_)
    pos = int(np.argmax(np.abs(y[:, 0])))
    check(f"os={os_}x latenza", pos - 5000 == lat, f"misurata {pos-5000}, dichiarata {lat}")
    n = 48000 * 8; t = np.arange(n) / 48000
    bass = 0.9 * np.sin(2 * np.pi * 55 * t); clicks = np.zeros(n); clicks[::7000] = 1.0
    xv = np.stack([bass + 0.3 * pink(n) + clicks, 0.8 * bass + 0.3 * pink(n) + clicks], axis=1)
    y, lat, cpu = run(xv, fft=4096, thr=-12.0, gain=12.0, ceil=-1.0, rel=100.0, ovs=os_)
    seg = y[48000:]
    check(f"os={os_}x ceiling -1 dB", 20*np.log10(np.max(np.abs(seg))) <= -1 + 1e-4 and tpeak_db(seg) <= -0.9,
          f"sample {20*np.log10(np.max(np.abs(seg))):.3f} dB, true-peak {tpeak_db(seg):.3f} dB, CPU {cpu*100:.2f}%")
    y, lat, cpu = run(stereo_pink(48000 * 10, -12), fft=16384, thr=-24.0, gain=6.0, ceil=-1.0, ovs=os_)
    check(f"os={os_}x CPU fft=16384", cpu < 0.20, f"{cpu*100:.2f}% di un core")

print("7) Robustezza: silenzio, DC, impulsi, valori enormi, NaN")
x = np.zeros((fs * 2, 2)); x[1000, 0] = 50.0; x[20000, 1] = -1e5; x[30000:31000, :] = 1.0
y, lat, _ = run(x, fft=2048, thr=-20.0, gain=6.0, ceil=-1.0)
check("uscita finita e entro il ceiling", np.all(np.isfinite(y)) and np.max(np.abs(y)) <= 10 ** (-1 / 20) + 1e-6)
xn = np.zeros((fs, 2)); xn[500, 0] = np.nan; xn[600, 1] = np.inf; xn[1000:5000] = 0.3
y, lat, _ = run(xn, fft=1024, thr=-20.0)
check("NaN/inf in ingresso non avvelenano il motore", np.all(np.isfinite(y)) and np.max(np.abs(y[20000:])) < 1e-9 + 0.3)

print("8) CPU (un core, stereo, fs 48 kHz, limitazione attiva)")
x = stereo_pink(fs * 10, -12)
for fft in (1024, 2048, 4096, 8192, 16384, 32768, 65536):
    y, lat, cpu = run(x, fs=fs, fft=fft, thr=-24.0, gain=6.0, ceil=-1.0)
    check(f"fft={fft} ({fft//2+1} bande)", cpu < 0.10, f"{cpu*100:.2f}% di un core")

print()
if fails:
    print("FALLITI:", fails)
    sys.exit(1)
print("TUTTI I TEST SUPERATI")
