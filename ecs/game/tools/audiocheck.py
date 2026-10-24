#!/usr/bin/env python3
"""Level, silence and clipping of a recorded wav (and its loudest frequencies per 5 s): audiocheck.py file.wav"""
import sys, wave, numpy as np
w = wave.open(sys.argv[1]); n = w.getnframes(); ch = w.getnchannels(); sr = w.getframerate()
x = np.frombuffer(w.readframes(n), dtype='<i2').reshape(-1, ch).astype(np.float64) / 32768
m = x.mean(axis=1)
print('%s: %.1f s, %d Hz, %d ch, peak %.2f, rms %.3f, dc %.4f, clipped samples %d, silent seconds %d of %d' % (sys.argv[1].split('/')[-1], n / sr, sr, ch, np.abs(x).max(), np.sqrt((m**2).mean()), m.mean(), int((np.abs(x) > .99).sum()), sum(1 for i in range(int(n / sr)) if np.abs(m[i*sr:(i+1)*sr]).max() < .005), int(n / sr)))
for t in range(0, int(n / sr) - 4, 5):
    seg = m[t*sr:(t+5)*sr] * np.hanning(5*sr); sp = np.abs(np.fft.rfft(seg)); f = np.fft.rfftfreq(len(seg), 1/sr)
    top = np.argsort(sp[f > 40])[-3:][::-1] + int((f <= 40).sum())
    print('  %2d-%2d s: rms %.3f  peaks %s Hz' % (t, t + 5, np.sqrt((m[t*sr:(t+5)*sr]**2).mean()), ' '.join('%d' % f[i] for i in top)))
