import struct
import json
import os

with open("assets/models/manifest.json", "r") as f:
    data = json.load(f)

for anim, ad in list(data["animations"].items())[:5]:
    path = "assets/" + ad["file"]
    with open(path, "rb") as f:
        f.seek(12)
        name_len = struct.unpack("<I", f.read(4))[0]
        f.seek(12 + 4 + name_len)
        ft, nf, nb = struct.unpack("<fII", f.read(12))
        print(f"{anim}: manifest_bones={ad['bones']}, file_frames={nf}, file_keys={nb}")
