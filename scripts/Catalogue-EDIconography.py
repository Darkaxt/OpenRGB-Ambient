"""Catalogue original release icons without changing the application UI.

Reads the retained, authenticated REA extraction. Reuses the already-vendored
font bytes, preserves source locations and exports original inline SVGs. The
result is a design input, not a replacement application or another RGB sender.
"""
import argparse
import hashlib
import html
import json
import re
from pathlib import Path


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    dest = root / "design-reference" / "iconography"
    dest.mkdir(parents=True, exist_ok=True)
    baseline = json.loads((args.source / "recovery-manifest.json").read_text())
    expected = {item["path"]: item["sha256"] for item in baseline["files"]}
    occurrences, dynamic, svgs, fonts = [], [], [], []

    def add(name, source, line, style="", kind="literal"):
        occurrences.append(dict(glyph=name, source=source, line=line,
                                style=style, resolution=kind))

    for path in sorted((args.source / "web").rglob("*")):
        if path.suffix not in (".html", ".js"):
            continue
        relative = path.relative_to(args.source).as_posix()
        if relative not in expected or digest(path) != expected[relative]:
            raise ValueError(f"Source identity mismatch: {relative}")
        text = path.read_text(encoding="utf-8")
        for match in re.finditer(r'<span\b([^>]*\bmaterial-symbols-outlined\b[^>]*)>(.*?)</span>', text, re.S):
            content = html.unescape(match[2]).strip()
            line = text.count("\n", 0, match.start()) + 1
            style = re.search(r'style="([^"]*)"', match[1])
            style = style[1] if style else ""
            if re.fullmatch(r"[a-z][a-z0-9_]*", content):
                add(content, relative, line, style)
            else:
                dynamic.append(dict(source=relative, line=line, expression=content))
        if path.name == "navigation.js":
            for match in re.finditer(r"icon:\s*'([^']+)'", text):
                add(match[1], relative, text.count("\n", 0, match.start()) + 1,
                    "26px; inactive FILL=0,wght=200; active FILL=1,wght=300; GRAD=0,opsz=24", "NAV_ITEMS")
        # Explicitly resolve the two dynamic icon selectors from their definitions.
        if path.name in ("ambilight.js", "devices.html"):
            match = re.search(r"const iconName\s*=.*?;", text, re.S)
            for name in re.findall(r"'([^']+)'", match[0]):
                if name not in ("matrix", "linear"):
                    add(name, relative, text.count("\n", 0, match.start()) + 1,
                        "20px" if path.name == "devices.html" else "19px",
                        "iconName topology-type selector")
        if path.name == "common.js":
            match = re.search(r"const icons\s*=\s*\{(.*?)\}", text, re.S)
            for name in re.findall(r":\s*'([^']+)'", match[1]):
                add(name, relative, text.count("\n", 0, match.start()) + 1,
                    "24px", "notification type selector")
        for index, match in enumerate(re.finditer(r"<svg\b.*?</svg>", text, re.S), 1):
            original = match[0]
            svg = original if "xmlns=" in original else original.replace(
                "<svg", '<svg xmlns="http://www.w3.org/2000/svg"', 1)
            target = dest / f"{path.stem}-{index}.svg"
            target.write_text(svg, encoding="utf-8", newline="\n")
            svgs.append(dict(file=target.name, source=relative,
                             line=text.count("\n", 0, match.start()) + 1,
                             original_fragment_sha256=hashlib.sha256(original.encode()).hexdigest(),
                             exported_sha256=digest(target),
                             transformation="Added XML namespace only where absent"))
    for source in sorted((args.source / "web" / "fonts").glob("*.woff2")):
        relative = source.relative_to(args.source).as_posix()
        target = root / "third_party" / "ed-preview" / "fonts" / source.name
        sha = digest(source)
        if sha != expected[relative] or digest(target) != sha:
            raise ValueError(f"Font identity mismatch: {source.name}")
        fonts.append(dict(file=source.name, bytes=source.stat().st_size,
                          sha256=sha, vendored_path=target.relative_to(root).as_posix()))
    names = sorted({entry["glyph"] for entry in occurrences})
    report = dict(release=baseline["release"], archive_sha256=baseline["archive_sha256"],
                  input_directory=str(args.source), fonts=fonts,
                  glyphs=names, occurrences=occurrences, dynamic_sites=dynamic,
                  dynamic_resolution="NAV_ITEMS, iconName and notification selectors resolved above; template sites retained verbatim",
                  inline_svg=svgs,
                  license_note="Release code MIT, Stefano Indraccolo. Do not assume the release MIT license replaces embedded font licenses; font license review remains required before new redistribution.")
    (dest / "manifest.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8", newline="\n")
    cards = "".join(f'<article><div class="samples"><span class="glyph">{name}</span>'
                    f'<span class="glyph active">{name}</span></div><code>{name}</code></article>' for name in names)
    svg_cards = "".join(f'<article><div class="original-svg">{(dest / entry["file"]).read_text(encoding="utf-8")}</div>'
                        f'<code>{entry["file"]}</code><small>{entry["source"]}:{entry["line"]}</small></article>' for entry in svgs)
    content = '''<!doctype html><html lang="en"><meta charset="utf-8">
<title>Recovered EDAmbilight iconography</title>
<style>
@font-face{font-family:Symbols;src:url('../../third_party/ed-preview/fonts/MaterialSymbolsOutlined.woff2') format('woff2');font-weight:100 700}
@font-face{font-family:Orbitron;src:url('../../third_party/ed-preview/fonts/Orbitron-700.woff2')}
@font-face{font-family:Rajdhani;src:url('../../third_party/ed-preview/fonts/Rajdhani-500.woff2')}
*{box-sizing:border-box}body{margin:0;padding:32px;background:#040c14;color:#cdd6e0;font:18px Rajdhani,sans-serif}
h1{font:22px Orbitron;color:#14f5ff;margin:0 0 16px}h2{font:16px Orbitron;margin-top:28px}p{max-width:1100px}
.grid{display:grid;grid-template-columns:repeat(6,minmax(0,1fr));gap:10px}article{border:1px solid #24444b;border-radius:12px;background:linear-gradient(145deg,#091722,#020a10);padding:14px;text-align:center;min-height:112px}
.samples{display:flex;justify-content:center;gap:22px;margin-bottom:12px}.glyph{font-family:Symbols;font-size:32px;line-height:1;font-variation-settings:'FILL' 0,'wght' 200,'GRAD' 0,'opsz' 24;font-feature-settings:'liga';color:#9ca6af}.active{font-variation-settings:'FILL' 1,'wght' 300,'GRAD' 0,'opsz' 24;color:#14f5ff}
code{font:12px monospace;overflow-wrap:anywhere}small{display:block;font-size:12px;margin-top:8px}.original-svg{height:52px;display:flex;align-items:center;justify-content:center;color:#14f5ff}.original-svg svg{width:44px;height:44px}a{color:#14f5ff}
</style><h1>Recovered EDAmbilight iconography</h1>
<p>Original v1.2.0 font bytes, verified against the retained release manifest. Left: outline/inactive (weight 200). Right: filled/active (weight 300). These are the actual ligature names used by the shipped frontend, not substitute drawings.</p>
<div class="grid">''' + cards + '''</div><h2>Original inline SVG artwork</h2><div class="grid">''' + svg_cards + '''</div>
<p>Navigation: settings_brightness · hub · memory · tune · auto_awesome · help. Original logo surrounds settings_brightness with CSS rings; the loading emblem is inline SVG. CSS glows, chevrons, rings and switches are styling, not missing image files.</p>
<p><a href="manifest.json">Source locations, exact styles and SHA-256 manifest</a>. This catalogue does not modify the native application or lighting configuration.</p></html>'''
    (dest / "contact-sheet.html").write_text(content, encoding="utf-8", newline="\n")
    print(json.dumps(dict(glyphs=len(names), occurrences=len(occurrences), fonts=len(fonts),
                          inline_svg=len(svgs), output=str(dest))))


if __name__ == "__main__":
    main()
