# Builds the mock GitHub release + a throttled payload server's files.
import hashlib, json, os, shutil, sys
S = os.environ['UPD_WORK']  # scratch dir; never the repo
SRC = os.path.expanduser('~/src/crosspoint-reader/fs_/fonts')
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8766
mock, payload = os.path.join(S, 'mock'), os.path.join(S, 'payload')
os.makedirs(mock, exist_ok=True); os.makedirs(payload, exist_ok=True)
fam = []
assets = [{"name": "manifest.json", "url": f"http://127.0.0.1:{PORT}/manifest-fonts.json"}]
for name in ["Edgar", "Doves", "Coelacanth"]:
    files = []
    for f in sorted(os.listdir(os.path.join(SRC, name))):
        if not f.endswith('.cpfont'): continue
        b = open(os.path.join(SRC, name, f), 'rb').read()
        shutil.copy(os.path.join(SRC, name, f), os.path.join(payload, f))
        files.append({"file": f, "asset": f, "bytes": len(b), "sha256": hashlib.sha256(b).hexdigest()})
        assets.append({"name": f, "url": f"http://127.0.0.1:{PORT}/{f}"})
    fam.append({"family": name, "files": files})
json.dump({"version": 1, "families": fam}, open(os.path.join(payload, 'manifest-fonts.json'), 'w'))
json.dump({"tag_name": "fonts-latest", "assets": assets}, open(os.path.join(mock, 'fonts-latest'), 'w'))
# library: three epubs from the real card
books, lassets = [], [{"name": "manifest.json", "url": f"http://127.0.0.1:{PORT}/manifest-lib.json"}]
card = os.path.expanduser('~/src/crosspoint-reader/fs_')
eps = sorted([f for f in os.listdir(card) if f.endswith('.epub')], key=lambda f: -os.path.getsize(os.path.join(card, f)))[:3]
for i, f in enumerate(eps):
    b = open(os.path.join(card, f), 'rb').read()
    an = f"book{i}.epub"
    shutil.copy(os.path.join(card, f), os.path.join(payload, an))
    books.append({"file": f, "asset": an, "bytes": len(b), "sha256": hashlib.sha256(b).hexdigest()})
    lassets.append({"name": an, "url": f"http://127.0.0.1:{PORT}/{an}"})
# a 6 MB synthetic book, so one download is long enough to see
b = os.urandom(6_000_000); open(os.path.join(payload, 'book9.epub'), 'wb').write(b)
books.append({"file": "atlas-of-remote-islands.epub", "asset": "book9.epub", "bytes": len(b), "sha256": hashlib.sha256(b).hexdigest()})
lassets.append({"name": "book9.epub", "url": f"http://127.0.0.1:{PORT}/book9.epub"})
json.dump({"version": 1, "books": books}, open(os.path.join(payload, 'manifest-lib.json'), 'w'))
json.dump({"tag_name": "library-latest", "assets": lassets}, open(os.path.join(mock, 'library-latest'), 'w'))
print([ (b['file'], b['bytes']) for b in books])
