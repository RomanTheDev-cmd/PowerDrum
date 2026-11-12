"""Fits PowerDrum sounds to reference one-shots (numbers from Референсы/drum_features.pkl; no reference audio is used).

  python3 fit_drums.py probe [type]          what the engine can reach: random sounds of each type/model vs the references
  python3 fit_drums.py fit PLAN.json OUT.json   fits every sound of a kit plan (Tools/build_kits.py plan) to its reference

A sound = TYPE (fixed by the slot role, with alternatives tried), MODEL (all three tried), TUNE, DECAY, TONE, A, B,
DRIVE, DRIVE MODE (SAT, CLIP, FOLD; CRUSH for lo-fi), WIDTH. The fit: a few random starts per model, then an adaptive
(1+1)-ES with per-parameter steps. The distance (drum_features.distance) penalises a tail MORE tonal than the
reference (Roman: drums, not synths). TRIM is set afterwards: the hit peaks at -1 dBFS at LEVEL 0 dB.
Env: PD_TESTS (render server), FIT_ITERS (220), FIT_WORKERS (9), FIT_FROM_KIT (refit: only kits from this index),
FIT_KEEP_BETTER=1 (refit with FIT_KEYS: keep the stored fit unless the new one is closer)."""
import json, math, os, pickle, random, subprocess, sys, time
from concurrent.futures import ProcessPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
if os.environ.get("PK_NUMPY_PATH"):
    sys.path.insert(0, os.environ["PK_NUMPY_PATH"])
import numpy as np
from drum_features import features, distance

TESTS = os.environ.get("PD_TESTS", os.path.expanduser("~/Library/Caches/PowerDrumBuildUniversal/powerdrum_tests_artefacts/Release/powerdrum_tests"))
REFS = os.path.join(HERE, "..", "Референсы", "drum_features.pkl")
SR = 48000
TYPES = ["kick", "snare", "clap", "hat", "cymbal", "tom", "perc", "rumble", "fx"]
# role -> engine types to try
ROLE_TYPES = {"kick": ["kick"], "rumble": ["rumble"], "snare": ["snare", "clap"], "clap": ["clap", "snare"],
              "hat_c": ["hat"], "hat_o": ["hat", "cymbal"], "ride": ["cymbal"], "crash": ["cymbal"],
              "perc": ["perc", "tom", "snare"], "perc_low": ["tom", "perc"], "fx": ["fx", "cymbal"]}
SPACE = {"tune": (-24, 24), "decay": (0, 1), "tone": (0, 1), "a": (0, 1), "b": (0, 1), "drive": (0, 1), "width": (0, 1)}
# Kicks stay kicks: register and drive limits (PowerKick: no lead-like tone, the register of the references)
LIMITS = {"kick": {"tune": (-10, 8)}, "rumble": {"tune": (-10, 8)}, "tom": {"tune": (-12, 18)}}


class Renderer:
    def __init__(self):
        self.p = subprocess.Popen([TESTS, "--serve"], stdin=subprocess.PIPE, stdout=subprocess.PIPE)

    def render(self, params, length):
        frames = int(round(length * SR))
        line = " ".join("%s=%.6g" % kv for kv in params.items()) + " _n=%d\n" % frames   # exact frames: no rounding mismatch
        self.p.stdin.write(line.encode()); self.p.stdin.flush()
        n = frames * 2 * 4
        buf = b""
        while len(buf) < n:
            chunk = self.p.stdout.read(n - len(buf))
            if not chunk:
                raise RuntimeError("render server stopped")
            buf += chunk
        return np.frombuffer(buf, dtype="<f4").reshape(-1, 2).astype(np.float64)


def bounds(etype, k):
    lo, hi = SPACE[k]
    return LIMITS.get(etype, {}).get(k, (lo, hi))


def to_engine(etype, model, x, drive_mode):
    p = {"type": TYPES.index(etype), "model": model, "driveMode": drive_mode, "trim": 0, "level": 0, "vel": 0.6}
    p.update({k: float(v) for k, v in x.items()})
    return p


def evaluate(r, etype, model, x, drive_mode, target, length):
    audio = r.render(to_engine(etype, model, x, drive_mode), length)
    f = features(audio, SR)
    if f is None:
        return 1e9, None
    return distance(f, target), f


def random_point(etype, rng):
    return {k: rng.uniform(*bounds(etype, k)) for k in SPACE}


def fit_one(args):
    """args: (key, role, target features, seed, iters). Returns the best sound found."""
    key, role, target, seed, iters = args
    rng = random.Random(seed)
    r = Renderer()
    length = float(min(max(target["len"] * 1.15 + 0.15, 0.4), 6.0))
    best = (1e9, None)
    modes = [0, 1, 2] + ([3] if target["flat"] > -6 and role in ("hat_c", "perc", "clap") else [])
    for etype in ROLE_TYPES[role]:
        for model in range(3):
            # random starts
            cand = []
            for _ in range(6):
                x = random_point(etype, rng)
                dm = rng.choice(modes)
                d, _ = evaluate(r, etype, model, x, dm, target, length)
                cand.append((d, x, dm))
            cand.sort(key=lambda c: c[0])
            d, x, dm = cand[0]
            step = {k: 0.25 * (bounds(etype, k)[1] - bounds(etype, k)[0]) for k in SPACE}
            n_it = iters // (3 * len(ROLE_TYPES[role]))
            for it in range(n_it):
                y = dict(x)
                for k in SPACE:
                    if rng.random() < 0.5:
                        lo, hi = bounds(etype, k)
                        y[k] = min(hi, max(lo, y[k] + rng.gauss(0, step[k])))
                ydm = dm if rng.random() > 0.08 else rng.choice(modes)
                dy, _ = evaluate(r, etype, model, y, ydm, target, length)
                if dy < d:
                    d, x, dm = dy, y, ydm
                    for k in SPACE: step[k] *= 1.25
                else:
                    for k in SPACE: step[k] *= 0.93
            if d < best[0]:
                best = (d, {"type": etype, "model": model, "x": x, "driveMode": dm})
    # trim: the peak of the hit at -1 dBFS at LEVEL 0
    b = best[1]
    audio = r.render(to_engine(b["type"], b["model"], b["x"], b["driveMode"]), length)
    peak = float(np.max(np.abs(audio))) + 1e-12
    trim = -1.0 - 20 * math.log10(peak)
    f = features(audio, SR)
    _, parts = distance(f, target, detail=True)
    r.p.stdin.close(); r.p.wait()
    return key, {"dist": best[0], "parts": parts, "type": b["type"], "model": b["model"], "driveMode": b["driveMode"],
                 "x": b["x"], "trim": round(trim, 2), "len": f["len"], "tonal": f["tonal"], "ref_tonal": target["tonal"]}


def probe(only=None):
    refs = pickle.load(open(REFS, "rb"))
    r = Renderer()
    rng = random.Random(1)
    for etype in TYPES:
        if only and etype != only:
            continue
        for model in range(3):
            tons, lens, cents = [], [], []
            for _ in range(40):
                x = random_point(etype, rng)
                x["drive"] = rng.uniform(0, 0.6)
                audio = r.render(to_engine(etype, model, x, rng.choice([0, 1, 2])), 2.0)
                f = features(audio, SR)
                if f is None:
                    continue
                tons.append(f["tonal"]); lens.append(f["len"]); cents.append(f["cent"])
            p = lambda a: "%.1f/%.1f/%.1f" % tuple(np.percentile(a, [10, 50, 90]))
            print("%-7s %d  tonal %s  len %s  cent %s" % (etype, model, p(tons), p(lens), p(cents)))


def main():
    mode = sys.argv[1]
    if mode == "probe":
        probe(sys.argv[2] if len(sys.argv) > 2 else None)
        return
    if mode == "refit":
        # the worst fits again, longer and from more starts: python3 fit_drums.py refit PLAN FIT [threshold]
        plan = json.load(open(sys.argv[2]))
        out_path = sys.argv[3]
        thr = float(sys.argv[4]) if len(sys.argv) > 4 else 26.0
        refs = {q["rel"]: q for q in pickle.load(open(REFS, "rb"))}
        done = json.load(open(out_path))
        iters = int(os.environ.get("FIT_ITERS", "1800"))
        only = set(os.environ.get("FIT_KEYS", "").split()) - {""}
        from_kit = int(os.environ.get("FIT_FROM_KIT", "0"))   # only the kits from this index (new kits, old ones as they are)
        jobs = [(k, plan[k]["role"], refs[plan[k]["ref"]]["feat"], (hash(k) + 7) & 0xffff, iters) for k, v in done.items()
                if int(k.split(":")[0]) >= from_kit and (k in only if only else v["dist"] > thr)]
        print("refitting", len(jobs), "sounds above", thr, flush=True)
        from concurrent.futures import as_completed
        better = 0
        with ProcessPoolExecutor(int(os.environ.get("FIT_WORKERS", "9"))) as ex:
            futures = [ex.submit(fit_one, j) for j in jobs]
            for fut in as_completed(futures):
                key, res = fut.result()
                # FIT_KEYS (after an engine change) always takes the new fit: the stored one was measured on the old engine
                keep_better = os.environ.get("FIT_KEEP_BETTER") == "1"   # FIT_KEYS, but only an improvement replaces
                if (only and not keep_better) or res["dist"] < done[key]["dist"]:
                    done[key] = res; better += 1
                json.dump(done, open(out_path, "w"), indent=1)
        print("improved", better, "of", len(jobs), flush=True)
        return
    if mode == "fit":
        plan = json.load(open(sys.argv[2]))
        out_path = sys.argv[3]
        refs = {q["rel"]: q for q in pickle.load(open(REFS, "rb"))}
        done = json.load(open(out_path)) if os.path.exists(out_path) else {}
        iters = int(os.environ.get("FIT_ITERS", "220"))
        jobs = []
        for key, item in plan.items():
            if key in done:
                continue
            jobs.append((key, item["role"], refs[item["ref"]]["feat"], hash(key) & 0xffff, iters))
        print("sounds to fit:", len(jobs), "(already:", len(done), ")")
        t0 = time.time()
        from concurrent.futures import as_completed
        with ProcessPoolExecutor(int(os.environ.get("FIT_WORKERS", "9"))) as ex:
            futures = [ex.submit(fit_one, j) for j in jobs]
            for i, fut in enumerate(as_completed(futures)):   # saved as they finish, in any order
                key, res = fut.result()
                done[key] = res
                if i % 10 == 9 or i == len(jobs) - 1:
                    json.dump(done, open(out_path, "w"), indent=1)
                    print("%d/%d  %.0f s  last %s d=%.2f tonal %.1f (ref %.1f)" % (i + 1, len(jobs), time.time() - t0, key, res["dist"], res["tonal"], res["ref_tonal"]))
        json.dump(done, open(out_path, "w"), indent=1)


if __name__ == "__main__":
    main()
