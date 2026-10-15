# Usage: python3 -I tools/subset_cjk_fonts.py NotoSansCJK-Regular.ttc assets/fonts/cjk Regular
# (the .ttc from Debian/Ubuntu fonts-noto-cjk or github.com/notofonts/noto-cjk; run
# from the repo root, needs fontTools).
# Subsets Noto Sans CJK (Source Han Sans' glyphs) per language: the characters of
# that language's .po file plus its common national character set.
import sys
from fontTools.ttLib import TTCollection
from fontTools import subset
src, out_dir, weight = sys.argv[1], sys.argv[2], sys.argv[3]
FACES = {"ja": 0, "ko": 1, "zh": 2, "zh_TW": 3}
CODECS = {"zh": "gb2312", "zh_TW": "big5", "ja": "euc_jp", "ko": "euc_kr"}
# Big5: symbols and level 1; EUC-KR: symbols and hangul, no hanja.
def charset(codec):
    out = set()
    for hi in (range(0xA1, 0xA4) if codec == "big5" else range(0xA1, 0xCA) if codec == "euc_kr" else range(0xA1, 0xFF)):
        for lo in range(0xA1, 0xFF):
            try:
                out.add(bytes([hi, lo]).decode(codec))
            except Exception:
                pass
    if codec == "big5":
        for hi in range(0xA4, 0xC7):  # Big5 level 1 (common characters)
            for lo in list(range(0x40, 0x7F)) + list(range(0xA1, 0xFF)):
                try:
                    out.add(bytes([hi, lo]).decode(codec))
                except Exception:
                    pass
    return out
for lang, face in FACES.items():
    chars = set(chr(c) for c in range(0x20, 0x7F))
    chars |= set(open(f"assets/l10n/{lang}.po", encoding="utf-8").read())
    chars |= charset(CODECS[lang])
    # The Language page names every language in its own script (user_settings.gd).
    chars |= set("日本語한국어中文(简体)(繁體)")
    font = TTCollection(src).fonts[face]
    opts = subset.Options()
    opts.layout_features = ["*"]
    opts.name_IDs = ["*"]
    opts.notdef_outline = True
    opts.hinting = False
    s = subset.Subsetter(opts)
    s.populate(unicodes=[ord(c) for c in chars if len(c) == 1])
    s.subset(font)
    path = f"{out_dir}/NotoSansCJK-{lang}-{weight}.otf"
    font.save(path)
    print(path, len(chars))
