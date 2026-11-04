# Original EDAmbilight iconography

Recovered design assets for the corresponding-page audit, 2026-11-04.

- [Rendered contact sheet](contact-sheet.png)
- [Interactive contact sheet](contact-sheet.html)
- [Source locations, exact styles and verified font hashes](manifest.json)
- [Original power symbol](ambilight-1.svg)
- [Original loading emblem](loading-1.svg)

`scripts/Catalogue-EDIconography.py` verifies the retained release extraction
against its recovery manifest, verifies the already-vendored font copies, records
literal glyph spans and resolves the navigation/device-type/notification selectors.
There are 34 distinct glyphs, 64 literal/resolved references, 12 font files and
two original inline SVGs. No duplicate font payload was added.

The contact sheet was actually rendered in the managed Helium workflow at
1440px width. Material Symbols, Orbitron and Rajdhani all reached `loaded` status;
the symbol width was 32px and the page did not overflow horizontally. The output
was visually inspected: all glyphs and both SVGs are visible. The disposable tab
was closed and the managed workflow released with native exit code 0.

This verifies the asset catalogue, not native Qt font rendering or product visual
acceptance. The native panel still does not use these original assets. Release
code attribution is recorded in the parent review; individual font notices must
be confirmed before any additional redistribution, rather than assuming they
inherit the release code's MIT license.
