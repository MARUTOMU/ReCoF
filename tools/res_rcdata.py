"""Write files as RCDATA resources in a .res file for the linker.

  python tools/res_rcdata.py <out.res> <id>=<file> [<id>=<file> ...]

A link section of tools/units.json lists them under "rcdata" ({"id": 101, "file": "src/..."}); tools/build.py link
passes the .res to the linker as it does for "resources". GameUI.dll carries the menu font this way (B398), so no
font file is added to the game folder.
"""
import struct
import sys


def pad4(b):
    return b + b"\0" * (-len(b) % 4)


def res_entry(type_id, name_id, data, lang=0x0409, flags=0x0030):
    # TYPE and NAME as ordinals (0xFFFF, id), then DataVersion, MemoryFlags (MOVEABLE | PURE), LanguageId, Version,
    # Characteristics
    hdr = struct.pack("<HHHH", 0xFFFF, type_id, 0xFFFF, name_id)
    hdr += struct.pack("<IHHII", 0, flags, lang, 0, 0)
    return struct.pack("<II", len(data), 8 + len(hdr)) + hdr + pad4(data)


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    out = bytearray()
    # the empty first entry every .res file starts with
    out += struct.pack("<II", 0, 32) + struct.pack("<HHHH", 0xFFFF, 0, 0xFFFF, 0) + struct.pack("<IHHII", 0, 0, 0, 0, 0)
    for arg in sys.argv[2:]:
        rid, path = arg.split("=", 1)
        out += res_entry(10, int(rid), open(path, "rb").read())  # RT_RCDATA
    open(sys.argv[1], "wb").write(bytes(out))


if __name__ == "__main__":
    main()
