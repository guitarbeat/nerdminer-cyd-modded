#!/usr/bin/env python3
"""Starvation guard: static checks that UI/sampler code can't starve the miner.

Background (Oct 2026): the Mine screen's RECENT ATTEMPTS figure put the hash
sampler's sequence counter (ticks ~5x/s) in its redraw key and repainted the
whole text-heavy 228x222 figure on every sample. That starved the stratum/job
feed: chunk gaps ~28M cycles and ~690 KH/s instead of ~1000. These checks pin
the fixed design so it can't silently regress. Host-only, no device needed.

Run:   python3 scripts/check_mining_starvation.py
       python3 scripts/check_mining_starvation.py --selftest   (proves the checks bite)
Exit code 0 = OK, 1 = violation(s).
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DISPLAY = "src/drivers/displays/esp23_2432s028r.cpp"
MINING = "src/mining.cpp"
MINING_H = "src/mining.h"

MIN_DOTS_MS = 1000
MIN_MISS_BIN_MS = 5000
MIN_TAP_EVERY = 64
# Pre-existing, once-per-chunk (not per-hash) clock read in the pipelined HW worker.
HW_MILLIS_ALLOW = ["s_pl_retry_at"]


def strip_comments(src):
    src = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), src, flags=re.S)
    return re.sub(r"//[^\n]*", "", src)


def func_bodies(src, signature_re):
    """All bodies (text between the matching braces) of functions matching signature_re."""
    out = []
    for m in re.finditer(signature_re, src):
        i = src.index("{", m.end() - 1 if src[m.end() - 1] == "{" else m.end())
        depth, j = 0, i
        while True:
            c = src[j]
            if c == "{":
                depth += 1
            elif c == "}":
                depth -= 1
                if depth == 0:
                    break
            j += 1
        out.append(src[i + 1 : j])
    return out


def call_args(body, fname):
    """Argument text of every fname(...) call in body (balanced parens)."""
    res = []
    for m in re.finditer(r"\b" + re.escape(fname) + r"\s*\(", body):
        i, depth = m.end(), 1
        while depth:
            c = body[i]
            depth += (c == "(") - (c == ")")
            i += 1
        res.append(body[m.end() : i - 1])
    return res


def define_value(src, name):
    m = re.search(r"#define\s+" + name + r"\s+(\d+)u?\b", src)
    return int(m.group(1)) if m else None


def check(files):
    errs = []
    disp = files[DISPLAY]
    dispc = strip_comments(disp)
    mine = strip_comments(files[MINING])
    mineh = files[MINING_H]

    # ---------------- display: RECENT ATTEMPTS redraw budget ----------------
    bodies = func_bodies(dispc, r"static void drawSharesFigure\s*\(const Rect &r, const MineV &m\)\s*\{")
    if len(bodies) != 1:
        return ["display: drawSharesFigure() not found exactly once"]
    fig = bodies[0]

    keys = call_args(fig, "snprintf")
    for a in keys:
        if re.search(r"\bsampSeq\b|getHashSampleSeq|getShareHistorySeq|\bm\.seq\b", a):
            errs.append("display: a RECENT ATTEMPTS redraw key contains a high-frequency sequence counter "
                        "(sampSeq/getHashSampleSeq/m.seq) -> repaints on every sample: " + " ".join(a.split())[:120])
        for d in re.findall(r"(?:nowMs|millis\(\))\s*/\s*(\d+)\b(?!\s*\*)", a):
            if int(d) < MIN_DOTS_MS:
                errs.append("display: redraw key time bucket finer than %d ms: /%s" % (MIN_DOTS_MS, d))

    dots_ms = define_value(disp, "ATT_DOTS_MIN_MS")
    bin_ms = define_value(disp, "ATT_MISS_BIN_MIN_MS")
    if dots_ms is None or dots_ms < MIN_DOTS_MS:
        errs.append("display: ATT_DOTS_MIN_MS missing or < %d (got %s)" % (MIN_DOTS_MS, dots_ms))
    if bin_ms is None or bin_ms < MIN_MISS_BIN_MS:
        errs.append("display: ATT_MISS_BIN_MIN_MS missing or < %d (got %s)" % (MIN_MISS_BIN_MS, bin_ms))

    # Spectrogram rows: the body is repainted in full only when a row scrolls
    # (every ATT_ROW_MS); in between only the newest row is pushed.
    row_ms = define_value(disp, "ATT_ROW_MS")
    if row_ms is None:
        m_rows = re.search(r"#define\s+ATT_ROWS\s+(\d+)", disp)
        m_win = re.search(r"#define\s+ATT_WIN_SEC\s+(\d+)", disp)
        if m_rows and m_win:
            row_ms = int(m_win.group(1)) * 1000 // int(m_rows.group(1))
    if row_ms is None or row_ms < 2 * MIN_DOTS_MS:
        errs.append("display: ATT_ROW_MS missing or < %d ms (full spectrogram repaint too often)" % (2 * MIN_DOTS_MS))
    if not re.search(r"rowEpoch\s*!=\s*s_lastRowEpoch", fig):
        errs.append("display: spectrogram full repaint must be gated on a row scroll (rowEpoch != s_lastRowEpoch)")
    # Screen 1 (target ruler): the staircase repaints at most every ATT_STAIR_MS,
    # and the 60 s sweep is only fully repainted on a frame/screenshot redraw
    # (each tick touches just the newest slots + cursor).
    stair_ms = define_value(disp, "ATT_STAIR_MS")
    if stair_ms is None or stair_ms < 5 * MIN_DOTS_MS:
        errs.append("display: ATT_STAIR_MS missing or < %d ms (staircase repaint too often)" % (5 * MIN_DOTS_MS))
    if not re.search(r"const\s+bool\s+fullE\s*=\s*s_dotsFull\s*\|\|\s*shotOn\s*;", fig):
        errs.append("display: Screen 1 sweep full repaint must be gated on s_dotsFull || shotOn only")
    if not re.search(r"stair\s*!=\s*s_lastStair", fig):
        errs.append("display: Screen 1 staircase repaint must be gated on a new ATT_STAIR_MS bucket")

    # Layering: frame / dots / bins must be separate widgets.
    needs = set(re.findall(r"\bneed\(\s*(W_[A-Z0-9_]+)", fig))
    for w in ("W_PROTO_PLOT", "W_ATT_DOTS", "W_ATT_BINS"):
        if w not in needs:
            errs.append("display: layered redraw missing need(%s, ...) in drawSharesFigure" % w)

    def key_for(widget):
        m = re.search(r"\bneed\(\s*" + widget + r"\s*,\s*(\w+)\s*\)", fig)
        if not m:
            return None
        var = m.group(1)
        for a in keys:
            if re.match(r"\s*" + var + r"\s*,", a):
                return a
        return None

    kd = key_for("W_ATT_DOTS")
    if kd is None or "ATT_DOTS_MIN_MS" not in kd:
        errs.append("display: dots-layer key must bucket time by ATT_DOTS_MIN_MS")
    kb = key_for("W_ATT_BINS")
    if kb is None or "ATT_MISS_BIN_MIN_MS" not in kb:
        errs.append("display: bins-layer key must bucket time by ATT_MISS_BIN_MIN_MS")
    kf = key_for("W_PROTO_PLOT")
    if kf is None:
        errs.append("display: frame-layer (W_PROTO_PLOT) key not found")
    elif re.search(r"nowMs|millis|nowUtc|ac\.", kf):
        errs.append("display: frame layer (headers/tabs/labels) key must not change with time/counters")

    # The per-second dots layer must stay text-free (text rendering is the expensive part).
    m = re.search(r"if\s*\(\s*need\(\s*W_ATT_DOTS.*?done\(\s*W_ATT_DOTS", fig, flags=re.S)
    if not m:
        errs.append("display: dots layer block (need..done W_ATT_DOTS) not found")
    elif re.search(r"\btext\s*\(|drawString|\bprint\s*\(", m.group(0)):
        errs.append("display: dots layer draws text; keep text in the frame/bins layers")

    # ---------------- display: header coin / pool badge ----------------
    # The coin + pool badge measures text (shortPoolHost) on every repaint, so
    # it must only repaint when the coin or host changes, never per second.
    hdr = func_bodies(dispc, r"static void drawHeader\s*\(const HdrV &h\)\s*\{")
    if len(hdr) != 1:
        errs.append("display: drawHeader() not found exactly once")
    else:
        hb = hdr[0]
        mk = re.search(r"String\s+kCoin\s*=\s*([^;]*);", hb)
        if not mk:
            errs.append("display: header coin badge key (kCoin) not found")
        elif re.search(r"\bh\.(job|time|time12|blocks|netHeight|bars|status)\b|millis|nowMs", mk.group(1)):
            errs.append("display: header coin badge key changes with time/counters: " + " ".join(mk.group(1).split())[:100])
        mb = re.search(r"if\s*\(\s*need\(\s*W_HDR_BADGE.*?done\(\s*W_HDR_BADGE", hb, flags=re.S)
        if "shortPoolHost(" in hb and (not mb or "shortPoolHost(" not in mb.group(0)
                                       or hb.count("shortPoolHost(") != mb.group(0).count("shortPoolHost(")):
            errs.append("display: shortPoolHost() must run only inside the need(W_HDR_BADGE) block")

    # ---------------- display: job-age ring ----------------
    jr = func_bodies(dispc, r"static void jobRingDraw\s*\(TFT_eSPI &d\)\s*\{")
    if len(jr) != 1:
        errs.append("display: jobRingDraw() not found exactly once")
    else:
        if not re.search(r"if\s*\(\s*!shotOn\s*&&\s*!jobRingDirty\s*&&\s*age\s*==\s*lastAge\s*\)\s*return", jr[0]):
            errs.append("display: jobRingDraw() must return early unless the age (seconds) changed")
        if not re.search(r"if\s*\(\s*!shotOn\s*&&\s*lvl\s*==\s*jobLvl\[i\]\s*\)\s*continue", jr[0]):
            errs.append("display: jobRingDraw() must skip unchanged cells (delta-only redraw)")
        if re.search(r"\btext\s*\(", jr[0]):
            errs.append("display: jobRingDraw() must not render text")
    if re.search(r"String\s+kSub\s*=[^;]*\bh\.job\b", dispc):
        errs.append("display: header height widget key must not include the job age (ring shows it)")

    # ---------------- mining: sampler stays off the HW hot path ----------------
    hw = func_bodies(mine, r"\nvoid minerWorkerHw\s*\(void \* task_id\)\s*\{")
    if not hw:
        errs.append("mining: minerWorkerHw() not found")
    for b in hw:
        for ln in b.split("\n"):
            if "millis(" in ln and not any(a in ln for a in HW_MILLIS_ALLOW):
                errs.append("mining: millis() in the HW miner loop: " + ln.strip()[:100])
            for bad in ("hashTapSw", "hashSamplePut", "hashSampleFeed", "getHashSamples",
                        "portENTER_CRITICAL", "Serial.print"):
                if re.search(r"\b" + bad + r"\b", ln) and "DEBUG" not in ln:
                    errs.append("mining: %s in the HW miner loop: %s" % (bad, ln.strip()[:100]))
        # noteCandidate only behind a prefilter hit.
        lines = b.split("\n")
        for i, ln in enumerate(lines):
            if "noteCandidate(" in ln:
                ctx = "\n".join(lines[max(0, i - 25) : i])
                if not re.search(r"\bhit\b|read_digest|confirmed|cand_diff", ctx):
                    errs.append("mining: noteCandidate() in HW loop not behind a prefilter-hit branch")

    # SW tap: only behind a cheap countdown gate, and HASH_TAP_EVERY sane.
    tap_every = define_value(mineh, "HASH_TAP_EVERY")
    if tap_every is None or tap_every < MIN_TAP_EVERY:
        errs.append("mining: HASH_TAP_EVERY missing or < %d" % MIN_TAP_EVERY)
    calls = [m.start() for m in re.finditer(r"\bhashTapSw\s*\(", mine)]
    defs = [m.start() for m in re.finditer(r"static void[^;{]*\bhashTapSw\s*\(", mine)]
    uses = [c for c in calls if not any(abs(c - d) < 80 for d in defs)]
    for c in uses:
        ctx = mine[max(0, c - 200) : c]
        if not re.search(r"if\s*\(\s*--\s*\w+\s*==\s*0\s*\)", ctx):
            errs.append("mining: hashTapSw() call not behind an `if (--n == 0)` countdown gate")
    sw = func_bodies(mine, r"\nvoid minerWorkerSw\s*\(void \* task_id\)\s*\{")
    if uses and (not sw or "hashTapSw(" not in sw[0]):
        errs.append("mining: hashTapSw() must be called only from minerWorkerSw (core 0)")
    if len(uses) > 1:
        errs.append("mining: hashTapSw() called from more than one site")

    # TAP-only sampler: noteCandidate() runs behind every HW/SW prefilter hit;
    # it must stay accounting-only (no sampling, no lock, no clock read).
    nc = func_bodies(mine, r"\nvoid noteCandidate\s*\(double diff, const uint8_t \*hashLE\)\s*\{")
    if len(nc) != 1:
        errs.append("mining: noteCandidate() not found")
    elif re.search(r"hashSample|hashTap|millis\(|portENTER_CRITICAL", nc[0]):
        errs.append("mining: noteCandidate() must not sample, lock or read the clock (TAP-only)")

    # updateBestDiff is also called from the stratum task; it must not sample.
    ub = func_bodies(mine, r"\nvoid updateBestDiff\s*\(double diff, const uint8_t \*hashLE\)\s*\{")
    if ub and re.search(r"hashSample|hashTap|millis\(", ub[0]):
        errs.append("mining: updateBestDiff() must not sample or read the clock")
    return errs


def load():
    return {p: open(os.path.join(ROOT, p), encoding="utf-8", errors="replace").read()
            for p in (DISPLAY, MINING, MINING_H)}


def selftest():
    base = load()
    assert not check(base), "baseline must pass before self-test: %s" % check(base)
    mutations = [
        ("sampSeq in plot key", DISPLAY,
         'snprintf(kd, sizeof(kd), "d|%lu|%u", (unsigned long)(nowMs / ATT_DOTS_MIN_MS), (unsigned)win);',
         'snprintf(kd, sizeof(kd), "d|%lu|%u|%u", (unsigned long)(nowMs / ATT_DOTS_MIN_MS), (unsigned)win, (unsigned)sampSeq);'),
        ("dots 500 ms", DISPLAY, "#define ATT_DOTS_MIN_MS 1000", "#define ATT_DOTS_MIN_MS 500"),
        ("bins 1 s", DISPLAY, "#define ATT_MISS_BIN_MIN_MS 5000", "#define ATT_MISS_BIN_MIN_MS 1000"),
        ("text in dots layer", DISPLAY,
         "            d.fillRect(x, dY(y), SPEC_W, SPEC_H, ramp[lane][lvl(n)]);",
         '            text(d, "x", x, dY(y), 9, C_TEXT, C_BG);\n            d.fillRect(x, dY(y), SPEC_W, SPEC_H, ramp[lane][lvl(n)]);'),
        ("spectrogram full repaint every tick", DISPLAY,
         "const bool full = s_dotsFull || shotOn || rowEpoch != s_lastRowEpoch;",
         "const bool full = true;"),
        ("spectrogram rows too short", DISPLAY, "#define ATT_ROWS 28", "#define ATT_ROWS 120"),
        ("staircase repaint every tick", DISPLAY, "#define ATT_STAIR_MS 5000", "#define ATT_STAIR_MS 200"),
        ("sweep full repaint every tick", DISPLAY, "const bool fullE = s_dotsFull || shotOn;",
         "const bool fullE = true;"),
        ("staircase ungated", DISPLAY, "if (stair != s_lastStair)", "if (true)"),
        ("job age in coin badge key", DISPLAY,
         'String kCoin = "hcoin|" + h.coin + "|" + h.poolHost + "|" + (P ? "P" : "L");',
         'String kCoin = "hcoin|" + h.coin + "|" + h.poolHost + "|" + h.job + "|" + (P ? "P" : "L");'),
        ("pool shortening outside its widget", DISPLAY,
         "  const int wCoin = 58, hCoin = 30;\n",
         "  const int wCoin = 58, hCoin = 30;\n  unsigned pz; String phx = shortPoolHost(h.poolHost, 57, 12, 11, &pz);\n"),
        ("job ring ungated", DISPLAY,
         "if (!shotOn && !jobRingDirty && age == lastAge)",
         "if (false)"),
        ("job ring full redraw", DISPLAY,
         "if (!shotOn && lvl == jobLvl[i])",
         "if (false)"),
        ("tap ungated", MINING, "if (--swTap == 0)", "if (true)"),
        ("tap every 1", MINING_H, "#define HASH_TAP_EVERY 256u", "#define HASH_TAP_EVERY 1u"),
        ("sampler in noteCandidate", MINING,
         "    s_candidates++;\n",
         "    s_candidates++;\n  hashSamplePut(-(float)diff, millis());\n"),
        ("sampler in updateBestDiff", MINING,
         "void updateBestDiff(double diff, const uint8_t *hashLE)\n{\n",
         "void updateBestDiff(double diff, const uint8_t *hashLE)\n{\n  hashSamplePut((float)diff, millis());\n"),
    ]
    ok = True
    for name, path, old, new in mutations:
        f = dict(base)
        if old not in f[path]:
            print("SELFTEST SKIP (anchor moved): " + name)
            ok = False
            continue
        f[path] = f[path].replace(old, new, 1)
        caught = check(f)
        print(("caught    " if caught else "MISSED    ") + name)
        ok &= bool(caught)
    return ok


def main():
    if "--selftest" in sys.argv:
        ok = selftest()
        print("selftest " + ("OK" if ok else "FAILED"))
        return 0 if ok else 1
    errs = check(load())
    for e in errs:
        print("FAIL " + e)
    print("mining starvation guard: " + ("OK" if not errs else "%d violation(s)" % len(errs)))
    return 1 if errs else 0


if __name__ == "__main__":
    sys.exit(main())
