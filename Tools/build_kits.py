"""The factory kits of PowerDrum (Roman, 2026-11-12: "at least 20 ready, meaningful kits, not random sounds").

Every kit is built around ONE pack of the references (one producer's sound): its kick, snare, clap, hats, percussion,
cymbals, FX; a role the pack has no one-shot for comes from a pack of the same style (listed in FALLBACK). Within a
pack the kits take different references (k-medoids of each role), so two kits of a pack do not repeat each other.
The mix is set by role and style (levels, a little pan for percussion, sends, choke of the hats, glue and master drive).

  python3 build_kits.py plan                  Референсы/kit_plan.json   (kit -> slot -> reference)
  python3 fit_drums.py fit Референсы/kit_plan.json Референсы/kit_fit.json
  python3 build_kits.py build                 Source/FactoryKits.inc (VOLUME 0 dB)
  powerdrum_tests --kit-peaks > peaks.txt;  python3 build_kits.py volume peaks.txt   (MAIN peak of the groove -> -1 dBFS)"""
import json, os, pickle, random, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
if os.environ.get("PK_NUMPY_PATH"):
    sys.path.insert(0, os.environ["PK_NUMPY_PATH"])
import numpy as np

REFS = os.path.join(HERE, "..", "Референсы", "drum_features.pkl")
PLAN = os.path.join(HERE, "..", "Референсы", "kit_plan.json")
FIT = os.path.join(HERE, "..", "Референсы", "kit_fit.json")
INC = os.path.join(HERE, "..", "Source", "FactoryKits.inc")
TYPES = ["kick", "snare", "clap", "hat", "cymbal", "tom", "perc", "rumble", "fx"]

# slot -> role (General MIDI layout of the plug-in: C1 kick ... D#2 ride)
SLOTS = ["kick", "perc_short", "snare", "clap", "rumble", "perc_low", "hat_c", "perc", "hat_c2", "perc2", "hat_o", "perc_high",
         "fx", "crash", "clap2", "ride"]
REF_ROLE = {"perc_short": "perc", "perc_low": "perc", "perc2": "perc", "perc_high": "perc", "hat_c2": "hat_c", "clap2": "clap"}
FIT_ROLE = {"perc_low": "perc_low", "clap2": "clap", "hat_c2": "hat_c", "perc_short": "perc", "perc2": "perc", "perc_high": "perc"}

STYLE = {
    "Schranz":     dict(revSize=0.35, revDamp=0.55, dlyTime=3, dlyFb=0.35, glue=0.45, mDrive=0.3),
    "Hard Techno": dict(revSize=0.45, revDamp=0.5, dlyTime=2, dlyFb=0.3, glue=0.35, mDrive=0.2),
    "Industrial":  dict(revSize=0.65, revDamp=0.4, dlyTime=1, dlyFb=0.4, glue=0.4, mDrive=0.35),
}
FALLBACK = {"Schranz": [37, 40, 2, 18, 28, 7], "Hard Techno": [7, 28, 18, 2, 40, 29], "Industrial": [29, 4, 7, 28, 40, 37]}

# (name, style, group, home pack, kick pack or None)
KITS = [
    ("Signal Schranz", "Schranz", "Signature", 37, None),
    ("Steel Pressure", "Schranz", "Signature", 37, None),
    ("Night Shift Schranz", "Schranz", "Signature", 37, None),
    ("Modern Schranz", "Schranz", "Modern", 40, None),
    ("Rumble Engine", "Schranz", "Modern", 40, None),
    ("Pulse Factory", "Schranz", "Modern", 40, None),
    ("Classic Schranz", "Schranz", "Classic", 2, 11),
    ("Old School Tools", "Schranz", "Classic", 2, 11),
    ("Schranz Vol. One", "Schranz", "Classic", 2, None),
    ("Loop Cutter", "Schranz", "Classic", 2, None),
    ("Second Wave", "Schranz", "Driving", 18, None),
    ("Rolling Schranz", "Schranz", "Driving", 18, None),
    ("Twenty Years Hard", "Hard Techno", "Signature", 7, None),
    ("Dirty Lofi Hard", "Hard Techno", "Lo-fi", 7, None),
    ("Warehouse Pressure", "Hard Techno", "Signature", 7, None),
    ("Hard Groove Tools", "Hard Techno", "Groove", 28, None),
    ("Driving Machine", "Hard Techno", "Groove", 28, None),
    ("Bunker Hard", "Hard Techno", "Groove", 28, None),
    ("Core Rumble", "Hard Techno", "Rumble", 7, 3),
    ("Dark Factory", "Industrial", "Dark", 29, None),
    ("Iron Mill", "Industrial", "Dark", 29, None),
    ("Concrete Dust", "Industrial", "Dark", 29, None),
    ("Rust Belt", "Industrial", "Raw", 4, None),
    ("Machine Hall", "Industrial", "Raw", 4, None),
]

# mix by slot role: level dB (after TRIM puts each hit at -1 dBFS), pan, reverb, delay
MIX = {
    "kick": (0.0, 0.0, 0.0, 0.0), "rumble": (-4.0, 0.0, 0.0, 0.0), "snare": (-5.0, 0.0, 0.12, 0.0),
    "clap": (-4.0, 0.0, 0.15, 0.0), "clap2": (-7.0, -0.15, 0.2, 0.0), "hat_c": (-11.0, 0.1, 0.0, 0.0),
    "hat_c2": (-13.0, -0.25, 0.05, 0.0), "hat_o": (-9.0, 0.05, 0.05, 0.0), "perc_short": (-10.0, 0.3, 0.08, 0.0),
    "perc_low": (-8.0, -0.2, 0.1, 0.0), "perc": (-10.0, -0.35, 0.12, 0.18), "perc2": (-10.0, 0.4, 0.15, 0.0),
    "perc_high": (-11.0, -0.45, 0.15, 0.12), "fx": (-11.0, 0.0, 0.3, 0.15), "crash": (-10.0, 0.0, 0.1, 0.0),
    "ride": (-12.0, -0.15, 0.05, 0.0),
}


def vec(f):
    return np.concatenate([f["env"], f["att"], f["body"] * 0.8, f["tail"] * 0.5, [f["tonal"], f["flat"]], f["side"] * 0.3,
                           [12 * np.log2(max(f["len"], 0.01)), f["crest"]]])


def medoids(items, k, rng):
    """k-medoids (PAM-lite) of reference dicts; returns them ordered by cluster size."""
    if len(items) <= k:
        return list(items)
    X = np.array([vec(q["feat"]) for q in items])
    D = np.abs(X[:, None, :] - X[None, :, :]).mean(axis=2)
    idx = rng.sample(range(len(items)), k)
    for _ in range(30):
        assign = np.argmin(D[:, idx], axis=1)
        new = []
        for c in range(k):
            members = np.where(assign == c)[0]
            if len(members) == 0:
                new.append(idx[c]); continue
            new.append(int(members[np.argmin(D[np.ix_(members, members)].sum(axis=1))]))
        if new == idx:
            break
        idx = new
    sizes = np.bincount(np.argmin(D[:, idx], axis=1), minlength=k)
    return [items[i] for _, i in sorted(zip(-sizes, idx))]


def plan():
    refs = pickle.load(open(REFS, "rb"))
    rng = random.Random(20261112)
    by = {}
    for q in refs:
        by.setdefault((q["pack"], q["role"]), []).append(q)
    used = set()
    pools = {}   # (pack, role) -> medoid list, consumed in order

    def take(pack, role, want=None):
        key = (pack, role)
        if key not in by:
            return None
        if key not in pools:
            items = by[key]
            pools[key] = medoids(items, min(len(items), 24), rng)
        for q in pools[key]:
            if q["rel"] in used:
                continue
            f = q["feat"]
            if want == "short" and f["len"] > 0.3: continue
            if want == "low" and f["cent"] > 900: continue
            if want == "high" and f["cent"] < 1500: continue
            if role == "kick" and f["tonal"] > 9: continue          # no singing kicks (PowerKick lesson)
            used.add(q["rel"])
            return q
        return None

    out = {}
    for ki, (name, style, group, home, kickPack) in enumerate(KITS):
        chain = [home] + [p for p in FALLBACK[style] if p != home]
        for si, slot in enumerate(SLOTS):
            role = REF_ROLE.get(slot, slot)
            want = {"perc_short": "short", "perc_low": "low", "perc_high": "high"}.get(slot)
            packs = ([kickPack] if (slot == "kick" and kickPack) else []) + chain
            if role == "rumble":
                packs = [40, 18] if style != "Hard Techno" else [18, 40]
            q = None
            for p in packs:
                q = take(p, role, want) or (take(p, role) if want else None)
                if q: break
            if q is None:
                for p in chain:   # any pack of the style, without the wish
                    q = take(p, role)
                    if q: break
            if q is None:   # last resort: the same role from any pack
                for p in sorted({k[0] for k in by}):
                    q = take(p, role)
                    if q: break
            if q is None:
                raise SystemExit("no reference for %s %s" % (name, slot))
            out["%d:%d" % (ki, si)] = {"kit": name, "slot": si, "role": FIT_ROLE.get(slot, role), "ref": q["rel"], "pack": q["pack"]}
    json.dump(out, open(PLAN, "w"), indent=1)
    from collections import Counter
    print("kits:", len(KITS), "sounds:", len(out))
    print("home pack share:", sum(1 for k, v in out.items() if v["pack"] == KITS[int(k.split(':')[0])][3]) / len(out))


# Short tag of each kit for its sounds' names ("Signal Dist Kick"): the browser lists sounds under their kit.
TAG = {"Signal Schranz": "Signal", "Steel Pressure": "Steel", "Night Shift Schranz": "Night", "Modern Schranz": "Modern",
       "Rumble Engine": "Engine", "Pulse Factory": "Pulse", "Classic Schranz": "Classic", "Old School Tools": "Oldschool",
       "Schranz Vol. One": "Vol One", "Loop Cutter": "Cutter", "Second Wave": "Wave", "Rolling Schranz": "Rolling",
       "Twenty Years Hard": "Twenty", "Dirty Lofi Hard": "Lofi", "Warehouse Pressure": "Warehouse", "Hard Groove Tools": "Groove",
       "Driving Machine": "Driving", "Bunker Hard": "Bunker", "Core Rumble": "Core", "Dark Factory": "Factory",
       "Iron Mill": "Iron", "Concrete Dust": "Concrete", "Rust Belt": "Rust", "Machine Hall": "Machine"}
NOUN = {"kick": "Kick", "perc_short": "Perc", "snare": "Snare", "clap": "Clap", "rumble": "Rumble", "perc_low": "Low Perc",
        "hat_c": "Closed Hat", "perc": "Perc", "hat_c2": "Ghost Hat", "perc2": "Perc", "hat_o": "Open Hat", "perc_high": "High Perc",
        "fx": "FX", "crash": "Crash", "clap2": "Flam Clap", "ride": "Ride"}


def sound_name(kit, slot, fit):
    """A name from what the sound IS and where it belongs (never the name of a reference file)."""
    t, m, x = fit["type"], fit["model"], fit["x"]
    desc = ""
    if t == "kick": desc = ["Hard", "Round", "Dist"][m]
    elif t == "rumble": desc = ["Comb", "Verb", "Growl"][m]
    elif t == "snare": desc = ["Tight", "Fat", "Noise"][m] if slot == "snare" else "Noise"
    elif t == "clap": desc = ("Room" if x["b"] > 0.6 else ["", "Spread", "Dense"][m]) if slot in ("clap", "clap2", "snare") else "Clap"
    elif t == "hat": desc = ["", "Bright", "Lofi"][m]
    elif t == "cymbal": desc = "China" if m == 2 else ("Washy" if slot in ("hat_c", "hat_c2", "hat_o", "fx") else "")
    elif t == "perc": desc = ["Metal", "Rim", "Wood"][m]
    elif t == "tom": desc = ["Tom", "Conga", "Click"][m]
    elif t == "fx": desc = ["Zap", "Sweep", "Impact"][m]
    noun = NOUN[slot]
    if desc == "Clap" and noun.endswith("Clap"): desc = ""
    dirty = "Dirty" if x["drive"] > 0.65 and desc != "Dist" else ""
    return " ".join(w for w in [TAG[kit], dirty, desc, noun] if w)


def build():
    refs = {q["rel"]: q for q in pickle.load(open(REFS, "rb"))}
    plan_ = json.load(open(PLAN))
    fit = json.load(open(FIT))
    lines = ["// Generated by Tools/build_kits.py from Референсы/kit_fit.json - do not edit by hand.",
             "// { kit, category, group, { 16 slot sound names }, { {param, plain value}... } }"]
    names_used = {}
    report = []
    for ki, (name, style, group, home, kickPack) in enumerate(KITS):
        vals = {}
        slot_names = []
        for si, slot in enumerate(SLOTS):
            key = "%d:%d" % (ki, si)
            p, ft = plan_[key], fit[key]
            ref = refs[p["ref"]]
            s = "s%d_" % (si + 1)
            x = ft["x"]
            vals[s + "type"] = TYPES.index(ft["type"])
            vals[s + "model"] = ft["model"]
            for k in ("tune", "decay", "tone", "a", "b", "drive"):
                vals[s + k] = round(float(x[k]), 4)
            vals[s + "driveMode"] = ft["driveMode"]
            vals[s + "trim"] = ft["trim"]
            vals[s + "width"] = round(float(x["width"]), 3)
            lvl, pan, rev, dly = MIX[slot]
            vals[s + "level"] = lvl
            vals[s + "pan"] = pan
            vals[s + "rev"] = rev
            vals[s + "dly"] = dly
            vals[s + "choke"] = 1 if slot in ("hat_c", "hat_c2", "hat_o") else 0
            nm = sound_name(name, slot, ft)
            # unique names across the library: "Dark Short Metal Perc", "Dark Short Metal Perc 2" ...
            n = names_used.get(nm, 0) + 1
            names_used[nm] = n
            slot_names.append(nm if n == 1 else "%s %d" % (nm, n))
            report.append((ft["dist"], name, slot, ft["type"], ft["tonal"], ft["ref_tonal"]))
        for k, v in STYLE[style].items():
            vals[k] = v
        vals["volume"] = 0.0
        def lit(v):   # a valid C++ float literal: 0.0f, 3.71f, -4.1414f
            s = "%.6g" % v
            return (s if ("." in s or "e" in s) else s + ".0") + "f"
        body = ", ".join('{ "%s", %s }' % (k, lit(v)) for k, v in vals.items())
        lines.append('{ "%s", "%s", "%s", { %s }, { %s } },' % (name, style, group, ", ".join('"%s"' % n for n in slot_names), body))
    open(INC, "w").write("\n".join(lines) + "\n")
    report.sort()
    d = np.array([r[0] for r in report])
    print("kits:", len(KITS), "sounds:", len(report), "distance median %.2f p90 %.2f max %.2f" % (np.median(d), np.percentile(d, 90), d.max()))
    worse = [r for r in report if r[4] > r[5] + 6]
    print("sounds more tonal than their reference by > 6 dB:", len(worse))
    for r in worse[:10]:
        print("  ", r)


def volume(peaks_file):
    """peaks: lines 'kit name|true peak dBTP' measured at the kit's current VOLUME -> VOLUME moved by the difference to
    -1 dBTP (run twice: a peak above the emergency ceiling reads low the first time)."""
    peaks = {}
    for line in open(peaks_file):
        if "|" in line:
            n, v = line.rsplit("|", 1)
            peaks[n.strip()] = float(v)
    src = open(INC).read().split("\n")
    out = []
    for line in src:
        if line.startswith('{ "'):
            name = line.split('"')[1]
            if name in peaks:
                import re
                m = re.search(r'\{ "volume", (-?[0-9.]+)f \}', line)
                old = float(m.group(1))
                vol = round(old + (-1.0 - peaks[name]), 1)
                line = line.replace(m.group(0), '{ "volume", %.1ff }' % vol)
        out.append(line)
    open(INC, "w").write("\n".join(out))
    print("volumes set for", len(peaks), "kits")


if __name__ == "__main__":
    {"plan": lambda: plan(), "build": lambda: build(), "volume": lambda: volume(sys.argv[2])}[sys.argv[1]]()
