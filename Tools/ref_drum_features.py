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
    if SKIP_PATH.search(os.path.dirname(rel)):
        return None
    d = os.path.basename(os.path.dirname(rel)).lower()   # the file's own folder decides
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
    m = re.match(r"TSOHT #(\d+)", rel)
    return int(m.group(1)) if m else 0


def measure(args):
    path, rel, role = args
    try:
        x, sr = read_wav(path)
    except Exception as e:
        return None
    if len(x) > sr * 8 or len(x) < sr * 0.005:
        return None
    f = features(x.astype(np.float64), sr)
    if f is None:
        return None
    r = role
    if r == "hat":
        r = "hat_c" if f["len"] < 0.25 else "hat_o"
    if r == "ride" and ("crash" in os.path.basename(rel).lower() or "cym" in os.path.basename(rel).lower()) and "ride" not in os.path.basename(rel).lower():
        r = "crash"
    return {"rel": rel, "pack": pack_of(rel), "role": r, "feat": f, "sr": sr}


def main():
    jobs = []
    for dp, _, files in os.walk(ROOT):
        for fn in files:
            if not fn.lower().endswith(".wav"):
                continue
            path = os.path.join(dp, fn)
            rel = os.path.relpath(path, ROOT)
            role = role_of(rel)
            if role:
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
