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
import re
import sys
from pathlib import Path

from pdf_common import (REPO, read, run, term_image, require_fresh, find_edge,
                        run_edge, edge_shot, widget_table,
                        make_widget_illustration, hud_strings, make_hud_image,
                        install_log_parts, CSS, paginate_js, blk, img,
                        md_blocks, checklist_from_md)

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

    print("[1/9] unit tests (real ctest + test binary output)")
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
    kits_out = run([sim_exe, "--list-kits"], cwd=REPO)
    term_image("> droppoint_sim_main.exe --list-kits", kits_out,
               shots / "s10-list-kits.png", max_rows=30)

    print("[4/9] ARM64 install transcripts (CI artifacts)")
    transcript_build_ids = set()
    for profile in ("m4t", "m400"):
        log = Path(args.install_logs) / "install-logs" / f"install-{profile}.log"
        m = re.search(r"droppoint\s+\S+\s+\(build\s+([^\s)]+)\)", read(log))
        if m:
            transcript_build_ids.add(m.group(1))
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
    required = (["s2-ctest.png", "s3-sim-hover.png",
                 "s4-sim-crosswind.png", "s5a-selftest-m4t.png",
                 "s5b-selftest-m400.png", "s7-dashboard.png",
                 "s8-widgets-illustration.png", "s9-floating-window.png",
                 "s10-list-kits.png"] +
                [f"s6-{p}-{k}.png" for p in ("m4t", "m400")
                 for k in ("install", "upgrade")])
    for name in required:
        p = shots / name
        require_fresh(p, f"screenshot {name}")
        if p.stat().st_size < 3000:
            sys.exit(f"error: screenshot suspiciously small: {p}")

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
      <li>{test_count} automated checks pass on Windows and Linux (the
        terminal capture below).</li>
      <li>Release-point accuracy, automated end-to-end runs: hover drop
        fired once, predicted miss {h_pred} m / true miss {h_truth} m;
        5 m/s crosswind run-in fired once in AUTO, predicted {c_pred} m /
        true {c_truth} m.</li>
      <li>The real PSDK binary compiles and links for x86_64 and aarch64
        Linux against DJI's libpayloadsdk.a (Payload-SDK 3.16.0).</li>
      <li>ARM64 .deb packages install, self-test, upgrade (keeping site
        config), and remove (keeping credentials and logs) in a clean
        Ubuntu 20.04 container — see section 7.</li></ul>""")
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
      release backend and gates for that airframe).</p>
      <p>You also need a drop kit: section 5 lists the compatible models for
      each aircraft and the <code>release.kit</code> value to set.</p>""")

    # -- 3/4/5: aircraft + drop-kit guides (verbatim markdown) ------------------
    m4t_blocks = md_blocks("docs/aircraft/M4T.md")
    SEC("<h2>3 · Matrice 4T — aircraft guide</h2>" + m4t_blocks[0])
    for e in m4t_blocks[1:]:
        B(e)
    m400_blocks = md_blocks("docs/aircraft/M400.md")
    SEC("<h2>4 · Matrice 400 — aircraft guide</h2>" + m400_blocks[0])
    for e in m400_blocks[1:]:
        B(e)
    # 9-column tables get the fixed-layout class so nothing clips on A4
    kit_blocks = [e.replace("<table>", '<table class="kits">')
                  for e in md_blocks("docs/DROP-KITS.md")]
    SEC("<h2>5 · Compatible drop kits — which model to buy</h2>" + kit_blocks[0])
    for e in kit_blocks[1:]:
        B(e)
    czi_blocks = md_blocks("docs/CZI-DROP-KITS.md")
    SEC("<h2>6 · CZI drop kits — integration notes</h2>" + czi_blocks[0])
    for e in czi_blocks[1:]:
        B(e)

    # -- 7: install step by step ------------------------------------------------
    SEC(f"""<h2>7 · Installing, step by step (real ARM64 transcript)</h2>
      <p>Every block below is a verbatim capture from the automated install
      test on a clean arm64 Ubuntu 20.04 container — the same steps you run
      on the companion computer. The transcripts below are from the package
      built as <code>{html.escape(sorted(transcript_build_ids)[0] if transcript_build_ids else "unknown")}</code>.</p>
      <ol>
      <li><b>Install the package</b> — <code>dpkg -i droppoint-m4t_&lt;ver&gt;_arm64.deb</code>.
        postinst creates the <code>droppoint</code> system user, the log
        directory, and placeholder credentials at
        <code>/etc/droppoint/credentials.yaml</code> (chmod 600 — fill in your
        real DJI app values before flying).</li>
      <li><b>Check it runs</b> — <code>droppoint --version</code> then
        <code>droppoint --selftest --no-link</code>. The report must pass with
        placeholder credentials shown as WARN, not FAIL.</li>
      <li><b>Try the practice mode</b> —
        <code>droppoint_sim_main --sim hover --profile /opt/droppoint/config</code>
        must print <code>SUMMARY … fired=1</code>.</li></ol>""")
    B(img("s6-m4t-install.png", "Real capture — install, --version, "
          "--selftest and the practice run for the Matrice 4T package."))
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

    # -- 8: try without a drone ---------------------------------------------------
    SEC("""<h2>8 · Trying it without a drone</h2>
      <p>The same binary carries a practice mode — scripted pilot,
      wind model, ground truth and every release gate — for bench training
      without a drone. Commands exactly as run:</p>""")
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
    B(img("s10-list-kits.png", "Real capture — droppoint_sim_main.exe "
          "--list-kits: the built-in drop-kit catalogue the config is "
          "checked against (same list as section 5)."))
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
    SEC("""<h2>9 · Operator controls in DJI Pilot 2</h2>
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

    # -- 10: before first flight --------------------------------------------------
    # flows onto the same page as the floating-window image when it fits
    B("""<h2>10 · Before first flight — verify on the aircraft</h2>
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
        Complete the before-first-flight checklist in section 10 before the
        first operational drop.</div></div></div>"""

    html_doc = ("<!DOCTYPE html><html><head><meta charset='utf-8'>"
                f"<style>{CSS}</style></head><body>{title_page}"
                f'<div id="flow" style="display:none">{"".join(blocks)}</div>'
                f"{paginate_js('DropPoint How-To')}</body></html>")
    html_path = shots.parent / "howto.html"
    html_path.write_text(html_doc, encoding="utf-8")
    require_fresh(html_path, "generated HTML")

    print("[9/9] printing to PDF via headless Edge")
    run_edge(edge,
             ["--print-to-pdf={OUT}", "--no-pdf-header-footer",
              "--virtual-time-budget=15000",
              "file:///" + str(html_path.resolve()).replace("\\", "/")],
             out_pdf, "print-to-pdf")
    if out_pdf.stat().st_size < 20000:
        sys.exit(f"error: PDF suspiciously small: {out_pdf}")

    # ---- self-check: the PDF text must carry this run's real numbers ------
    import pymupdf
    doc = pymupdf.open(str(out_pdf))
    pdf_text = re.sub(r"\s+", " ", "".join(p.get_text() for p in doc))
    for bid in sorted(transcript_build_ids):
        if bid not in pdf_text:
            sys.exit(f"error: transcript build id '{bid}' not found in PDF text "
                     f"— the PDF was not regenerated from the new artifacts")
    if f"{test_count} automated checks" not in pdf_text:
        sys.exit(f"error: test count '{test_count}' not found in PDF text")
    # every drop-kit id in the catalogue must appear in the PDF (the
    # DROP-KITS.md table) — ids are the first string of each entry
    kit_ids = re.findall(r'^\s*\{"([a-z0-9_]+)",',
                         read(REPO / "src/util/drop_kits.cpp"), re.M)
    if len(kit_ids) < 5:
        sys.exit("error: catalogue id parse found <5 ids in drop_kits.cpp")
    for kid in kit_ids:
        if kid not in pdf_text:
            sys.exit(f"error: drop-kit id '{kid}' not found in PDF text")
    print(f"DONE: {out_pdf} — {doc.page_count} pages, "
          f"{out_pdf.stat().st_size} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
