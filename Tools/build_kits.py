"""The factory kits of PowerDrum (Roman, 2026-11-12: "at least 20 ready, meaningful kits, not random sounds").

Every kit is built around ONE pack of the references (one producer's sound): its kick, snare, clap, hats, percussion,
cymbals, FX; a role the pack has no one-shot for comes from a pack of the same style (listed in FALLBACK). Within a
pack the kits take different references (k-medoids of each role), so two kits of a pack do not repeat each other.
The mix is set by role and style (levels, a little pan for percussion, sends, choke of the hats, glue and master drive).

  python3 build_kits.py plan                  Референсы/kit_plan.json   (kit -> slot -> reference; the kits already
                                              planned there keep their references, only new kits are planned)
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
FALLBACK = {"Schranz": [37, 40, 2, 18, 28, 7], "Hard Techno": [7, 28, 18, 2, 40, 29], "Industrial": [29, 4, 1, 7, 28, 40, 37]}
# (pack 1 joined the Industrial chain in 1.1.0; the 1.0.0 kits keep the references they were planned with)
ALT_ROLE = {"crash": "ride", "ride": "crash"}
# The longest hit the engine makes per role (fitted lengths of 1.0.0: perc 1.2 s, tom and clap 1.5, snare 1.0): a longer
# reference (the 3.2 s "Bonus Schranz Tools" of #4 are two-bar phrases at 150 BPM) cannot be matched - kits of 1.1.0 skip them
REACH = {"perc": 1.5, "clap": 1.5, "snare": 1.0, "hat_c": 0.9}   # the few crash one-shots run out: a ride (fitted as a cymbal) instead

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
    # 1.1.0 (Roman: "more industrial kits"): ten more, each with its own idea (the group) on top of its pack.
    # Options: kick_dir - the kick from that folder of the pack (heavy, distorted, rumble, processed FX kicks).
    ("Black Furnace", "Industrial", "Rumble", 29, None, {"kick_dir": "Heavy Kick"}),
    ("Ash Ritual", "Industrial", "Ritual", 29, None, {"kick_dir": "Heavy Kick"}),
    ("Steel Foundry", "Industrial", "Metal", 29, None, {"kick_dir": "Assorted Kick"}),
    ("Impact Chamber", "Industrial", "Processed", 29, None, {"kick_dir": "Processed FX-Kick"}),
    ("Static Grind", "Industrial", "Noise", 29, None, {}),
    ("Tunnel Rumble", "Industrial", "Rumble", 4, None, {"kick_dir": "Rumble Kick"}),
    ("Distortion Plant", "Industrial", "Noise", 4, None, {"kick_dir": "Distorted Kick"}),
    ("Scrap Yard", "Industrial", "Metal", 4, None, {"kick_dir": "Heavy Kick"}),
    ("Crusher", "Industrial", "Noise", 1, None, {}),   # around the crushed / harsh kit shots of TSOHT #1
    ("Smoke Ritual", "Industrial", "Ritual", 4, None, {}),
]

# The idea of a group (new industrial groups only; the groups of 1.0.0 keep their kits as they were):
#   which percussion it picks, how the slots are fitted, its room and master, its mix.
GROUP_WISH = {"Ritual": {"perc": "low", "perc2": "low"},          # toms and low hits, a big room
              "Metal": {"perc": "high", "perc2": "high"},         # bright metal, a bright long room
              "Noise": {"perc": "noisy", "perc2": "noisy"}}       # noisy, crushed hits, more master drive
GROUP_FIT_ROLE = {"Ritual": {"perc": "perc_low", "perc2": "perc_low"}}   # toms first
GROUP_STYLE = {
    ("Industrial", "Rumble"): dict(revSize=0.6, revDamp=0.5, glue=0.45, mDrive=0.4),
    ("Industrial", "Ritual"): dict(revSize=0.8, revDamp=0.45, dlyTime=1, dlyFb=0.45, glue=0.35, mDrive=0.3),
    ("Industrial", "Metal"): dict(revSize=0.7, revDamp=0.25, dlyTime=3, dlyFb=0.35),
    ("Industrial", "Processed"): dict(revSize=0.6, revDamp=0.4, dlyTime=4, dlyFb=0.4),
    ("Industrial", "Noise"): dict(revSize=0.5, revDamp=0.5, glue=0.5, mDrive=0.5),
}
PERC_SLOTS = ("perc_short", "perc_low", "perc", "perc2", "perc_high")


def kit_opts(kit):
    return kit[5] if len(kit) > 5 else {}


def mix_of(slot, style, group):
    """(level, pan, reverb, delay) of a slot: MIX by role, changed by the idea of a new industrial group."""
    lvl, pan, rev, dly = MIX[slot]
    g = group if (style, group) in GROUP_STYLE else None
    if g == "Rumble" and slot == "rumble":
        lvl, rev = -2.0, 0.05
    elif g == "Ritual" and slot in PERC_SLOTS:
        lvl, pan, rev = lvl + 1.0, max(-0.6, min(0.6, pan * 1.3)), min(0.35, rev * 1.6 + 0.04)
    elif g == "Metal" and slot in ("perc", "perc2", "perc_high", "ride"):
        rev = rev + 0.08
        if slot == "ride": lvl = -10.0
    elif g == "Processed" and slot == "fx":
        lvl, rev, dly = -9.0, 0.35, 0.2
    elif g == "Noise" and slot in ("perc", "perc2"):
        dly = 0.2
    return lvl, pan, rev, dly

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
    # kits planned before (same index, same name) keep their references: a new kit never changes an old one
    old = json.load(open(PLAN)) if os.path.exists(PLAN) else {}
    keep = {k: v for k, v in old.items() if int(k.split(":")[0]) < len(KITS) and v["kit"] == KITS[int(k.split(":")[0])][0]}
    used = {v["ref"] for v in keep.values()}
    pools = {}   # (pack, role) -> the unused references, the most different first (k-medoids), then the rest
    mode = {"strict": False}   # True for the kits of 1.1.0 on (the 1.0.0 kits are kept as planned)

    def take(pack, role, want=None, kick_dir=None):
        key = (pack, role)
        if key not in by:
            return None
        if key not in pools:
            items = [q for q in by[key] if q["rel"] not in used]
            first = medoids(items, min(len(items), 24), rng)
            ids = {q["rel"] for q in first}
            pools[key] = first + [q for q in items if q["rel"] not in ids]
        for q in pools[key]:
            if q["rel"] in used:
                continue
            f = q["feat"]
            if want == "short" and f["len"] > 0.3: continue
            if want == "low" and f["cent"] > 900: continue
            if want == "high" and f["cent"] < 1500: continue
            if want == "noisy" and f["flat"] < -8: continue
            if kick_dir and kick_dir not in os.path.dirname(q["rel"]).split("/")[-1]: continue
            if role == "kick" and f["tonal"] > 9: continue          # no singing kicks (PowerKick lesson)
            if mode["strict"] and f["len"] > REACH.get(role, 99): continue   # the engine cannot make it that long (a phrase, not a hit)
            if mode["strict"] and role != "kick" and f["tonal"] > 14: continue   # a held tone in the tail: drums, not synths
            used.add(q["rel"])
            return q
        return None

    out = dict(keep)
    for ki, kit in enumerate(KITS):
        name, style, group, home, kickPack = kit[:5]
        if all("%d:%d" % (ki, si) in keep for si in range(len(SLOTS))):
            continue
        kick_dir = kit_opts(kit).get("kick_dir")
        mode["strict"] = ki >= 24
        chain = [home] + [p for p in FALLBACK[style] if p != home]
        for si, slot in enumerate(SLOTS):
            role = REF_ROLE.get(slot, slot)
            want = GROUP_WISH.get(group, {}).get(slot) if style == "Industrial" else None
            want = want or {"perc_short": "short", "perc_low": "low", "perc_high": "high"}.get(slot)
            if slot == "kick" and kick_dir:
                q = take(home, "kick", kick_dir=kick_dir)
                if q:
                    out["%d:%d" % (ki, si)] = {"kit": name, "slot": si, "role": "kick", "ref": q["rel"], "pack": q["pack"]}
                    continue
            packs = ([kickPack] if (slot == "kick" and kickPack) else []) + chain
            if role == "rumble":
                packs = [40, 18] if style != "Hard Techno" else [18, 40]
            q = None
            if style == "Industrial" and GROUP_WISH.get(group, {}).get(slot):
                for p in packs:   # the idea of the group first: a low (bright, noisy) hit of any pack of the chain
                    q = take(p, role, want)
                    if q: break
            for p in packs if q is None else []:
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
            fit_role = FIT_ROLE.get(slot, role)
            if q is None and role in ALT_ROLE:   # no crash left anywhere: a ride of the style, fitted as a crash
                for p in chain:
                    q = take(p, ALT_ROLE[role])
                    if q: break
            if q is None:
                raise SystemExit("no reference for %s %s" % (name, slot))
            if style == "Industrial":
                gr = GROUP_FIT_ROLE.get(group, {}).get(slot)
                if gr and not (gr == "perc_low" and q["feat"]["cent"] > 900):   # toms first only for a low hit
                    fit_role = gr
            out["%d:%d" % (ki, si)] = {"kit": name, "slot": si, "role": fit_role, "ref": q["rel"], "pack": q["pack"]}
    json.dump(dict(sorted(out.items(), key=lambda kv: tuple(int(x) for x in kv[0].split(":")))), open(PLAN, "w"), indent=1)
    print("kits:", len(KITS), "sounds:", len(out), "kept from the plan before:", len(keep))
    print("home pack share:", sum(1 for k, v in out.items() if v["pack"] == KITS[int(k.split(':')[0])][3]) / len(out))
    for ki, kit in enumerate(KITS):
        if all("%d:%d" % (ki, si) in keep for si in range(len(SLOTS))):
            continue
        print("  %-18s" % kit[0], " ".join("%s:%s" % (SLOTS[si][:6], out["%d:%d" % (ki, si)]["pack"]) for si in range(len(SLOTS))))


# Short tag of each kit for its sounds' names ("Signal Dist Kick"): the browser lists sounds under their kit.
TAG = {"Signal Schranz": "Signal", "Steel Pressure": "Steel", "Night Shift Schranz": "Night", "Modern Schranz": "Modern",
       "Rumble Engine": "Engine", "Pulse Factory": "Pulse", "Classic Schranz": "Classic", "Old School Tools": "Oldschool",
       "Schranz Vol. One": "Vol One", "Loop Cutter": "Cutter", "Second Wave": "Wave", "Rolling Schranz": "Rolling",
       "Twenty Years Hard": "Twenty", "Dirty Lofi Hard": "Lofi", "Warehouse Pressure": "Warehouse", "Hard Groove Tools": "Groove",
       "Driving Machine": "Driving", "Bunker Hard": "Bunker", "Core Rumble": "Core", "Dark Factory": "Factory",
       "Iron Mill": "Iron", "Concrete Dust": "Concrete", "Rust Belt": "Rust", "Machine Hall": "Machine",
       "Black Furnace": "Furnace", "Ash Ritual": "Ash", "Steel Foundry": "Foundry", "Impact Chamber": "Chamber",
       "Static Grind": "Static", "Tunnel Rumble": "Tunnel", "Distortion Plant": "Plant", "Scrap Yard": "Scrap",
       "Smoke Ritual": "Smoke", "Crusher": "Crusher"}
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
    for ki, kit in enumerate(KITS):
        name, style, group, home, kickPack = kit[:5]
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
            lvl, pan, rev, dly = mix_of(slot, style, group)
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
        for k, v in dict(STYLE[style], **GROUP_STYLE.get((style, group), {})).items():
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
