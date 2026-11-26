#!/usr/bin/env python3
# tools/pdf_common.py — shared machinery for the PDF builders
# (tools/make_howto_pdf.py and tools/make_manual_pdf.py):
# headless-Edge invocation with fresh-output guarantees, terminal-style PNG
# rendering, real-output capture helpers, and the A4 CSS + JS paginator.
import datetime
import html
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

import markdown
from PIL import Image, ImageDraw, ImageFont

REPO = Path(__file__).resolve().parent.parent
SCRIPT_START = time.time()

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


def term_image(title: str, text: str, out: Path, max_rows: int = 40,
               max_cols: int = 110):
    """Render verbatim terminal output as a dark terminal-style PNG.
    Lines wider than max_cols are hard-wrapped with a visible '…'
    continuation marker instead of shrinking the font below legibility."""
    raw = text.replace("\r", "").split("\n")
    while raw and not raw[-1].strip():
        raw.pop()
    lines = []
    for ln in raw:
        while len(ln) > max_cols:
            lines.append(ln[:max_cols - 1] + "…")
            ln = "… " + ln[max_cols - 1:]
        lines.append(ln)
    if len(lines) > max_rows:
        keep_head = max_rows - 9
        omitted = len(lines) - keep_head - 8
        lines = lines[:keep_head] + [f"        … {omitted} lines omitted …"] + lines[-8:]

    pad, bar_h, width = 14, 34, 1600
    # shrink the font until the longest line fits — a clipped line would lie
    # about what the command really printed (e.g. the wide --list-kits table)
    max_chars = max((len(ln) for ln in lines), default=1)
    size = min(17, max(9, int((width - 2 * pad) / (0.62 * max_chars))))
    body_font = font("consola.ttf", size)
    line_h = int(size * 1.32)
    title_font = font("consolab.ttf", 16) if (FONT_DIR / "consolab.ttf").exists() \
        else font("consola.ttf", 16)
    height = bar_h + pad + line_h * len(lines) + pad
    img_ = Image.new("RGB", (width, height), "#0c0c0c")
    d = ImageDraw.Draw(img_)
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
    img_.save(out)


def require_fresh(path: Path, label: str):
    """A generated artifact must exist, be non-empty, and be newer than the
    script start — a stale file from an earlier run must never pass."""
    if not path.exists() or path.stat().st_size == 0:
        sys.exit(f"error: {label} missing or empty: {path}")
    if path.stat().st_mtime < SCRIPT_START - 1:
        sys.exit(f"error: {label} is stale (not regenerated this run): {path}")


def run_edge(edge: str, args: list, out: Path, label: str, timeout_s: int = 60):
    """Run headless Edge and require a FRESH `out` file.

    A dedicated throwaway --user-data-dir is mandatory: without it, when a
    normal Edge session is already running, the headless launch just hands
    the command line to that instance and exits having done nothing —
    silently producing no screenshot/PDF."""
    out = out.resolve()  # Chromium resolves --screenshot/--print-to-pdf
                         # relative to its own cwd/profile, not ours
    if out.exists():
        out.unlink()
    out.parent.mkdir(parents=True, exist_ok=True)
    profile = tempfile.mkdtemp(prefix="dp-edge-profile-")
    cmd = [edge, "--headless=new", "--disable-gpu",
           f"--user-data-dir={profile}",
           "--no-first-run", "--disable-extensions"] + \
          [str(a).replace("{OUT}", str(out)) for a in args]
    print(f"  $ {' '.join(cmd)}")
    t0 = time.time()
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True,
                              encoding="utf-8", errors="replace",
                              timeout=timeout_s)
        rc, so, se = proc.returncode, proc.stdout, proc.stderr
    except subprocess.TimeoutExpired as e:
        rc, so, se = -9, e.stdout or "", f"TIMEOUT after {timeout_s}s"
    # the browser can return a hair before the file hits disk — poll briefly
    deadline = t0 + 10
    while time.time() < deadline and not (out.exists() and out.stat().st_size > 0):
        time.sleep(0.25)
    elapsed = time.time() - t0
    print(f"    -> exit {rc}, {elapsed:.1f}s")
    if not (out.exists() and out.stat().st_size > 0
            and out.stat().st_mtime >= SCRIPT_START - 1):
        sys.exit(f"error: Edge produced no fresh output for {label}\n"
                 f"  out: {out}\n  exit={rc} after {elapsed:.1f}s\n"
                 f"  stdout: {str(so).strip()}\n  stderr: {str(se).strip()}")
    shutil.rmtree(profile, ignore_errors=True)


def edge_shot(edge: str, url: str, out: Path, budget_ms: int = 20000):
    run_edge(edge, ["--screenshot={OUT}", "--window-size=1600,1000",
                    f"--virtual-time-budget={budget_ms}", url],
             out, f"screenshot of {url}")


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
    img_ = Image.new("RGB", (W, H), "#15181d")
    d = ImageDraw.Draw(img_)
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
                img_.paste(icon, (cx - 36, y + 16), icon)
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
    img_.save(out)
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
    box_w, gap = 740, 22
    body = font("consola.ttf", 20)  # fits the longest real HUD line (55 ch)
    line_h = 30
    cols = 2
    rows = (len(huds) + cols - 1) // cols
    box_h = 16 + line_h * 5
    W = 40 + cols * (box_w + gap)
    H = 96 + rows * (box_h + gap)
    img_ = Image.new("RGB", (W, H), "#15181d")
    d = ImageDraw.Draw(img_)
    d.text((24, 16), "Floating-window text — exactly as DropPoint sends it",
           font=font("consolab.ttf", 30), fill="#e8e8e8")
    d.text((24, 54), "rendered here outside DJI Pilot 2",
           font=font("consola.ttf", 17), fill="#8b949e")
    for i, s in enumerate(huds):
        col, row = i % cols, i // cols
        x = 20 + col * (box_w + gap)
        y = 90 + row * (box_h + gap)
        d.rounded_rectangle([x, y, x + box_w, y + box_h], radius=8,
                            fill="#3a3f45", outline="#6e7681", width=2)
        for j, ln in enumerate(s.split("\n")[:5]):
            d.text((x + 16, y + 10 + j * line_h), ln, font=body, fill="#e8e8e8")
    img_.save(out)
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
.diagram svg { display: block; width: 100%; height: auto; }
.draftbox { border: 2pt dashed #999; background: #e8e8e8; color: #555;
            text-align: center; font-size: 14pt; font-weight: bold;
            padding: 18mm 4mm; margin: 2mm auto; }
.caption { font-size: 8.8pt; color: #555; text-align: center;
           margin: -1mm 0 3mm; }
table { border-collapse: collapse; width: 100%; margin: 2mm 0 3mm;
        font-size: 9.2pt; }
th, td { border: 1px solid #c8cdd4; padding: 1.6mm 2.4mm; text-align: left;
         overflow-wrap: anywhere; }
/* header cells may wrap at spaces but never mid-word ("Qty" must not
   become "Qt/y"): break-word keeps the column's min-content at the
   longest word instead of collapsing to one char like anywhere does */
th { background: #eef2f7; overflow-wrap: break-word; }
/* the drop-kit tables are 9 columns — fixed layout + small font keeps
   every column inside A4 portrait */
table.kits { table-layout: fixed; font-size: 7.4pt; }
table.kits th, table.kits td { padding: 1mm 1.1mm; overflow-wrap: break-word; }
/* ids like `cybowindow_4hook` and `psdk_widget` must not split mid-token */
table.kits code { font-size: 6.6pt; }
table.kits td code { padding: 0; }
table.kits th:nth-child(1) { width: 16%; }
table.kits th:nth-child(2) { width: 10%; }
table.kits th:nth-child(3) { width: 12%; }
table.kits th:nth-child(4) { width: 7%; }
table.kits th:nth-child(5) { width: 8%; }
table.kits th:nth-child(6) { width: 11%; }
table.kits th:nth-child(7) { width: 11%; }
table.kits th:nth-child(8) { width: 11%; }
table.kits th:nth-child(9) { width: 14%; }
code { font-family: Consolas, monospace; font-size: 9pt;
       background: #f2f4f7; padding: 0 1mm; border-radius: 1mm;
       overflow-wrap: anywhere; }
pre { font-family: Consolas, monospace; font-size: 8.6pt; background: #f6f8fa;
      border: 1px solid #d0d7de; border-radius: 2mm; padding: 3mm;
      white-space: pre-wrap; overflow-wrap: anywhere; }
.titlepage { text-align: center; padding-top: 60mm; }
.titlepage h1 { font-size: 34pt; border: none; }
.sub { font-size: 13pt; color: #444; margin-bottom: 14mm; }
.warn { border: 1.5pt solid #b58b00; background: #fff8e1; padding: 3mm 4mm;
        border-radius: 2mm; font-weight: 600; }
.checklist li { list-style: none; margin-left: -4mm; }
.checklist li:before { content: "☐  "; }
.toc { font-size: 10.5pt; }
.toc .t2 { display: flex; margin: 1.4mm 0; font-weight: 600; }
.toc .t3 { display: flex; margin: 0.6mm 0 0.6mm 7mm; color: #333; }
.toc .dots { flex: 1; border-bottom: 1px dotted #999; margin: 0 2mm 1mm; }
.toc .pg { font-variant-numeric: tabular-nums; }
/* flow blocks: real pagination is done by JS measuring scrollHeight */
.blk { overflow: hidden; margin: 0; }
"""


def paginate_js(footer: str) -> str:
    return """
<script>
window.addEventListener('load', function () {
  var flow = document.getElementById('flow');
  var blocks = Array.prototype.slice.call(flow.children);
  flow.parentNode.removeChild(flow);
  var pageNum = document.querySelectorAll('.page').length;  // fixed pages first
  var cur = null, limit = 0, used = 0;
  function newPage() {
    pageNum++;
    cur = document.createElement('div');
    cur.className = 'page';
    var pn = document.createElement('div');
    pn.className = 'pageno';
    pn.textContent = 'FOOTER_TEXT \\u2014 page ' + pageNum;
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
      // a small trailing block is pulled onto this page (scaled) instead of
      // being stranded alone on the next one — images may squeeze a bit more
      var avail = limit - used;
      var minS = b.querySelector('img') ? 0.5 : 0.65;
      if (bh <= avail / minS) {
        var s = avail / bh;
        b.style.transformOrigin = 'top left';
        b.style.transform = 'scale(' + s + ')';
        b.style.width = (100 / s) + '%';
        used = limit;
      } else {
        cur.removeChild(b);
        newPage();
        cur.appendChild(b);
        used = shrink(b);
      }
    } else if (!had && bh > limit) {
      used = shrink(b);
    } else {
      used += bh;
    }
  }
});
</script>
""".replace("FOOTER_TEXT", footer)


def blk(inner: str, cls: str = "") -> str:
    c = "blk" + (" " + cls if cls else "")
    return f'<div class="{c}">{inner}</div>'


def img(rel_name: str, caption: str) -> str:
    return (f'<img class="shot" src="screens/{rel_name}">'
            f'<div class="caption">{html.escape(caption)}</div>')


def draft_img(name: str, caption: str) -> str:
    """Grey CAPTURE PENDING box for --draft builds where a real capture
    cannot be produced yet."""
    return (f'<div class="draftbox">CAPTURE PENDING — {html.escape(name)}</div>'
            f'<div class="caption">{html.escape(caption)}</div>')


# Longest tag names first: with 'p' before 'pre', "<pre>" would be captured
# as tag 'p' + '[^>]*'="re" and its </p> backreference would run past the
# real </pre>, swallowing every heading until the next literal </p>.
TOP_ELEM_RE = re.compile(
    r'<(blockquote|figure|table|pre|div|img|h\d|ul|ol|p)[^>]*>.*?</\1>'
    r'|<hr[^>]*/?>|<img[^>]*>', re.S)


def check_ol_numbering(md_text: str, html_text: str, label: str):
    """Every number written in the markdown must be the number the HTML
    renders (python-markdown's sane_lists emits <ol start=N> for lists whose
    first item is not 1). Compared as flat sequences — a nested list keeps
    its own numbering, matching its position in the written sequence."""
    body = re.sub(r"^```.*?^```", "", md_text, flags=re.S | re.M)
    written = [int(n) for n in re.findall(r"^[ ]{0,3}(\d+)[.)]\s", body, re.M)]
    shown, stack = [], []
    for m in re.finditer(r"<(/?)(ol|ul|li)\b[^>]*>", html_text):
        closing, tag, whole = m.group(1), m.group(2), m.group(0)
        if tag in ("ol", "ul"):
            if closing:
                if stack:
                    stack.pop()
            else:
                s = re.search(r'start="(\d+)"', whole)
                stack.append((tag, int(s.group(1)) if s else 1))
        elif tag == "li" and not closing and stack and stack[-1][0] == "ol":
            _, n = stack[-1]
            shown.append(n)
            stack[-1] = ("ol", n + 1)
    if shown != written:
        sys.exit(f"error: ordered-list numbering in {label} does not match "
                 f"the markdown source\n  written: {written}\n  shown:    {shown}")


def md_blocks(rel: str) -> list:
    """Render a markdown file to HTML split into top-level block elements.
    Each heading is merged with the element that follows it so a heading can
    never be orphaned at the bottom of a page."""
    md_text = read(REPO / rel)
    h = markdown.markdown(md_text,
                          extensions=["tables", "fenced_code", "sane_lists"])
    check_ol_numbering(md_text, h, rel)
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
