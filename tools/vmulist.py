#!/usr/bin/env python3
"""List a Dreamcast VMU image the way the BIOS file manager does:
walk the root block -> directory -> FAT and validate each entry.
Usage: vmulist.py vmu_save_A1.bin [...]"""
import sys, struct

BLOCK = 512

def u16(b, o):
    return struct.unpack_from("<H", b, o)[0]

def list_vmu(path):
    img = open(path, "rb").read()
    print(f"=== {path} ({len(img)} bytes)")
    if len(img) < 256 * BLOCK:
        print("  too small for a VMU image"); return
    # Flycast .bin files are raw 128KB images; take the first 128KB
    img = img[:256 * BLOCK]
    root = img[255 * BLOCK:256 * BLOCK]
    if root[:16] != b"\x55" * 16:
        print("  NO VALID ROOT BLOCK (magic missing) -> BIOS would prompt to format!")
        return
    fat_block = u16(root, 0x46)
    fat_size = u16(root, 0x48)
    dir_block = u16(root, 0x4A)
    dir_size = u16(root, 0x4C)
    print(f"  root ok: FAT@{fat_block}x{fat_size}, DIR@{dir_block}x{dir_size}")
    fat = img[fat_block * BLOCK:(fat_block + fat_size) * BLOCK]
    free = sum(1 for i in range(256) if u16(fat, i * 2) == 0xFFFC)
    print(f"  free blocks: {free}")
    found = 0
    for db in range(dir_size):
        blk = img[(dir_block - db) * BLOCK:(dir_block - db + 1) * BLOCK]
        for e in range(16):
            ent = blk[e * 32:(e + 1) * 32]
            ftype = ent[0]
            if ftype == 0:
                continue
            found += 1
            first = u16(ent, 2)
            name = ent[4:16].decode("ascii", "replace").rstrip("\x00")
            size_blocks = u16(ent, 0x18)
            hdr_off = u16(ent, 0x1A)
            # follow FAT chain like the BIOS does
            chain, cur = [], first
            while cur < 256 and len(chain) <= size_blocks + 2:
                chain.append(cur)
                nxt = u16(fat, cur * 2)
                if nxt == 0xFFFA:
                    break
                if nxt == 0xFFFC:
                    chain.append(-1)
                    break
                cur = nxt
            chain_ok = len(chain) == size_blocks and -1 not in chain
            # check VMS header at header offset
            hdr_block = chain[hdr_off] if hdr_off < len(chain) else None
            desc = ""
            if hdr_block is not None:
                desc = img[hdr_block * BLOCK:hdr_block * BLOCK + 16].decode("ascii", "replace")
            print(f"  entry: type={ftype:#04x} name='{name}' blocks={size_blocks} "
                  f"first={first} hdr_off={hdr_off} chain_ok={chain_ok} desc='{desc}'")
    if not found:
        print("  no directory entries -> BIOS shows 'No files found'")

for p in sys.argv[1:]:
    list_vmu(p)
