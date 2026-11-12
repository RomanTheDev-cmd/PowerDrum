"""Measures the drum one-shots of the TSOHT packs (iCloud definitionofhardtechno) into Референсы/drum_features.pkl:
for every hit its pack, its role (kick, rumble, snare, clap, hat_c, hat_o, ride, crash, perc, fx) and the numbers
of drum_features.features(). Loops, synths, vocals and demo folders of other packs are skipped. No audio is copied.
  python3 -I ref_drum_features.py [root]"""
import os, pickle, re, sys
from concurrent.futures import ProcessPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
if os.environ.get("PK_NUMPY_PATH"):
    sys.path.insert(0, os.environ["PK_NUMPY_PATH"])
import numpy as np
from drum_features import features
from wavread import read_wav

ROOT = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser("~/Library/Mobile Documents/com~apple~CloudDocs/definitionofhardtechno")
OUT = os.path.join(HERE, "..", "Референсы", "drum_features.pkl")
SKIP_PATH = re.compile(r"loop|demo|midi|ableton", re.I)          # anywhere in the path: other packs' demos, loops
SKIP_DIR = re.compile(r"synth|vocal|bassline|bass loop|drives|rolls|builder shots|theme|sequence|riser|texture|atmo shots|"
                      r"screech|pluck|abstract|bell|transition|crash fx|fx loops", re.I)   # the folder of the file itself


def role_of(rel):
    d = os.path.basename(os.path.dirname(rel)).lower()   # the file's own folder decides
    if d == "htx shots":          # the one-shots of TSOHT #1 (Hard & Dark Industrial) in the demo folder of #4: industrial
        return "perc"             # kit shots (crushed, harsh, disto), the only material of that pack here
    if SKIP_PATH.search(os.path.dirname(rel)):
        return None
    if d.endswith("atmo & fx"):   # #4 has no FX shots: its atmo FX, the noisy ones only (filtered in measure())
        return "fx_atmo"
    if SKIP_DIR.search(d):
        return None
    if "rumble" in d and ("builder" in d or "rumble shots" in d):
        return "rumble"
    if "kick" in d:
        return "kick"
    if "snare" in d:
        return "snare"
    if "clap" in d:
        return "clap"
    if "crash" in d:
        return "crash"
    if "closed" in d or "lofi hats" in d:
        return "hat_c"
    if "open" in d:
        return "hat_o"
    if "hats" in d or "hi-hat" in d:
        return "hat"          # mixed: decided by length below
    if "ride" in d or "cymbal" in d:
        return "ride"         # "Rides & Cymbals": decided below
    if "impact" in d or "fx shots" in d or "white noise" in d:
        return "fx"
    if re.search(r"perc|rhythm shots|metallic|lofi shots|signature shots|schranz tools", d):
        return "perc"
    if d in ("kicks", "simple kicks", "complex kicks", "compressed complex kicks"):
        return "kick"
    return None


def pack_of(rel):
    m = re.findall(r"TSOHT #(\d+)", rel)      # the innermost pack: a demo of #1 inside the folder of #4 is #1
    return int(m[-1]) if m else 0


def one_hit(x, sr):
    """A two-bar phrase of one repeated hit (the "Bonus Schranz Tools" of #4 at 150 BPM) -> its most isolated loud hit:
    onsets = a 9 dB rise within 15 ms above -30 dB; of the hits with 200 ms or more before the next one and a peak
    within 2 dB of the loudest, the one with the longest gap; cut 3 ms before the next onset, 10 ms fade out."""
    m = np.abs(x.mean(axis=1))
    hop = int(0.005 * sr)
    e = np.array([m[i:i + hop].max() for i in range(0, len(m) - hop, hop)]) + 1e-9
    db = 20 * np.log10(e / e.max())
    ons = []
    for i in range(3, len(db)):
        if db[i] > -30 and db[i] - db[i - 3] > 9 and (not ons or i - ons[-1] > 10):
            ons.append(i)
    starts = [max(0, i - 3) * hop for i in ons]
    segs = [(a, b) for a, b in zip(starts, starts[1:] + [len(x)]) if b - a >= 0.2 * sr]
    if not segs:
        return None, None
    pk = {s: np.abs(x[s[0]:s[1]]).max() for s in segs}
    loud = [s for s in segs if pk[s] >= max(pk.values()) * 10 ** (-2 / 20)]
    a, b = max(loud, key=lambda s: (s[1] - s[0], pk[s]))
    seg = x[a:b - int(0.003 * sr)].copy()
    fade = min(len(seg) // 4, int(0.01 * sr))
    seg[-fade:] *= np.linspace(1, 0, fade)[:, None]
    return seg, (int(a * 1000 / sr), int(b * 1000 / sr))


def measure(args):
    path, rel, role = args
    try:
        x, sr = read_wav(path)
    except Exception as e:
        return None
    if len(x) > sr * 8 or len(x) < sr * 0.005:
        return None
    x = x.astype(np.float64)
    cut = None
    if "bonus schranz tools" in os.path.basename(os.path.dirname(rel)).lower():
        hit, cut = one_hit(x, sr)
        if hit is not None:   # no isolated hit (a dense roll): the phrase as it is, as before
            x = hit
    f = features(x, sr)
    if f is None:
        return None
    r = role
    if r == "fx_atmo":            # drums, not synths: a held tone or a drone is no FX shot for PowerDrum
        if f["tonal"] > 12:
            return None
        r = "fx"
    if r == "hat":
        r = "hat_c" if f["len"] < 0.25 else "hat_o"
    if r == "ride" and ("crash" in os.path.basename(rel).lower() or "cym" in os.path.basename(rel).lower()) and "ride" not in os.path.basename(rel).lower():
        r = "crash"
    q = {"rel": rel, "pack": pack_of(rel), "role": r, "feat": f, "sr": sr}
    if cut:
        q["cut_ms"] = cut   # the one hit taken from the phrase
    return q


def main():
    jobs, seen = [], set()
    for dp, _, files in os.walk(ROOT):
        for fn in files:
            if not fn.lower().endswith(".wav"):
                continue
            path = os.path.join(dp, fn)
            rel = os.path.relpath(path, ROOT)
            role = role_of(rel)
            same = (pack_of(rel), fn, os.path.getsize(path))   # the HTX demo shots ship twice (in #4 and in #11)
            if role and same not in seen:
                seen.add(same)
                jobs.append((path, rel, role))
    print("hits to measure:", len(jobs))
    with ProcessPoolExecutor(9) as ex:
        res = [r for r in ex.map(measure, jobs, chunksize=8) if r]
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "wb") as f:
        pickle.dump(res, f)
    from collections import Counter
    print("measured:", len(res))
    print(sorted(Counter(r["role"] for r in res).items()))
    print(sorted(Counter((r["pack"], r["role"]) for r in res).items()))


if __name__ == "__main__":
    main()
