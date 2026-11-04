"""Convert the recovered reference fonts for native Qt; no runtime dependency.

Reproduction tool: fonttools==4.60.1 and brotli==1.2.1. Converted OFL families
are renamed because changing container format constitutes a modified version.
Material Symbols are instantiated/subsetted to the actual used glyphs and mapped
to private codepoints, avoiding platform-dependent ligature shaping.
"""
import argparse
import hashlib
import json
import sys
import urllib.request
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--library-root", type=Path, required=True)
    parser.add_argument("--inspect-symbols", action="store_true")
    args = parser.parse_args()
    sys.path.insert(0, str(args.library_root))
    from fontTools.ttLib import TTFont
    from fontTools.ttLib.tables._c_m_a_p import CmapSubtable
    from fontTools.varLib.instancer import instantiateVariableFont
    from fontTools import subset

    root = Path(__file__).resolve().parents[1]
    source = root / "third_party/ed-preview/fonts"
    dest = root / "assets/office"
    dest.mkdir(parents=True, exist_ok=True)
    manifest = json.loads((root / "design-reference/iconography/manifest.json").read_text())
    expected = {item["file"]: item["sha256"] for item in manifest["fonts"]}
    outputs = []

    def read(name):
        path = source / name
        if hashlib.sha256(path.read_bytes()).hexdigest() != expected[name]:
            raise ValueError(f"Reference font identity changed: {name}")
        return TTFont(path, recalcTimestamp=False)

    def save(font, filename, family, style):
        font.flavor = None
        for name_id, value in ((1, family), (2, style), (4, family + " " + style),
                               (6, (family + "-" + style).replace(" ", "")),
                               (16, family), (17, style)):
            font["name"].setName(value, name_id, 3, 1, 0x409)
            font["name"].setName(value, name_id, 1, 0, 0)
        path = dest / filename
        font.save(path)
        outputs.append(dict(file=filename, sha256=hashlib.sha256(path.read_bytes()).hexdigest()))

    for family, weights, name in (("Orbitron", (400, 700), "Office Display"),
                                   ("Rajdhani", (400, 500, 600, 700), "Office Body")):
        for weight in weights:
            font = read(f"{family}-{weight}.woff2")
            style = {400: "Regular", 500: "Medium", 600: "SemiBold", 700: "Bold"}[weight]
            save(font, f"{family}-{weight}.ttf", name, style)
    font = read("MaterialSymbolsOutlined.woff2")
    chars = font.getBestCmap()
    if args.inspect_symbols:
        print(json.dumps(dict(adjust_chars=[chars[ord(c)] for c in "adjust"],
            glyph_matches=[g for g in font.getGlyphOrder() if "adjust" in g],
            lookups=[dict(type=l.LookupType, tables=[dict(format=getattr(t,"Format",None),
                extension=getattr(t,"ExtensionLookupType",None),
                first_ligatures=list(getattr(t,"ligatures",{}).items())[:1]) for t in l.SubTable])
                for l in font["GSUB"].table.LookupList.Lookup]), default=str))
        return
    ligatures = {}
    for lookup in font["GSUB"].table.LookupList.Lookup:
        if lookup.LookupType in (4, 7):
            for wrapped in lookup.SubTable:
                table = wrapped.ExtSubTable if lookup.LookupType == 7 else wrapped
                if not hasattr(table, "ligatures"):
                    continue
                for first, entries in table.ligatures.items():
                    for entry in entries:
                        ligatures[tuple([first] + entry.Component)] = entry.LigGlyph
    glyphs = {}
    for index, name in enumerate(manifest["glyphs"]):
        key = tuple(chars[ord(c)] for c in name)
        if key not in ligatures:
            raise ValueError(f"Original font does not resolve {name}")
        glyphs[name] = (0xE000 + index, ligatures[key])
    for active, weight in ((False, 200), (True, 300)):
        instance = instantiateVariableFont(font, {"FILL": int(active), "wght": weight, "GRAD": 0, "opsz": 24}, inplace=False)
        # Instancing resolves FeatureVariations to the active feature lookups.
        # Apply selected single substitutions before dropping shaping tables so
        # filled icons do not accidentally retain the outline glyph.
        substitutions = {}
        gsub = instance["GSUB"].table
        selected = {i for feature in gsub.FeatureList.FeatureRecord for i in feature.Feature.LookupListIndex}
        for i in sorted(selected):
            lookup = gsub.LookupList.Lookup[i]
            if lookup.LookupType == 1:
                for table in lookup.SubTable:
                    substitutions.update(table.mapping)
        resolved = {code: substitutions.get(glyph, glyph) for code, glyph in glyphs.values()}
        for table in instance["cmap"].tables:
            if table.isUnicode():
                table.cmap.update(resolved)
        options = subset.Options()
        options.layout_features = []
        options.name_IDs = [0, 1, 2, 3, 4, 5, 6, 13, 14, 16, 17]
        sub = subset.Subsetter(options)
        sub.populate(unicodes=[code for code, glyph in glyphs.values()])
        sub.subset(instance)
        save(instance, "Symbols-Active.ttf" if active else "Symbols-Outline.ttf",
             "Office Symbols Active" if active else "Office Symbols Outline", "Regular")
    header = "#pragma once\n#include <QHash>\n#include <QString>\nnamespace officeui {\ninline const QHash<QString, ushort> symbolCodes = {\n"
    header += "".join(f'    {{"{name}", 0x{code:X}}},\n' for name, (code, glyph) in glyphs.items())
    header += "};\n}\n"
    (root / "OfficeSymbols.h").write_text(header, encoding="utf-8", newline="\n")
    for name, url in (
        ("LICENSE-Material-Symbols.txt", "https://raw.githubusercontent.com/google/material-design-icons/master/LICENSE"),
        ("LICENSE-Orbitron.txt", "https://raw.githubusercontent.com/google/fonts/main/ofl/orbitron/OFL.txt"),
        ("LICENSE-Rajdhani.txt", "https://raw.githubusercontent.com/google/fonts/main/ofl/rajdhani/OFL.txt")):
        with urllib.request.urlopen(url) as response:
            (dest / name).write_bytes(response.read())
    (dest / "provenance.json").write_text(json.dumps(dict(
        source_release=manifest["release"], source_archive_sha256=manifest["archive_sha256"],
        generator="scripts/Prepare-OfficeFonts.py", fonttools="4.60.1", brotli="1.2.1",
        modification="WOFF2 decompression, renamed converted OFL families; Material Symbols static axes and used-glyph subset",
        glyphs={name: code for name, (code, glyph) in glyphs.items()}, outputs=outputs), indent=2) + "\n", encoding="utf-8", newline="\n")
    qrc = '<RCC><qresource prefix="/office">\n'
    for path in sorted(dest.iterdir()):
        if path.suffix in (".ttf", ".txt"):
            qrc += f'<file alias="{path.name}">assets/office/{path.name}</file>\n'
    qrc += '</qresource></RCC>\n'
    (root / "OfficeAssets.qrc").write_text(qrc, encoding="utf-8", newline="\n")
    print(json.dumps(dict(fonts=len(outputs), glyphs=len(glyphs), resources=str(dest))))


if __name__ == "__main__":
    main()
