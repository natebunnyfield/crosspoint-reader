#!/usr/bin/env python3
"""Subset every face the page needs to woff2, and emit a CSS file of @font-face
rules with the payload inlined as data URIs (the Artifact CSP blocks font CDNs).
"""
import base64
import os
import urllib.request

from fontTools import subset, ttLib

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "sources")
os.makedirs(SRC, exist_ok=True)

# What the specimen lines need: the page shows one line of live text per face.
SPECIMEN_TEXT = ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
                 "0123456789 .,;:!?'\"()[]-–—/&%·äöüéèêàçñÄÖÜÉ"
                 "‘’“”")

# The page's own chrome: headings, body copy, table labels.
CHROME_TEXT = SPECIMEN_TEXT + "→←↑↓×✓−+@#*|~^_<>{}=$£€"

CANDIDATES = {
    "AtkinsonNext": "AtkinsonNext-regular-raw.ttf",
    "Luciole": "Luciole-Regular.ttf",
    "LexicaUltralegible": "LexicaUltralegible-regular-raw.ttf",
    "Andika": "Andika-regular-raw.ttf",
    "SourceSans3": "SourceSans3-regular-raw.ttf",
    "FiraSans": "FiraSans-regular-raw.ttf",
    "IBMPlexSans": "IBMPlexSans-regular-raw.ttf",
    "AlegreyaSans": "AlegreyaSans-regular-raw.ttf",
    "OpenSans": "OpenSans-regular.ttf",
    "PublicSans": "PublicSans-regular.ttf",
    "PTSans": "PTSans-regular-raw.ttf",
    "Inter": "Inter-regular.ttf",
    "NotoSans": "NotoSans-regular-raw.ttf",
}

CHROME = {
    # A serif for the page's own voice: it cannot be mistaken for one of the
    # sans specimens, which is the point — document type vs. exhibit type.
    "ChromeText": ("https://raw.githubusercontent.com/adobe-fonts/source-serif/release/TTF/SourceSerif4-Regular.ttf", 400),
    "ChromeTextBold": ("https://raw.githubusercontent.com/adobe-fonts/source-serif/release/TTF/SourceSerif4-Semibold.ttf", 600),
    "ChromeMono": ("https://raw.githubusercontent.com/adobe-fonts/source-code-pro/release/TTF/SourceCodePro-Regular.ttf", 400),
}


def fetch(url, dest):
    if os.path.exists(dest) and os.path.getsize(dest) > 0:
        return dest
    req = urllib.request.Request(url, headers={"User-Agent": "crosspoint-page"})
    with urllib.request.urlopen(req, timeout=60) as r:
        data = r.read()
    open(dest, "wb").write(data)
    return dest


def subset_to_woff2(path, text, out_path):
    font = ttLib.TTFont(path, fontNumber=0)
    opts = subset.Options()
    opts.layout_features = ["kern", "liga", "clig", "calt"]
    opts.desubroutinize = True
    opts.notdef_outline = False
    opts.recalc_bounds = True
    opts.drop_tables += ["GSUB", "GPOS"] if False else []
    s = subset.Subsetter(options=opts)
    s.populate(text=text)
    s.subset(font)
    font.flavor = "woff2"
    font.save(out_path)
    return out_path


def data_uri(path):
    return "data:font/woff2;base64," + base64.b64encode(open(path, "rb").read()).decode()


def main():
    css = []
    total = 0
    for name, (url, weight) in CHROME.items():
        raw = fetch(url, os.path.join(SRC, f"{name}.ttf"))
        out = os.path.join(SRC, f"{name}.woff2")
        subset_to_woff2(raw, CHROME_TEXT, out)
        total += os.path.getsize(out)
        family = "ChromeMono" if name == "ChromeMono" else "ChromeText"
        css.append(f"@font-face{{font-family:'{family}';font-style:normal;"
                   f"font-weight:{weight};font-display:swap;src:url({data_uri(out)}) format('woff2')}}")

    for name, filename in CANDIDATES.items():
        raw = os.path.join(SRC, filename)
        if not os.path.exists(raw):
            print("MISSING", raw)
            continue
        out = os.path.join(SRC, f"spec-{name}.woff2")
        subset_to_woff2(raw, SPECIMEN_TEXT, out)
        total += os.path.getsize(out)
        css.append(f"@font-face{{font-family:'S{name}';font-style:normal;font-weight:400;"
                   f"font-display:swap;src:url({data_uri(out)}) format('woff2')}}")
        print(f"{name:20s} {os.path.getsize(out)/1024:6.1f} KB")

    path = os.path.join(HERE, "fonts.css")
    open(path, "w").write("\n".join(css))
    print(f"\ntotal woff2 {total/1024:.0f} KB -> css {os.path.getsize(path)/1024:.0f} KB")


if __name__ == "__main__":
    main()
