"""Features of one drum hit (a reference one-shot or a rendered PowerDrum sound) and a distance between two of them.

All levels are relative to the hit itself (normalised by its peak): shape, balance and density, never loudness.
  env     energy in log-spaced time windows after the onset (0-2.5, 2.5-5, 5-10 ... 1280-2560 ms), dB re the loudest
  att     octave bands 31 Hz..16 kHz in the first 20 ms (dB re their total)      - the strike
  body    the same 20-200 ms                                                       - the body
  tail    the same 200 ms to the end (only if the hit lasts)                      - the ring
  tonal   peakiness of the tail from 40 ms: the loudest 1/12-octave band over the median of its two octaves (dB).
          A held pitch (a synth note) stands 25-45 dB out; noise and metal stay lower (Roman: drums, not synths)
  flat    spectral flatness 1-12 kHz over the body (dB): noise about -5, tone about -30
  side    side / mid in 150-1k, 1k-5k, 5k-16k (dB, -40..0)
  len     seconds to -60 dB
  crest   peak / RMS of the first 150 ms (dB)
Only these numbers are kept from the references; no reference audio ends up in PowerDrum."""
import numpy as np

WIN_MS = [0, 2.5, 5, 10, 20, 40, 80, 160, 320, 640, 1280, 2560]
OCT = [31.25 * 2 ** k for k in range(10)]          # band centres 31 Hz .. 16 kHz
SIDE_BANDS = [(150, 1000), (1000, 5000), (5000, 16000)]


def _bands(seg, sr):
    if len(seg) < 64:
        return np.full(len(OCT), -80.0)
    n = max(4096, 1 << int(np.ceil(np.log2(len(seg)))))
    P = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), n)) ** 2
    f = np.fft.rfftfreq(n, 1 / sr)
    e = np.array([np.sum(P[(f >= c / np.sqrt(2)) & (f < c * np.sqrt(2))]) for c in OCT]) + 1e-20
    return np.maximum(10 * np.log10(e / np.sum(e)), -80.0)


def tonality(seg, sr):
    if len(seg) < 2048:
        return 0.0
    seg = seg[:16384]
    n = 16384
    P = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), n)) ** 2 + 1e-24
    f = np.fft.rfftfreq(n, 1 / sr)
    edges = 150.0 * 2 ** (np.arange(0, 12 * np.log2(12000 / 150.0) + 1) / 12.0)
    idx = np.searchsorted(f, edges)
    b = np.array([10 * np.log10(np.sum(P[idx[i]:max(idx[i + 1], idx[i] + 1)])) for i in range(len(idx) - 1)])
    worst = 0.0
    for i in range(12, len(b) - 12):
        worst = max(worst, b[i] - np.median(b[i - 12:i + 13]))
    return float(worst)


def features(x, sr):
    """x: (n, 2) float. Returns None for silence."""
    mid = x.mean(axis=1)
    side = (x[:, 0] - x[:, 1]) / 2
    pk = np.max(np.abs(mid)) + 1e-12
    if pk < 1e-5:
        return None
    a = np.abs(mid)
    onset = int(np.argmax(a > pk * 0.05))
    mid, side = mid[onset:] / pk, side[onset:] / pk
    hop = max(1, int(0.0025 * sr))
    e2 = mid ** 2
    env = []
    for lo, hi in zip(WIN_MS[:-1], WIN_MS[1:]):
        s, t = int(lo / 1000 * sr), int(hi / 1000 * sr)
        seg = e2[s:t]
        env.append(10 * np.log10(np.mean(seg) + 1e-12) if len(seg) else -120.0)
    env = np.array(env)
    env = np.maximum(env - np.max(env), -80.0)
    # length to -60 dB of the peak (2.5 ms frames)
    fr = np.array([np.max(np.abs(mid[i:i + hop])) for i in range(0, len(mid) - hop, hop)]) if len(mid) > hop else np.array([1.0])
    above = np.where(fr > 1e-3)[0]
    length = (above[-1] + 1) * hop / sr if len(above) else 0.0
    i20, i200 = int(0.02 * sr), int(0.2 * sr)
    att = _bands(mid[:i20], sr)
    body = _bands(mid[i20:i200], sr) if len(mid) > i20 + 256 else np.full(len(OCT), -80.0)
    end = int(min(len(mid), length * sr + hop))
    tail = _bands(mid[i200:end], sr) if end > i200 + 2048 else np.full(len(OCT), -80.0)
    tonal = tonality(mid[int(0.04 * sr): end], sr) if end > int(0.04 * sr) + 2048 else 0.0
    # flatness 1-12 kHz over the first 200 ms
    seg = mid[:i200]
    n = 8192
    Q = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), n)) ** 2 + 1e-20
    ff = np.fft.rfftfreq(n, 1 / sr)
    q = Q[(ff >= 1000) & (ff < 12000)]
    flat = float(10 * np.log10(np.exp(np.mean(np.log(q))) / np.mean(q)))
    seg_s = side[:end]
    seg_m = mid[:end]
    st = []
    if len(seg_m) > 256:
        nn = max(4096, 1 << int(np.ceil(np.log2(len(seg_m)))))
        M = np.abs(np.fft.rfft(seg_m, nn)) ** 2
        S = np.abs(np.fft.rfft(seg_s, nn)) ** 2
        f = np.fft.rfftfreq(nn, 1 / sr)
        for lo, hi in SIDE_BANDS:
            mm = (f >= lo) & (f < hi)
            st.append(np.clip(10 * np.log10((np.sum(S[mm]) + 1e-30) / (np.sum(M[mm]) + 1e-30)), -40, 0))
    else:
        st = [-40.0] * 3
    i150 = int(0.15 * sr)
    crest = float(20 * np.log10(1.0 / (np.sqrt(np.mean(mid[:i150] ** 2)) + 1e-12)))
    # centroid of the body (for picking low / high percussion)
    Pb = np.abs(np.fft.rfft(mid[:i200] * np.hanning(len(mid[:i200])), 8192)) ** 2
    fb = np.fft.rfftfreq(8192, 1 / sr)
    cent = float(np.sum(Pb * fb) / (np.sum(Pb) + 1e-20))
    return {"env": env, "att": att, "body": body, "tail": tail, "tonal": tonal, "flat": flat, "side": np.array(st),
            "len": float(length), "crest": crest, "cent": cent}


def distance(a, b, detail=False):
    """a: candidate (render), b: target (reference). Tonality is only penalised where the candidate is MORE tonal
    than its reference (a synth-like ring), not less."""
    act = (a["env"] > -70) | (b["env"] > -70)
    envd = float(np.mean(np.abs(a["env"][act] - b["env"][act]))) if np.any(act) else 0.0
    def bandd(x, y):
        m = (x > -70) | (y > -70)
        return float(np.mean(np.abs(x[m] - y[m]))) if np.any(m) else 0.0
    tail_w = 1.0 if b["len"] > 0.3 else 0.3
    parts = {
        "env": envd,
        "att": bandd(a["att"], b["att"]),
        "body": bandd(a["body"], b["body"]),
        "tail": bandd(a["tail"], b["tail"]) * tail_w,
        "tonal": max(0.0, a["tonal"] - b["tonal"] - 2.0),
        "flat": abs(a["flat"] - b["flat"]) / 2.0,
        "side": float(np.mean(np.abs(a["side"] - b["side"]))),
        "len": 12.0 * abs(np.log2(max(a["len"], 0.005) / max(b["len"], 0.005))),
        "crest": abs(a["crest"] - b["crest"]),
    }
    import os
    w = {"env": 1.0, "att": 0.6, "body": 0.8, "tail": 0.5, "tonal": float(os.environ.get("DRUM_TONAL_W", "1.2")), "flat": 0.4,
         "side": 0.3, "len": 0.6, "crest": 0.4}
    total = sum(w[k] * v for k, v in parts.items())
    return (total, parts) if detail else total
