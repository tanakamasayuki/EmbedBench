#!/usr/bin/env python3
"""Generate the SD card preset images shipped in devices/src/sd_images.*.

The images are real FAT12 volumes, not sketches of one: this script builds
them, parses them back the way a driver would, and only then emits the C
source. Run it from the repository root:

    python3 devices/tools/make_sd_images.py
"""

import struct
import sys
from pathlib import Path

SECTOR = 512
SECTORS = 8            # matches UnitSdCardModel::kBlockCount
RESERVED = 1
NUM_FATS = 1
FAT_SECTORS = 1
ROOT_ENTRIES = 16
ROOT_SECTORS = ROOT_ENTRIES * 32 // SECTOR
DATA_START = RESERVED + NUM_FATS * FAT_SECTORS + ROOT_SECTORS


def boot_sector(label: bytes) -> bytearray:
    s = bytearray(SECTOR)
    s[0:3] = b"\xEB\x3C\x90"
    s[3:11] = b"EMBEDBEN"
    struct.pack_into("<H", s, 11, SECTOR)        # BytsPerSec
    s[13] = 1                                     # SecPerClus
    struct.pack_into("<H", s, 14, RESERVED)      # RsvdSecCnt
    s[16] = NUM_FATS
    struct.pack_into("<H", s, 17, ROOT_ENTRIES)
    struct.pack_into("<H", s, 19, SECTORS)       # TotSec16
    s[21] = 0xF8                                  # Media
    struct.pack_into("<H", s, 22, FAT_SECTORS)   # FATSz16
    struct.pack_into("<H", s, 24, 1)             # SecPerTrk
    struct.pack_into("<H", s, 26, 1)             # NumHeads
    s[36] = 0x80                                  # DrvNum
    s[38] = 0x29                                  # BootSig
    struct.pack_into("<I", s, 39, 0x454D4244)    # VolID
    s[43:54] = label.ljust(11, b" ")
    s[54:62] = b"FAT12   "
    s[510:512] = b"\x55\xAA"
    return s


def fat_sector(chains) -> bytearray:
    """chains: {cluster: next} with 0xFFF meaning end of chain."""
    entries = {0: 0xFF8, 1: 0xFFF}
    entries.update(chains)
    s = bytearray(SECTOR)
    for cluster, value in entries.items():
        off = cluster * 3 // 2
        if cluster % 2 == 0:
            s[off] = value & 0xFF
            s[off + 1] = (s[off + 1] & 0xF0) | ((value >> 8) & 0x0F)
        else:
            s[off] = (s[off] & 0x0F) | ((value << 4) & 0xF0)
            s[off + 1] = (value >> 4) & 0xFF
    return s


def dir_entry(name: bytes, ext: bytes, cluster: int, size: int) -> bytes:
    e = bytearray(32)
    e[0:8] = name.ljust(8, b" ")
    e[8:11] = ext.ljust(3, b" ")
    e[11] = 0x20                                  # archive
    struct.pack_into("<H", e, 22, 0x6000)        # time
    struct.pack_into("<H", e, 24, 0x5926)        # date: 2024-09-06
    struct.pack_into("<H", e, 26, cluster)
    struct.pack_into("<I", e, 28, size)
    return bytes(e)


def build_with_file(content: bytes, name=b"HELLO", ext=b"TXT") -> bytearray:
    assert len(content) <= SECTOR, "the toy volume holds one cluster per file"
    image = bytearray(SECTOR * SECTORS)
    image[0:SECTOR] = boot_sector(b"EMBEDBENCH")
    off = RESERVED * SECTOR
    image[off:off + SECTOR] = fat_sector({2: 0xFFF})
    off = (RESERVED + FAT_SECTORS) * SECTOR
    image[off:off + 32] = dir_entry(name, ext, 2, len(content))
    off = DATA_START * SECTOR
    image[off:off + len(content)] = content
    return image


def build_broken(kind: str) -> bytearray:
    """A volume that is wrong in one specific, plausible way.

    The interesting corruptions are not the ones a driver rejects on its
    first check — those are easy. They are the ones that pass the boot
    sector and then make a reasonable driver do something bad: loop
    forever, read a block that is not there, or run off the end of a file.
    """
    content = b"the chain leads elsewhere\n"
    image = build_with_file(content)
    fat_off = RESERVED * SECTOR
    dir_off = (RESERVED + FAT_SECTORS) * SECTOR

    if kind == "circular":
        # 2 -> 3 -> 2: following the chain never ends.
        image[fat_off:fat_off + SECTOR] = fat_sector({2: 3, 3: 2})
        struct.pack_into("<I", image, dir_off + 28, SECTOR * 4)
    elif kind == "outofrange":
        # The second cluster is past the end of a volume this size.
        image[fat_off:fat_off + SECTOR] = fat_sector({2: 200})
        struct.pack_into("<I", image, dir_off + 28, SECTOR * 2)
    elif kind == "sizemismatch":
        # The directory claims four clusters; the chain ends after one.
        struct.pack_into("<I", image, dir_off + 28, SECTOR * 4)
    elif kind == "zerobps":
        # Bytes-per-sector of zero. Every offset a driver computes from
        # the BPB divides by this.
        struct.pack_into("<H", image, 11, 0)
    else:
        raise ValueError(kind)
    return image


def build_empty() -> bytearray:
    image = bytearray(SECTOR * SECTORS)
    image[0:SECTOR] = boot_sector(b"EMPTY")
    off = RESERVED * SECTOR
    image[off:off + SECTOR] = fat_sector({})
    return image


# --- read it back the way a driver would, so a broken preset cannot ship ---

def parse(image: bytes):
    assert image[510:512] == b"\x55\xAA", "no boot signature"
    bps = struct.unpack_from("<H", image, 11)[0]
    spc = image[13]
    rsvd = struct.unpack_from("<H", image, 14)[0]
    nfat = image[16]
    root_entries = struct.unpack_from("<H", image, 17)[0]
    fatsz = struct.unpack_from("<H", image, 22)[0]
    assert bps == SECTOR and spc == 1, (bps, spc)
    assert image[54:59] == b"FAT12", image[54:62]
    root = rsvd + nfat * fatsz
    data = root + root_entries * 32 // bps
    files = {}
    for i in range(root_entries):
        off = root * bps + i * 32
        if image[off] in (0x00, 0xE5):
            continue
        name = image[off:off + 8].rstrip(b" ").decode()
        ext = image[off + 8:off + 11].rstrip(b" ").decode()
        cluster = struct.unpack_from("<H", image, off + 26)[0]
        size = struct.unpack_from("<I", image, off + 28)[0]
        start = (data + (cluster - 2) * spc) * bps
        files[f"{name}.{ext}" if ext else name] = image[start:start + size]
    return files


def runs(image: bytes):
    """Only the bytes that are not zero, as (offset, bytes) runs."""
    out = []
    i = 0
    while i < len(image):
        if image[i] == 0:
            i += 1
            continue
        j = i
        # A gap of a few zeros is cheaper to keep than to split around.
        zeros = 0
        while j < len(image) and zeros < 8:
            zeros = zeros + 1 if image[j] == 0 else 0
            j += 1
        while j > i and image[j - 1] == 0:
            j -= 1
        out.append((i, bytes(image[i:j])))
        i = j
    return out


def emit(images):
    hdr = ['''// SD card preset images — real FAT12 volumes, not sketches of one.
//
// Generated by devices/tools/make_sd_images.py, which builds each image,
// parses it back the way a driver would, and only then writes this file.
// Do not edit by hand; edit the generator and re-run it.
//
// The images are stored as the runs of bytes that are not zero, because a
// formatted volume is mostly zeros: 4 KB of image costs a few hundred
// bytes of source here. UnitSdCardModel::loadImage expands them.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace ebsd {

// One stretch of non-zero bytes at a byte offset into the whole volume.
struct ImageRun {
  uint32_t offset;
  uint16_t length;
  const uint8_t* bytes;
};

// A whole card image. Everything not covered by a run is zero.
struct Image {
  const char* name;
  uint32_t blocks;
  const ImageRun* runs;
  size_t runCount;
};
''']
    src = ['''// SD card preset images. Generated by devices/tools/make_sd_images.py.
#include "sd_images.h"

namespace ebsd {
namespace {
''']
    decls = []
    for ident, image, comment in images:
        rs = runs(image)
        total = sum(len(b) for _, b in rs)
        for k, (off, data) in enumerate(rs):
            src.append(f"const uint8_t k{ident}Run{k}[] = {{")
            for line_start in range(0, len(data), 12):
                chunk = data[line_start:line_start + 12]
                src.append("    " + " ".join(f"0x{b:02X}," for b in chunk))
            src.append("};")
        src.append(f"const ImageRun k{ident}Runs[] = {{")
        for k, (off, data) in enumerate(rs):
            src.append(f"    {{{off}, {len(data)}, k{ident}Run{k}}},")
        src.append("};")
        src.append("")
        decls.append((ident, image, comment, len(rs), total))
    src.append("}  // namespace")
    src.append("")
    for ident, image, comment, nruns, total in decls:
        src.append(f"const Image k{ident} = {{")
        src.append(f'    "{ident}", {len(image) // SECTOR}, k{ident}Runs,')
        src.append(f"    sizeof(k{ident}Runs) / sizeof(k{ident}Runs[0])}};")
        src.append("")
    src.append("}  // namespace ebsd")

    for ident, image, comment, nruns, total in decls:
        hdr.append(f"// {comment}")
        hdr.append(f"// {len(image) // SECTOR} blocks, {total} non-zero bytes in {nruns} runs.")
        hdr.append(f"extern const Image k{ident};")
        hdr.append("")
    hdr.append("}  // namespace ebsd")
    return "\n".join(hdr) + "\n", "\n".join(src) + "\n"


def main():
    hello = build_with_file(b"EmbedBench reads this from a real FAT12 volume.\n")
    empty = build_empty()
    corrupt = build_with_file(b"unreachable")
    corrupt[510:512] = b"\x00\x00"          # the boot signature is gone

    circular = build_broken("circular")
    outofrange = build_broken("outofrange")
    sizemismatch = build_broken("sizemismatch")
    zerobps = build_broken("zerobps")

    files = parse(bytes(hello))
    assert list(files) == ["HELLO.TXT"], files
    assert files["HELLO.TXT"].startswith(b"EmbedBench reads this"), files
    assert parse(bytes(empty)) == {}, "the empty volume should hold no files"
    try:
        parse(bytes(corrupt))
    except AssertionError:
        pass
    else:
        raise SystemExit("the corrupt image parsed, which defeats its purpose")

    # Each broken image has to be broken in the way it claims to be, or
    # the test built on it proves nothing.
    def fat_entry(image, cluster):
        off = RESERVED * SECTOR + cluster * 3 // 2
        if cluster % 2 == 0:
            return image[off] | ((image[off + 1] & 0x0F) << 8)
        return (image[off] >> 4) | (image[off + 1] << 4)

    def dir_size(image):
        return struct.unpack_from(
            "<I", image, (RESERVED + FAT_SECTORS) * SECTOR + 28)[0]

    assert fat_entry(circular, 2) == 3 and fat_entry(circular, 3) == 2, \
        "the circular image does not actually loop"
    assert fat_entry(outofrange, 2) == 200, "the chain is not out of range"
    assert fat_entry(sizemismatch, 2) == 0xFFF, "the chain should end at once"
    assert dir_size(sizemismatch) == SECTOR * 4, "the size is not overstated"
    assert struct.unpack_from("<H", zerobps, 11)[0] == 0, "BytsPerSec is set"
    assert bytes(zerobps[510:512]) == b"\x55\xAA", \
        "the zero-BPS image must still pass the signature check, or it is\n" \
        "just another bad-boot image"

    images = [
        ("Fat12Hello", hello,
         "A FAT12 volume holding one file, HELLO.TXT."),
        ("Fat12Empty", empty,
         "A formatted FAT12 volume with no files on it."),
        ("Fat12BadBoot", corrupt,
         "The boot signature erased, so a driver refuses on its first\n// check. The easy corruption."),
        ("Fat12Circular", circular,
         "The cluster chain loops back on itself (2 -> 3 -> 2). A reader\n// that follows it without a guard never finishes."),
        ("Fat12OutOfRange", outofrange,
         "The chain leads to a cluster past the end of the volume, so the\n// next block a reader asks for does not exist."),
        ("Fat12SizeMismatch", sizemismatch,
         "The directory claims four clusters; the chain ends after one, so\n// a reader trusting the size runs off the file."),
        ("Fat12ZeroBps", zerobps,
         "Bytes-per-sector is zero. The boot signature is intact, so this\n// gets past the easy check and into the arithmetic."),
    ]
    hdr, src = emit(images)
    root = Path(__file__).resolve().parents[1] / "src"
    (root / "sd_images.h").write_text(hdr)
    (root / "sd_images.cpp").write_text(src)
    print(f"wrote {root/'sd_images.h'} and {root/'sd_images.cpp'}")
    for name, data in parse(bytes(hello)).items():
        print(f"  verified {name}: {len(data)} bytes")


if __name__ == "__main__":
    main()
