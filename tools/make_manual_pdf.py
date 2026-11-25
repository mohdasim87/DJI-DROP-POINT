#!/usr/bin/env python3
# tools/make_manual_pdf.py
#
# Builds docs/DropPoint-Setup-Manual.pdf from docs/SETUP-MANUAL.md. Every
# terminal capture is a REAL one (same rules as the how-to PDF): install
# transcripts re-rendered verbatim, the widget panel labelled as an
# illustration, the floating-window text captured from real sim runs.
# Diagrams come from hand-written SVGs in docs/diagrams/.
#
# Placeholders on their own line in the markdown:
#   {{diagram:NAME|caption}}  ->  docs/diagrams/NAME.svg inlined at text width
#   {{shot:NAME|caption}}     ->  docs/howto/screens/NAME.png
#
# Usage:
#   python tools/make_manual_pdf.py [--install-logs DIR] [--run-url URL]
#       [--out docs/DropPoint-Setup-Manual.pdf] [--shots docs/howto/screens]
#       [--build-dir build-h2] [--draft]
#
# --draft: a capture that cannot be produced (missing install log, missing
# build) is replaced by a grey CAPTURE PENDING box; the script still exits 0
# and prints DRAFT. Without --draft every missing input is fatal.
import argparse
import datetime
import html as html_mod
import re
import sys
import tempfile
from pathlib import Path

import markdown
import pymupdf

from pdf_common import (REPO, read, run, term_image, require_fresh, find_edge,
                        run_edge, widget_table, make_widget_illustration,
                        hud_strings, make_hud_image, install_log_parts,
                        CSS, paginate_js, blk, img, draft_img, TOP_ELEM_RE,
                        check_ol_numbering)

MANUAL_MD = REPO / "docs/SETUP-MANUAL.md"
DIAGRAMS = REPO / "docs" / "diagrams"
# chapters that must open on a fresh page (per the layout spec)
NEWPAGE_CHAPTERS = {5, 6, 7, 11}
FOOTER = "DropPoint Setup Manual"


def manual_blocks(md_text: str, media_for) -> list:
    """Markdown -> list of top-level HTML elements, placeholders resolved
    through media_for(kind, name, caption) -> html. Headings are merged with
    the following element so they can never be orphaned."""
    sentinels = {}

    def sub(m):
        key = f"@@DPMEDIA{len(sentinels)}@@"
        sentinels[key] = m.groups()
        return f"\n\n{key}\n\n"

    processed = re.sub(
        r"\{\{(diagram|shot):([^|}]+)\|([^}]*)\}\}", sub, md_text)
    leftover = re.findall(r"\{\{[^}]*\}\}", processed)
    if leftover:
        sys.exit(f"error: unresolved placeholders in SETUP-MANUAL.md: {leftover}")

    h = markdown.markdown(processed,
                          extensions=["tables", "fenced_code", "sane_lists"])
    check_ol_numbering(processed, h, "SETUP-MANUAL.md")
    for key, (kind, name, cap) in sentinels.items():
        marker = f"<p>{key}</p>"
        if marker not in h:
            sys.exit(f"error: placeholder {name} did not survive markdown")
        h = h.replace(marker, media_for(kind, name.strip(), cap.strip()))

    elems = [m.group(0) for m in TOP_ELEM_RE.finditer(h)]
    if re.sub(r"\s", "", "".join(elems)) != re.sub(r"\s", "", h):
        sys.exit("error: markdown block split lost content in SETUP-MANUAL.md")
    merged, i = [], 0
    while i < len(elems):
        if re.match(r"<h\d", elems[i]) and i + 1 < len(elems):
            merged.append(elems[i] + elems[i + 1])
            i += 2
        else:
            merged.append(elems[i])
            i += 1
    return merged


def heading_entries(elems: list) -> list:
    """(level, text, elem_index) for every h2/h3 — the TOC's source data."""
    out = []
    for i, e in enumerate(elems):
        for m in re.finditer(r"<h([23])>(.*?)</h\1>", e, re.S):
            text = re.sub(r"<[^>]+>", "", m.group(2)).strip()
            out.append((int(m.group(1)), text, i))
    return out


HEADING_COLOR = 0x0A3069  # h2/h3 colour in the CSS


def _page_heading_text(page) -> str:
    """Text of lines containing a heading-coloured span. Body text and
    table headers stay black, so heading titles that also appear verbatim
    inside a table (the build names in 'Choose your build') do not fake a
    TOC hit on the wrong page."""
    out = []
    for b in page.get_text("dict")["blocks"]:
        for l in b.get("lines", []):
            if any(s["color"] == HEADING_COLOR for s in l["spans"]):
                out.append("".join(s["text"] for s in l["spans"]))
    return " ".join(out)


def find_heading_pages(pdf_path: Path, titles: list, skip_pages: int) -> list:
    """For each title, the first page index >= skip_pages containing it."""
    doc = pymupdf.open(str(pdf_path))
    pages = list(range(skip_pages, doc.page_count))
    heading_text = {p: _page_heading_text(doc[p]) for p in pages}
    doc.close()
    found = []
    for t in titles:
        needle = t
        hit = None
        while needle and hit is None:
            for p in pages:
                if needle in heading_text[p]:
                    hit = p
                    break
            needle = needle[:-1]  # em-dashes/hyphens may reflow
        if hit is None:
            sys.exit(f"error: heading '{t}' not found in the generated PDF")
        found.append(hit)
    return found


def toc_html(entries: list, pages: list) -> str:
    rows = []
    for (level, title, _), pg in zip(entries, pages):
        cls = "t2" if level == 2 else "t3"
        rows.append(f'<div class="{cls}"><span>{html_mod.escape(title)}</span>'
                    f'<span class="dots"></span>'
                    f'<span class="pg">{pg + 1}</span></div>')
    return ('<div class="page"><div class="pageno">'
            f'{FOOTER} — page 2</div><h2>Contents</h2>'
            f'<div class="toc">{"".join(rows)}</div></div>')


def main() -> int:
    ap = argparse.ArgumentParser(description="Build docs/DropPoint-Setup-Manual.pdf")
    ap.add_argument("--install-logs",
                    default=r"C:\Users\DELL\AppData\Local\Temp\dp\art4")
    ap.add_argument("--run-url",
                    default="https://github.com/mohdasim87/DJI-DROP-POINT/actions/runs/")
    ap.add_argument("--out", default=str(REPO / "docs/DropPoint-Setup-Manual.pdf"))
    ap.add_argument("--shots", default=str(REPO / "docs/howto/screens"))
    ap.add_argument("--build-dir", default=str(REPO / "build-h2"))
    ap.add_argument("--draft", action="store_true")
    args = ap.parse_args()

    shots = Path(args.shots)
    out_pdf = Path(args.out)
    shots.mkdir(parents=True, exist_ok=True)
    draft = args.draft
    edge = find_edge()
    build = Path(args.build_dir)
    sim_exe = build / "src/app/Release/droppoint_sim_main.exe"
    logs_dir = Path(args.install_logs) / "install-logs"
    pending = set()  # capture names that could not be produced this run

    # ---- captures ----------------------------------------------------------
    print("[1/4] captures")
    m4t_log = logs_dir / "install-m4t.log"
    if m4t_log.exists():
        inst, upg = install_log_parts(m4t_log)
        term_image("arm64v8/ubuntu:20.04 — dpkg -i + --version + --selftest (m4t)",
                   inst, shots / "s6-m4t-install.png", max_rows=44)
        term_image("arm64v8/ubuntu:20.04 — reinstall (upgrade) + dpkg -r (m4t)",
                   upg, shots / "s6-m4t-upgrade.png", max_rows=30)
    else:
        pending.update({"s6-m4t-install.png", "s6-m4t-upgrade.png"})

    m400_log = logs_dir / "install-m400.log"
    if m400_log.exists():
        text = read(m400_log)
        m = re.search(r"=== make-dpk ===(.*?)=== end make-dpk ===", text, re.S)
        if m:
            term_image("arm64v8/ubuntu:20.04 — droppoint-make-dpk as an "
                       "unprivileged user (m400)", m.group(1),
                       shots / "s11-make-dpk.png", max_rows=40)
        else:
            pending.add("s11-make-dpk.png")
            print("    note: no '=== make-dpk ===' span in install-m400.log")
    else:
        pending.add("s11-make-dpk.png")

    if sim_exe.exists():
        widgets = widget_table()
        make_widget_illustration(widgets, shots)
        make_hud_image(hud_strings(build, sim_exe), shots)
        kits_out = run([sim_exe, "--list-kits"], cwd=REPO)
        term_image("> droppoint_sim_main.exe --list-kits", kits_out,
                   shots / "s10-list-kits.png", max_rows=30)
    else:
        pending.update({"s8-widgets-illustration.png",
                        "s9-floating-window.png", "s10-list-kits.png"})

    if pending and not draft:
        sys.exit(f"error: cannot produce captures: {sorted(pending)} "
                 f"(run with --draft to substitute CAPTURE PENDING boxes)")
    if pending:
        print(f"    DRAFT: missing captures -> {sorted(pending)}")

    transcript_build_ids = set()
    for profile in ("m4t", "m400"):
        p = logs_dir / f"install-{profile}.log"
        if p.exists():
            m = re.search(r"droppoint\s+\S+\s+\(build\s+([^\s)]+)\)", read(p))
            if m:
                transcript_build_ids.add(m.group(1))

    # ---- title-page facts ----------------------------------------------------
    try:
        ver_h = read(build / "generated/util/version.h")
        ver = re.search(r'kVersionString\s*=\s*"([^"]+)"', ver_h)
        bld = re.search(r'kBuildVersion\s*=\s*"([^"]+)"', ver_h)
        version = (f'{ver.group(1) if ver else "?"} '
                   f'(build {bld.group(1) if bld else "?"})')
    except SystemExit:
        version = "unknown (DRAFT)" if draft else "unknown"

    # ---- manual blocks -------------------------------------------------------
    print("[2/4] manual markdown")
    md_text = read(MANUAL_MD)

    def media_for(kind: str, name: str, cap: str) -> str:
        if kind == "diagram":
            svg_path = DIAGRAMS / f"{name}.svg"
            if not svg_path.exists():
                sys.exit(f"error: diagram missing: {svg_path}")
            svg = read(svg_path)
            return (f'<div class="diagram">{svg}</div>'
                    f'<div class="caption">{html_mod.escape(cap)}</div>')
        fname = f"{name}.png"
        if fname in pending or not (shots / fname).exists():
            if draft:
                return draft_img(fname, cap)
            sys.exit(f"error: capture missing: {shots / fname}")
        return img(fname, cap)

    elems = manual_blocks(md_text, media_for)
    entries = heading_entries(elems)

    blocks = []
    for e in elems:
        m = re.match(r"<h2>(\d+)\.", e)
        cls = "newpage" if m and int(m.group(1)) in NEWPAGE_CHAPTERS else ""
        blocks.append(blk(e, cls))

    # ---- HTML assembly --------------------------------------------------------
    ver_esc = html_mod.escape(version)
    today = datetime.date.today().isoformat()
    # the captures carry the CI build id inside their pixels; the PDF text
    # layer must name it too (self-check + provenance for the reader)
    build_line = (f"<br>install transcripts captured from CI build "
                  f"{html_mod.escape(sorted(transcript_build_ids)[0])}"
                  if transcript_build_ids else "")
    title_page = f"""<div class="page"><div class="titlepage">
        <h1>DropPoint</h1>
        <div class="sub">Setup manual — hardware, wiring and
        installation<br>version {ver_esc} — {today}{build_line}</div>
        <div class="warn" style="display:inline-block;text-align:left;">
        Not flight-verified: built and tested in simulation and on ARM64 Linux
        without an aircraft.</div></div></div>"""

    html_path = shots.parent / "setup-manual.html"

    def assemble(toc: str) -> str:
        doc = ("<!DOCTYPE html><html><head><meta charset='utf-8'>"
               f"<style>{CSS}</style></head><body>{title_page}{toc}"
               f'<div id="flow" style="display:none">{"".join(blocks)}</div>'
               f"{paginate_js(FOOTER)}</body></html>")
        html_path.write_text(doc, encoding="utf-8")
        require_fresh(html_path, "generated HTML")
        return doc

    def print_pdf(target: Path):
        run_edge(edge,
                 ["--print-to-pdf={OUT}", "--no-pdf-header-footer",
                  "--virtual-time-budget=15000",
                  "file:///" + str(html_path.resolve()).replace("\\", "/")],
                 target, "print-to-pdf")
        if target.stat().st_size < 20000:
            sys.exit(f"error: PDF suspiciously small: {target}")

    # ---- two-pass TOC ---------------------------------------------------------
    print("[3/4] layout pass 1 (locating chapters)")
    assemble("")
    tmp_pdf = Path(tempfile.mkdtemp(prefix="dp-manual-")) / "pass1.pdf"
    print_pdf(tmp_pdf)
    pages0 = find_heading_pages(tmp_pdf, [t for _, t, _ in entries],
                                skip_pages=1)
    toc_pages = 1
    listed = [p + toc_pages for p in pages0]

    print("[4/4] layout pass 2 (TOC + final PDF)")
    for attempt in range(3):
        assemble(toc_html(entries, listed))
        print_pdf(out_pdf)
        actual = find_heading_pages(out_pdf, [t for _, t, _ in entries],
                                    skip_pages=1 + toc_pages)
        if actual == listed:
            break
        listed = actual  # TOC page count differed from the guess — converge
    else:
        sys.exit("error: TOC page numbers did not converge")

    # ---- self-checks -----------------------------------------------------------
    doc = pymupdf.open(str(out_pdf))
    pdf_text = re.sub(r"\s+", " ", "".join(p.get_text() for p in doc))
    if "{{" in pdf_text:
        sys.exit("error: an unresolved {{placeholder}} leaked into the PDF")
    # every table in the md: its first header cell must appear in the text
    table_headers = re.findall(r"^\|([^|\n]+)\|[^\n]*\n\|[-: |]+\|", md_text, re.M)
    for cell in table_headers:
        cell_txt = re.sub(r"[*`]", "", cell).strip()
        if cell_txt and cell_txt not in pdf_text:
            sys.exit(f"error: md table header '{cell_txt}' not found in PDF text")
    # TOC entries must not be clipped off the TOC page
    for (_, title, _), pg in zip(entries, listed):
        if title not in pdf_text:
            sys.exit(f"error: TOC entry '{title}' not found in PDF text")
    for bid in sorted(transcript_build_ids):
        if bid not in pdf_text:
            sys.exit(f"error: transcript build id '{bid}' not found in PDF text")
    # every release.kit id mentioned in the manual must appear in the text
    kit_ids = re.findall(r'^\s*\{"([a-z0-9_]+)",',
                         read(REPO / "src/util/drop_kits.cpp"), re.M)
    for kid in kit_ids:
        if re.search(rf"`{kid}`|\b{re.escape(kid)}\b", md_text) \
                and kid not in pdf_text:
            sys.exit(f"error: kit id '{kid}' mentioned in md but missing in PDF")
    tag = "DRAFT: " if draft or pending else ""
    print(f"{tag}DONE: {out_pdf} — {doc.page_count} pages, "
          f"{out_pdf.stat().st_size} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
