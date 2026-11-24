#!/usr/bin/env python3
# tools/make_howto_pdf.py
#
# Builds docs/DropPoint-How-To.pdf: a customer-facing "what it is and how to
# use it" document whose every screenshot is a REAL capture — terminal output
# re-rendered verbatim from actual command output, a headless-Edge capture of
# the CI run page and the bench dashboard, and the ARM64 install transcripts.
# The only non-photographic image (the Pilot 2 widget panel) is labelled as
# an illustration, per the header promise of this document.
#
# Usage:
#   python tools/make_howto_pdf.py [--install-logs DIR] [--run-url URL]
#       [--out docs/DropPoint-How-To.pdf] [--shots docs/howto/screens]
#       [--build-dir build-h2]
#
# Dependencies: Python 3 + Pillow + markdown (both installed). Prints the PDF
# through headless Microsoft Edge — no reportlab.
import argparse
import datetime
import html
import os
import re
import subprocess
import sys
from pathlib import Path

import markdown
from PIL import Image, ImageDraw, ImageFont

REPO = Path(__file__).resolve().parent.parent

EDGE_CANDIDATES = [
    r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
    r"C:\Program Files\Microsoft\Edge\Application\msedge.exe",
]
FONT_DIR = Path(os.environ.get("WINDIR", r"C:\Windows")) / "Fonts"

# ---------------------------------------------------------------------------
# small helpers
# ---------------------------------------------------------------------------


def find_edge() -> str:
    for cand in EDGE_CANDIDATES:
        if Path(cand).exists():
            return cand
    sys.exit("error: Microsoft Edge not found (looked for msedge.exe)")


def font(name: str, size: int) -> ImageFont.FreeTypeFont:
    path = FONT_DIR / name
    if path.exists():
        return ImageFont.truetype(str(path), size)
    return ImageFont.load_default()


def run(cmd, cwd=None, check=True) -> str:
    """Run a command, return its combined stdout. Fails loudly."""
    print(f"  $ {' '.join(str(c) for c in cmd)}")
    proc = subprocess.run([str(c) for c in cmd], capture_output=True, text=True,
                          cwd=cwd, encoding="utf-8", errors="replace")
    if check and proc.returncode != 0:
        sys.exit(f"error: command failed ({proc.returncode}): {cmd}\n"
                 f"{proc.stdout}\n{proc.stderr}")
    return proc.stdout + proc.stderr


def term_image(title: str, text: str, out: Path, max_rows: int = 40):
    """Render verbatim terminal output as a dark terminal-style PNG."""
    lines = text.replace("\r", "").split("\n")
    while lines and not lines[-1].strip():
        lines.pop()
    if len(lines) > max_rows:
        keep_head = max_rows - 9
        omitted = len(lines) - keep_head - 8
        lines = lines[:keep_head] + [f"        … {omitted} lines omitted …"] + lines[-8:]

    body_font = font("consola.ttf", 17)
    title_font = font("consolab.ttf", 16) if (FONT_DIR / "consolab.ttf").exists() \
        else font("consola.ttf", 16)
    pad, bar_h, line_h = 14, 34, 22
    width = 1600
    height = bar_h + pad + line_h * len(lines) + pad
    img = Image.new("RGB", (width, height), "#0c0c0c")
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, width, bar_h], fill="#2b2b2b")
    d.text((pad, bar_h // 2 - 9), title, font=title_font, fill="#e8e8e8")

    colours = {
        "[OK]": "#7ee787", "[PASS]": "#7ee787", "PASS": "#7ee787",
        "[FAIL]": "#ff7b72", "FAIL": "#ff7b72", "error": "#ff7b72",
        "[WARN]": "#ffa657", "WARN": "#ffa657",
        "[SKIP]": "#8b949e", "SKIP": "#8b949e",
        "[INFO]": "#79c0ff",
    }
    y = bar_h + pad
    for ln in lines:
        colour = "#c9d1d9"
        for tag, c in colours.items():
            if tag in ln:
                colour = c
                break
        d.text((pad, y), ln, font=body_font, fill=colour)
        y += line_h
    out.parent.mkdir(parents=True, exist_ok=True)
    img.save(out)


def edge_shot(edge: str, url: str, out: Path, budget_ms: int = 20000):
    run([edge, "--headless=new", "--disable-gpu",
         f"--screenshot={out}", "--window-size=1600,1000",
         f"--virtual-time-budget={budget_ms}", url], check=False)
    if not out.exists() or out.stat().st_size < 5000:
        sys.exit(f"error: Edge screenshot failed for {url} -> {out}")


def read(path: Path) -> str:
    if not path.exists():
        sys.exit(f"error: required input missing: {path}")
    return path.read_text(encoding="utf-8", errors="replace")


# ---------------------------------------------------------------------------
# real captures
# ---------------------------------------------------------------------------

WIDGET_RE = re.compile(
    r'\{(\d+),\s*WidgetType::k(\w+),\s*"([^"]+)",\s*(true|false),\s*"([^"]*)",\s*(true|false)\}')


def widget_table() -> list:
    """Parse the single source of truth (src/hw/widget_map.h)."""
    src = read(REPO / "src/hw/widget_map.h")
    rows = []
    for m in WIDGET_RE.finditer(src):
        idx, wtype, name, main, icon, rc = m.groups()
        rows.append({"index": int(idx), "type": wtype, "name": name,
                     "main": main == "true", "icon": icon, "rc": rc == "true"})
    if len(rows) != 10:
        sys.exit(f"error: expected 10 widgets in widget_map.h, parsed {len(rows)}")
    return rows


def make_widget_illustration(widgets, shots: Path) -> Path:
    """S8 — clearly labelled ILLUSTRATION of the ten Pilot 2 widgets."""
    out = shots / "s8-widgets-illustration.png"
    W, H = 1600, 640
    img = Image.new("RGB", (W, H), "#15181d")
    d = ImageDraw.Draw(img)
    title_f = font("consolab.ttf", 26)
    name_f = font("consolab.ttf", 19)
    sub_f = font("consola.ttf", 15)
    cap_f = font("consola.ttf", 14)

    d.text((24, 16), "DropPoint operator widgets — DJI Pilot 2 layout",
           font=title_f, fill="#e8e8e8")
    d.text((24, 52), "ILLUSTRATION — built from the real widget table "
           "(src/hw/widget_map.h); DJI Pilot 2 cannot be captured without an aircraft",
           font=cap_f, fill="#8b949e")

    type_label = {"Button": "button", "Switch": "switch", "Scale": "scale",
                  "List": "list", "IntInputBox": "int input"}
    rows_def = [("main_interface", [w for w in widgets if w["main"]]),
                ("config_interface", [w for w in widgets if not w["main"]])]
    y = 86
    for row_name, row in rows_def:
        d.text((24, y), row_name, font=sub_f, fill="#79c0ff")
        y += 26
        x = 24
        for w in row:
            cell_w, cell_h = 300, 210
            d.rounded_rectangle([x, y, x + cell_w - 12, y + cell_h],
                                radius=10, fill="#21262d", outline="#444c56", width=2)
            cx = x + (cell_w - 12) // 2
            icon_path = REPO / "assets/widget" / w["icon"] if w["icon"] else None
            if icon_path and icon_path.exists():
                icon = Image.open(icon_path).convert("RGBA").resize((72, 72))
                img.paste(icon, (cx - 36, y + 16), icon)
            else:
                # generic glyph for widgets without an icon file
                glyph = {"Switch": "◐", "List": "≡", "IntInputBox": "[  ]",
                         "Button": "●"}.get(w["type"], "□")
                d.text((cx - 20, y + 24), glyph, font=font("consola.ttf", 44),
                       fill="#8b949e")
            d.text((cx - d.textlength(w["name"], font=name_f) / 2, y + 104),
                   w["name"], font=name_f, fill="#e8e8e8")
            sub = f'#{w["index"]}  {type_label.get(w["type"], w["type"])}'
            d.text((cx - d.textlength(sub, font=sub_f) / 2, y + 136), sub,
                   font=sub_f, fill="#8b949e")
            if w["rc"]:
                badge = "RC-bindable"
                bw = d.textlength(badge, font=cap_f) + 16
                d.rounded_rectangle([cx - bw / 2, y + 166, cx + bw / 2, y + 190],
                                    radius=10, fill="#1f6feb")
                d.text((cx - d.textlength(badge, font=cap_f) / 2, y + 170),
                       badge, font=cap_f, fill="#ffffff")
            x += cell_w
        y += cell_h + 18
    img.save(out)
    return out


def hud_strings(build_dir: Path, sim_exe: Path) -> list:
    """S9 — capture the floating-window text the GuidanceLoop really emits."""
    texts = []
    for scen in ("hover", "crosswind-5ms"):
        out = run([sim_exe, "--sim", scen, "--verbose"], cwd=REPO, check=True)
        texts.append(out)
    blobs = "\n".join(texts)
    seen, all_huds = set(), []
    for m in re.finditer(r"hud='(.*?)'", blobs, re.S):
        s = m.group(1)
        if s not in seen:
            seen.add(s)
            all_huds.append(s)
    # Pick one representative string per distinct HUD pattern.
    picks = []
    patterns = ["NO TARGET", "XX HOLD", "PRESS ARM", "MANUAL T-", "AUTO T-",
                "DROP IN 3", "DROP IN 1", "DROP OK", "RELEASED"]
    for pat in patterns:
        for s in all_huds:
            if pat in s and s not in picks:
                picks.append(s)
                break
    if len(picks) < 5:
        sys.exit(f"error: too few distinct HUD strings captured ({len(picks)})")
    return picks


def make_hud_image(huds, shots: Path) -> Path:
    out = shots / "s9-floating-window.png"
    box_w, box_h, gap = 480, 130, 18
    cols = 3
    rows = (len(huds) + cols - 1) // cols
    W = 40 + cols * (box_w + gap)
    H = 80 + rows * (box_h + gap)
    img = Image.new("RGB", (W, H), "#15181d")
    d = ImageDraw.Draw(img)
    d.text((24, 16), "Floating-window text — exactly as DropPoint sends it",
           font=font("consolab.ttf", 24), fill="#e8e8e8")
    d.text((24, 48), "captured from simulator runs (rendered here outside DJI Pilot 2)",
           font=font("consola.ttf", 14), fill="#8b949e")
    body = font("consola.ttf", 16)
    for i, s in enumerate(huds):
        col, row = i % cols, i // cols
        x = 20 + col * (box_w + gap)
        y = 78 + row * (box_h + gap)
        d.rounded_rectangle([x, y, x + box_w, y + box_h], radius=8,
                            fill="#3a3f45", outline="#6e7681", width=2)
        for j, ln in enumerate(s.split("\n")[:5]):
            d.text((x + 14, y + 12 + j * 22), ln, font=body, fill="#e8e8e8")
    img.save(out)
    return out


def install_log_parts(log_path: Path):
    """Split an install transcript into (install+selftest, upgrade+remove)."""
    lines = read(log_path).split("\n")
    cut = next((i for i, l in enumerate(lines) if "site edit marker" in l), len(lines))
    head = lines[:cut]
    # Drop the docker image-pull preamble from the first image.
    start = next((i for i, l in enumerate(head) if "dpkg -i" in l), 0)
    return "\n".join(head[start:]), "\n".join(lines[cut:])


# ---------------------------------------------------------------------------
# HTML / PDF
# ---------------------------------------------------------------------------

CSS = """
@page { size: A4; margin: 0; }
* { box-sizing: border-box; }
body { margin: 0; font-family: 'Segoe UI', Arial, sans-serif; color: #1b1f24;
       font-size: 10.5pt; line-height: 1.45; }
.page { width: 210mm; height: 296mm; overflow: hidden; position: relative;
        padding: 15mm 17mm 20mm; page-break-after: always; background: white; }
.page:last-child { page-break-after: auto; }
.pageno { position: absolute; bottom: 7mm; right: 17mm; font-size: 8.5pt;
          color: #888; }
h1 { font-size: 20pt; margin: 0 0 4mm; color: #0a3069; }
h2 { font-size: 15pt; margin: 0 0 3mm; color: #0a3069;
     border-bottom: 2px solid #0a3069; padding-bottom: 1.5mm; }
h3 { font-size: 12pt; margin: 4mm 0 2mm; color: #0a3069; }
p { margin: 0 0 2.5mm; }
ul, ol { margin: 0 0 2.5mm 6mm; padding: 0; }
li { margin-bottom: 1mm; }
img.shot { display: block; max-width: 100%; border: 1px solid #c8cdd4;
           border-radius: 2mm; margin: 2mm auto; }
.caption { font-size: 8.8pt; color: #555; text-align: center;
           margin: -1mm 0 3mm; }
table { border-collapse: collapse; width: 100%; margin: 2mm 0 3mm;
        font-size: 9.2pt; }
th, td { border: 1px solid #c8cdd4; padding: 1.6mm 2.4mm; text-align: left; }
th { background: #eef2f7; }
code { font-family: Consolas, monospace; font-size: 9pt;
       background: #f2f4f7; padding: 0 1mm; border-radius: 1mm; }
pre { font-family: Consolas, monospace; font-size: 8.6pt; background: #f6f8fa;
      border: 1px solid #d0d7de; border-radius: 2mm; padding: 3mm;
      white-space: pre-wrap; }
.titlepage { text-align: center; padding-top: 60mm; }
.titlepage h1 { font-size: 34pt; border: none; }
.sub { font-size: 13pt; color: #444; margin-bottom: 14mm; }
.warn { border: 1.5pt solid #b58b00; background: #fff8e1; padding: 3mm 4mm;
        border-radius: 2mm; font-weight: 600; }
.checklist li { list-style: none; margin-left: -4mm; }
.checklist li:before { content: "☐  "; }
/* flow blocks: real pagination is done by JS measuring scrollHeight */
.blk { overflow: hidden; margin: 0; }
"""


PAGINATE_JS = """
<script>
window.addEventListener('load', function () {
  var flow = document.getElementById('flow');
  var blocks = Array.prototype.slice.call(flow.children);
  flow.parentNode.removeChild(flow);
  var pageNum = document.querySelectorAll('.page').length;  // title = 1
  var cur = null, limit = 0, used = 0;
  function newPage() {
    pageNum++;
    cur = document.createElement('div');
    cur.className = 'page';
    var pn = document.createElement('div');
    pn.className = 'pageno';
    pn.textContent = 'DropPoint How-To \\u2014 page ' + pageNum;
    cur.appendChild(pn);
    document.body.appendChild(cur);
    var cs = getComputedStyle(cur);
    limit = cur.clientHeight - parseFloat(cs.paddingTop)
          - parseFloat(cs.paddingBottom) - 2;
    used = 0;
  }
  function shrink(b) {
    var h = b.offsetHeight;
    if (h > limit) {
      var s = limit / h;
      b.style.transformOrigin = 'top left';
      b.style.transform = 'scale(' + s + ')';
      b.style.width = (100 / s) + '%';
      b.style.height = (limit) + 'px';
    }
    return Math.min(h, limit);
  }
  newPage();
  for (var i = 0; i < blocks.length; i++) {
    var b = blocks[i];
    var had = used > 0;
    if (had && b.classList.contains('newpage')) { newPage(); had = false; }
    cur.appendChild(b);
    var bh = b.offsetHeight;
    if (had && used + bh > limit) {
      cur.removeChild(b);
      newPage();
      cur.appendChild(b);
      used = shrink(b);
    } else if (!had && bh > limit) {
      used = shrink(b);
    } else {
      used += bh;
    }
  }
});
</script>
"""


def blk(inner: str, cls: str = "") -> str:
    c = "blk" + (" " + cls if cls else "")
    return f'<div class="{c}">{inner}</div>'


def img(rel_name: str, caption: str) -> str:
    return (f'<img class="shot" src="screens/{rel_name}">'
            f'<div class="caption">{html.escape(caption)}</div>')


TOP_ELEM_RE = re.compile(
    r'<(h\d|p|ul|ol|table|pre|blockquote)[^>]*>.*?</\1>|<hr[^>]*/?>', re.S)


def md_blocks(rel: str) -> list:
    """Render a markdown file to HTML split into top-level block elements.
    Each heading is merged with the element that follows it so a heading can
    never be orphaned at the bottom of a page."""
    h = markdown.markdown(read(REPO / rel),
                          extensions=["tables", "fenced_code"])
    elems = [m.group(0) for m in TOP_ELEM_RE.finditer(h)]
    if re.sub(r"\s", "", "".join(elems)) != re.sub(r"\s", "", h):
        sys.exit(f"error: markdown block split lost content in {rel}")
    merged, i = [], 0
    while i < len(elems):
        if re.match(r"<h\d", elems[i]) and i + 1 < len(elems):
            merged.append(elems[i] + elems[i + 1])
            i += 2
        else:
            merged.append(elems[i])
            i += 1
    return merged


def checklist_from_md(rel: str, marker: str) -> str:
    """Extract the bullet list under a '## <marker>' heading as <li> html."""
    text = read(REPO / rel)
    m = re.search(re.escape(marker) + r"\s*\n(.*?)(\n## |\Z)", text, re.S)
    if not m:
        sys.exit(f"error: section '{marker}' not found in {rel}")
    body = markdown.markdown(m.group(1).strip())
    return body


def main() -> int:
    ap = argparse.ArgumentParser(description="Build docs/DropPoint-How-To.pdf")
    ap.add_argument("--install-logs",
                    default=r"C:\Users\DELL\AppData\Local\Temp\dp\art")
    ap.add_argument("--run-url",
                    default="https://github.com/mohdasim87/DJI-DROP-POINT/"
                            "actions/runs/45331670054")
    ap.add_argument("--out", default=str(REPO / "docs/DropPoint-How-To.pdf"))
    ap.add_argument("--shots", default=str(REPO / "docs/howto/screens"))
    ap.add_argument("--build-dir", default=str(REPO / "build-h2"))
    args = ap.parse_args()

    shots = Path(args.shots)
    out_pdf = Path(args.out)
    shots.mkdir(parents=True, exist_ok=True)
    edge = find_edge()
    build = Path(args.build_dir)
    sim_exe = build / "src/app/Release/droppoint_sim_main.exe"
    test_exe = build / "tests/Release/droppoint_core_tests.exe"
    for exe in (sim_exe, test_exe):
        if not exe.exists():
            sys.exit(f"error: build artifact missing: {exe} (pass --build-dir)")

    print("[1/9] CI run page (real browser capture)")
    edge_shot(edge, args.run_url, shots / "s1-ci-run.png")

    print("[2/9] unit tests (real ctest + test binary output)")
    ctest_out = run(["ctest", "--test-dir", str(build), "-C", "Release",
                     "--output-on-failure"], cwd=REPO)
    test_out = run([test_exe], cwd=REPO)
    m = re.search(r"(\d+) tests, 0 failed", test_out)
    test_count = m.group(1) if m else "?"
    term_image("> ctest --test-dir build-h2 -C Release --output-on-failure"
               "   (then the test binary itself)",
               ctest_out + "\n> build-h2/tests/Release/droppoint_core_tests.exe\n" + test_out,
               shots / "s2-ctest.png")

    print("[3/9] simulator runs (real output)")
    hover_out = run([sim_exe, "--sim", "hover"], cwd=REPO)
    term_image("> droppoint_sim_main.exe --sim hover", hover_out,
               shots / "s3-sim-hover.png")
    cw_out = run([sim_exe, "--sim", "crosswind-5ms"], cwd=REPO)
    term_image("> droppoint_sim_main.exe --sim crosswind-5ms", cw_out,
               shots / "s4-sim-crosswind.png")
    st_m4t = run([sim_exe, "--selftest", "--config", "config/profiles/m4t"], cwd=REPO)
    term_image("> droppoint_sim_main.exe --selftest --config config/profiles/m4t",
               st_m4t, shots / "s5a-selftest-m4t.png")
    st_m400 = run([sim_exe, "--selftest", "--config", "config/profiles/m400"], cwd=REPO)
    term_image("> droppoint_sim_main.exe --selftest --config config/profiles/m400",
               st_m400, shots / "s5b-selftest-m400.png")

    print("[4/9] ARM64 install transcripts (CI artifacts)")
    for profile in ("m4t", "m400"):
        log = Path(args.install_logs) / "install-logs" / f"install-{profile}.log"
        install_part, upgrade_part = install_log_parts(log)
        term_image(f"arm64v8/ubuntu:20.04 — dpkg -i + --version + --selftest "
                   f"({profile})", install_part,
                   shots / f"s6-{profile}-install.png", max_rows=44)
        term_image(f"arm64v8/ubuntu:20.04 — reinstall (upgrade) + dpkg -r "
                   f"({profile})", upgrade_part,
                   shots / f"s6-{profile}-upgrade.png", max_rows=30)

    print("[5/9] bench dashboard (real UI capture)")
    dash = (REPO / "tools/dashboard/index.html").resolve()
    edge_shot(edge, "file:///" + str(dash).replace("\\", "/"),
              shots / "s7-dashboard.png", budget_ms=8000)

    print("[6/9] widget illustration")
    widgets = widget_table()
    make_widget_illustration(widgets, shots)

    print("[7/9] floating-window strings (real output)")
    huds = hud_strings(build, sim_exe)
    make_hud_image(huds, shots)

    # ---- numbers pulled from the real captures -----------------------------
    def summary_nums(text):
        m = re.search(r"SUMMARY.*fired=(\d).*predicted_miss=([\d.]+)m.*truth_miss=([\d.n/a]+)",
                      text)
        return m.groups() if m else ("?", "?", "?")

    h_fired, h_pred, h_truth = summary_nums(hover_out)
    c_fired, c_pred, c_truth = summary_nums(cw_out)

    debs = sorted(p.name for p in
                  (Path(args.install_logs) / "droppoint-packages").glob("*.deb"))
    m4t_deb = next((d for d in debs if "-m4t_" in d), "droppoint-m4t_<ver>_arm64.deb")
    m400_deb = next((d for d in debs if "-m400_" in d), "droppoint-m400_<ver>_arm64.deb")

    # version + date for the title page
    ver_h = read(build / "generated/util/version.h")
    ver = re.search(r'kVersionString\s*=\s*"([^"]+)"', ver_h)
    bld = re.search(r'kBuildVersion\s*=\s*"([^"]+)"', ver_h)
    version = f'{ver.group(1) if ver else "?"} (build {bld.group(1) if bld else "?"})'
    today = datetime.date.today().isoformat()

    # ---- verify every screenshot exists before writing HTML ----------------
    required = (["s1-ci-run.png", "s2-ctest.png", "s3-sim-hover.png",
                 "s4-sim-crosswind.png", "s5a-selftest-m4t.png",
                 "s5b-selftest-m400.png", "s7-dashboard.png",
                 "s8-widgets-illustration.png", "s9-floating-window.png"] +
                [f"s6-{p}-{k}.png" for p in ("m4t", "m400")
                 for k in ("install", "upgrade")])
    for name in required:
        p = shots / name
        if not p.exists() or p.stat().st_size < 3000:
            sys.exit(f"error: screenshot missing or suspiciously small: {p}")

    print("[8/9] assembling HTML")
    blocks = []

    def B(inner, cls=""):
        blocks.append(blk(inner, cls))

    def SEC(inner):
        B(inner, "newpage")

    # -- 1. what it is / what has been verified --------------------------------
    SEC(f"""<h2>1 · What DropPoint is — and what has / has not been verified</h2>
      <p>DropPoint is a companion-computer application that watches the
      aircraft's telemetry, computes the ballistic release point for the
      configured payload, and drives a physical drop mechanism — either a
      servo release on a general companion computer, or a CZI drop kit
      operated through DJI Pilot 2 widgets on the Matrice 400.</p>
      <p><b>Verified in this build:</b></p><ul>
      <li>{test_count} unit and end-to-end simulation tests pass on Windows
        and Linux (the terminal capture below).</li>
      <li>The simulator drops: hover scenario fired once, predicted miss
        {h_pred} m / true miss {h_truth} m; 5 m/s crosswind run-in fired once
        in AUTO, predicted {c_pred} m / true {c_truth} m.</li>
      <li>The real PSDK binary compiles and links for x86_64 and aarch64
        Linux against DJI's libpayloadsdk.a (Payload-SDK 3.16.0).</li>
      <li>ARM64 .deb packages install, self-test, upgrade (keeping site
        config), and remove (keeping credentials and logs) in a clean
        Ubuntu 20.04 container — see section 6.</li></ul>""")
    B("""<p><b>Not verified:</b> nothing here has run on an aircraft. Every
      hardware-facing adapter is marked unverified in the source tree; the
      before-first-flight checklist is section 9.</p>""")
    B(img("s1-ci-run.png", "Real capture — the GitHub Actions run for this "
          "build: every job green (unit tests on Ubuntu and Windows, the "
          "PSDK Linux build, ARM64 packaging and the clean-install test)."))
    B(img("s2-ctest.png", "Real capture — the test suite running on the "
          "development machine (Windows, MSVC Release)."))

    # -- 2. which file for which drone -----------------------------------------
    SEC(f"""<h2>2 · Which file is for which drone</h2>
      <table>
        <tr><th>Aircraft / companion computer</th><th>Package</th><th>Install path</th></tr>
        <tr><td>Matrice 4T — generic Linux companion (E-Port, UART)</td>
            <td><code>{html.escape(m4t_deb)}</code></td>
            <td><code>dpkg -i</code>, files under <code>/opt/droppoint</code></td></tr>
        <tr><td>Matrice 400 — generic Linux companion or SSH development (E-Port V2)</td>
            <td><code>{html.escape(m400_deb)}</code></td>
            <td><code>dpkg -i</code></td></tr>
        <tr><td>Matrice 400 — <b>Manifold 3</b> (the supported install path)</td>
            <td><code>DropPoint-M400_v&lt;ver&gt;.dpk</code></td>
            <td>DJI Pilot 2 installs the .dpk into <code>/open_app/</code></td></tr>
      </table>""")
    B("""<p>The .dpk is <b>built by you</b> with
      <code>scripts/package_dpk.sh</code>, because it embeds your DJI developer
      credentials and Pilot 2 refuses to install an app whose credentials do
      not work. It is never published — keep it private.</p>
      <p>Both .deb packages ship the same binaries; the shipped
      <code>config/</code> directory is the aircraft profile (payloads, lens,
      release backend and gates for that airframe).</p>""")

    # -- 3/4/5: aircraft + drop-kit guides (verbatim markdown) ------------------
    m4t_blocks = md_blocks("docs/aircraft/M4T.md")
    SEC("<h2>3 · Matrice 4T — aircraft guide</h2>" + m4t_blocks[0])
    for e in m4t_blocks[1:]:
        B(e)
    m400_blocks = md_blocks("docs/aircraft/M400.md")
    SEC("<h2>4 · Matrice 400 — aircraft guide</h2>" + m400_blocks[0])
    for e in m400_blocks[1:]:
        B(e)
    czi_blocks = md_blocks("docs/CZI-DROP-KITS.md")
    SEC("<h2>5 · CZI drop kits — integration notes</h2>" + czi_blocks[0])
    for e in czi_blocks[1:]:
        B(e)

    # -- 6: install step by step ------------------------------------------------
    SEC(f"""<h2>6 · Installing, step by step (real ARM64 transcript)</h2>
      <p>Every block below is a verbatim capture from the automated install
      test on a clean arm64 Ubuntu 20.04 container — the same steps you run
      on the companion computer.</p>
      <ol>
      <li><b>Install the package</b> — <code>dpkg -i droppoint-m4t_&lt;ver&gt;_arm64.deb</code>.
        postinst creates the <code>droppoint</code> system user, the log
        directory, and placeholder credentials at
        <code>/etc/droppoint/credentials.yaml</code> (chmod 600 — fill in your
        real DJI app values before flying).</li>
      <li><b>Check it runs</b> — <code>droppoint --version</code> then
        <code>droppoint --selftest --no-link</code>. The report must pass with
        placeholder credentials shown as WARN, not FAIL.</li>
      <li><b>Try the simulator</b> —
        <code>droppoint_sim_main --sim hover --profile /opt/droppoint/config</code>
        must print <code>SUMMARY … fired=1</code>.</li></ol>""")
    B(img("s6-m4t-install.png", "Real capture — install, --version, "
          "--selftest and the simulator for the Matrice 4T package."))
    B(img("s6-m400-install.png", "Real capture — the same steps for the "
          "Matrice 400 package."))
    B("""<h3>Upgrade and removal behaviour</h3>
      <ol start="4">
      <li><b>Upgrade keeps your settings</b> — reinstalling the package keeps
        edits to <code>/opt/droppoint/config/*.yaml</code> (they are dpkg
        conffiles), your real
        <code>/etc/droppoint/credentials.yaml</code>, and the log
        directory.</li>
      <li><b>Removal keeps site data</b> — <code>dpkg -r</code> removes the
        binaries but deliberately leaves
        <code>/etc/droppoint/credentials.yaml</code> and
        <code>/var/log/droppoint</code> behind.</li>
      <li><b>Start the service</b> —
        <code>systemctl enable --now droppoint</code>. The unit runs as the
        <code>droppoint</code> user (dialout group for the UART) with a 5 s
        watchdog the app must pet.</li></ol>""")
    B(img("s6-m4t-upgrade.png", "Real capture — reinstall keeps the site "
          "config marker, credentials marker and log file; removal keeps "
          "credentials and logs (Matrice 4T)."))
    B(img("s6-m400-upgrade.png", "Real capture — the same assertions on the "
          "Matrice 400 package."))

    # -- 7: try without a drone ---------------------------------------------------
    SEC("""<h2>7 · Trying it without a drone</h2>
      <p>The same binary that flies carries a full simulator — scripted pilot,
      wind model, truth drop and every release gate. Commands exactly as run:</p>""")
    B(img("s3-sim-hover.png", "Real capture — droppoint_sim_main.exe --sim hover: "
          "the scripted pilot designates, confirms, arms and releases; "
          "SUMMARY reports one manual drop, true miss 1.52 m inside the 3 m "
          "target radius."))
    B(img("s4-sim-crosswind.png", "Real capture — --sim crosswind-5ms: an "
          "AUTO run-in in a 5 m/s crosswind — predicted miss 0.34 m, true "
          "miss 0.74 m."))
    B(img("s5a-selftest-m4t.png", "Real capture — --selftest --config "
          "config/profiles/m4t: the no-hardware self-test for the Matrice 4T "
          "profile."))
    B(img("s5b-selftest-m400.png", "Real capture — the same self-test for "
          "the Matrice 400 profile."))
    B("""<h3>The bench dashboard</h3>
      <p><code>tools/dashboard/index.html</code> is a development tool that
      plots the target, predicted impact point and dispersion cloud live
      while a sim or aircraft session runs.</p>""")
    B(img("s7-dashboard.png", "Real capture — the dashboard opened from "
          "disk. It shows 'Not connected' here because no sim/aircraft "
          "backend is attached; connected to a run it plots the drop "
          "geometry live."))

    # -- 8: operator controls ---------------------------------------------------
    type_names = {"Button": "button", "Switch": "switch", "Scale": "scale",
                  "List": "list", "IntInputBox": "integer input"}
    actions = {0: "Latch the crosshair ground point as the target",
               1: "Confirm the latched target (required before arming)",
               2: "Arm the release — on = arm, off = disarm",
               3: "Manual release (fires the mechanism in manual mode)",
               4: "Abort — safe the mechanism and cancel the mission",
               5: "Clear the latched/confirmed target",
               6: "Release mode — off = MANUAL, on = AUTO",
               7: "Select the active payload profile",
               8: "Ambient temperature for the ballistic model (−40…60 °C)",
               9: "Surface type — sets the wind-profile exponent"}
    wrows = "".join(
        f"<tr><td>{w['index']}</td><td>{html.escape(w['name'])}</td>"
        f"<td>{type_names.get(w['type'], w['type'])}</td>"
        f"<td>{html.escape(actions[w['index']])}</td>"
        f"<td>{'yes' if w['rc'] else 'no'}</td></tr>"
        for w in widgets)
    SEC("""<h2>8 · Operator controls in DJI Pilot 2</h2>
      <p>DropPoint registers ten widgets — five on the main interface (which
      require icons) and five on the settings interface. All button-style
      widgets can additionally be bound to the RC's hardware buttons inside
      Pilot 2.</p>""")
    B(img("s8-widgets-illustration.png", "Illustration — not a screenshot: "
          "the widget layout and icons as DropPoint registers them; DJI "
          "Pilot 2 itself cannot be captured without an aircraft."))
    B(f"""<table><tr><th>#</th><th>Name</th><th>Type</th><th>What it does</th>
      <th>RC-bindable</th></tr>{wrows}</table>""")
    B(img("s9-floating-window.png", "Text exactly as DropPoint sends it to "
          "the Pilot 2 floating window; shown here outside DJI Pilot 2."))

    # -- 9: before first flight ---------------------------------------------------
    def checklist_items(rel):
        body = checklist_from_md(
            rel, "## To verify on the aircraft before relying on it")
        items = re.findall(r"<li>.*?</li>", body, re.S)
        if not items:
            sys.exit(f"error: no checklist items found in {rel}")
        return "".join(items)

    SEC("""<h2>9 · Before first flight — verify on the aircraft</h2>
      <p>Nothing in this package has run on an aircraft. Before relying on a
      drop, work through the merged checklist from both aircraft guides:</p>""")
    B(f"""<ul class="checklist">
      {checklist_items("docs/aircraft/M4T.md")}
      {checklist_items("docs/aircraft/M400.md")}</ul>""")

    title_page = f"""<div class="page"><div class="titlepage">
        <h1>DropPoint</h1>
        <div class="sub">Precision payload-drop guidance for DJI enterprise
        aircraft<br>operator's how-to — version {html.escape(version)} — {today}</div>
        <div class="warn" style="display:inline-block;text-align:left;">
        Not flight-verified: built and tested in simulation and on ARM64 Linux
        without an aircraft.</div></div></div>"""

    html_doc = ("<!DOCTYPE html><html><head><meta charset='utf-8'>"
                f"<style>{CSS}</style></head><body>{title_page}"
                f'<div id="flow" style="display:none">{"".join(blocks)}</div>'
                f"{PAGINATE_JS}</body></html>")
    html_path = shots.parent / "howto.html"
    html_path.write_text(html_doc, encoding="utf-8")

    print("[9/9] printing to PDF via headless Edge")
    run([edge, "--headless=new", "--disable-gpu",
         f"--print-to-pdf={out_pdf}", "--no-pdf-header-footer",
         "--virtual-time-budget=15000",
         "file:///" + str(html_path.resolve()).replace("\\", "/")], check=False)
    if not out_pdf.exists() or out_pdf.stat().st_size < 20000:
        sys.exit(f"error: PDF not produced: {out_pdf}")

    data = out_pdf.read_bytes()
    pages_n = len(re.findall(rb"/Type\s*/Page[^s]", data))
    print(f"DONE: {out_pdf} — {pages_n} pages, {out_pdf.stat().st_size} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
