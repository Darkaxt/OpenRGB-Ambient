"""Vendor the inspected MIT EDAmbilight standalone preview, without its server.

Inputs are the locally retained v1.2.0 shipped frontend; no runtime downloads.
"""
import argparse
import hashlib
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1] / 'third_party' / 'ed-preview'
    if root.exists():
        raise FileExistsError('Preview already imported; update through reviewed source changes')
    names = ['web/preview.html', 'web/js/common.js', 'web/css/fonts.css', 'LICENSE']
    names += [str(p.relative_to(args.source)).replace('\\', '/') for p in sorted((args.source/'web/fonts').glob('*.woff2'))]
    manifest = {}
    for name in names:
        data = (args.source/name).read_bytes()
        destination = root / (name[4:] if name.startswith('web/') else name)
        destination.parent.mkdir(parents=True, exist_ok=True)
        manifest[name] = hashlib.sha256(data).hexdigest()
        if name == 'web/preview.html':
            document = data.decode('utf-8')
            assert document.count('<script src="/eel.js"></script>') == 1
            document = document.replace('<script src="/eel.js"></script>', '<!-- Standalone preview: no Eel server or lighting writer. -->')
            document = document.replace("'edambilight.preview.", "'office-ambilight.preview.")
            data = document.encode('utf-8')
        destination.write_bytes(data)
    (root/'provenance.json').write_text(json.dumps({'upstream':'https://github.com/leandor70/EDAmbilight','release':'v1.2.0','input_sha256':manifest,
        'changes':['Remove unused Eel script','Namespace preview localStorage; original procedural/audio/preset behavior retained']}, indent=2))
    print(json.dumps({'imported':len(names),'destination':str(root)}))


if __name__ == '__main__':
    main()
