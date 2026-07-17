#!/usr/bin/env bash
#
# Build a Dreamcast-bootable .cdi of rayverse (Rayman 1).
#
# Data track: the contents of data/ end up at /cd/data, where the game looks
# for its files (see DATA_DIR in src/common.h).
#
# Music (default, MUSIC=ogg): the data/Music/*.ogg files (GOG Rayman Forever
# set) go onto the data track and are streamed through the game's software
# mixer, exactly like the PC build. No conversion, no drive seek delay.
#
# Music (MUSIC=cdda): the .ogg files are converted to 44.1 kHz s16 WAV and
# added as Red Book audio tracks IN ORDER — the game requests the original
# CD's track numbers (2..19, track 1 was the PC data track) and
# dc_cdda_init() remaps them onto the first audio track in the TOC. The ELF
# is rebuilt with DC_MUSIC_CDDA=1 to match. Converted WAVs are cached in
# cdda/. Useful if OGG decoding turns out to be too slow on real hardware.
#
# Usage:
#   tools/make_cdi.sh [output.cdi]
#
# Env overrides:
#   MKDCDISC     path to mkdcdisc (default: tools/mkdcdisc)
#   MUSIC        ogg (default) | cdda | none
#   PAD=0        skip data-track padding (-N): smaller image for SD/emulator
#                testing, do NOT burn such an image to CD-R
#
# Run it in Flycast with, for example:
#   ~/flycast-x86_64.AppImage rayverse.cdi

set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
mkdcdisc="${MKDCDISC:-$here/mkdcdisc}"
out="${1:-$root/rayverse.cdi}"
MUSIC="${MUSIC:-ogg}"

if [ -z "${KOS_BASE:-}" ]; then
    # KOS's environ scripts use unset variables internally -> suspend -u
    set +u
    # shellcheck disable=SC1091
    source /opt/toolchains/dc/kos/environ.sh
    set -u
fi

# The music backend is a compile-time switch; rebuild from scratch when the
# ELF on disk doesn't match (make doesn't track CFLAGS changes).
cdda_define=0
if [ "$MUSIC" = "cdda" ]; then
    cdda_define=1
fi
make -C "$root" -f Makefile.dc clean >/dev/null
make -C "$root" -f Makefile.dc DC_MUSIC_CDDA="$cdda_define"

# Stage the disc data directory: /cd/data
# The filenames are DOS 8.3 already, so ISO9660's single-dot rule is safe.
stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT
mkdir -p "$stage/data"
find "$root/data" -mindepth 1 -maxdepth 1 ! -name Music ! -name readme.txt \
    -exec cp -r {} "$stage/data/" \;

cddaflags=()
case "$MUSIC" in
    ogg)
        cp -r "$root/data/Music" "$stage/data/Music"
        ;;
    cdda)
        mkdir -p "$root/cdda"
        for ogg in $(ls "$root"/data/Music/rayman*.ogg | sort); do
            wav="$root/cdda/$(basename "${ogg%.ogg}").wav"
            if [ ! -f "$wav" ] || [ "$ogg" -nt "$wav" ]; then
                echo "Converting $(basename "$ogg") -> $(basename "$wav")"
                ffmpeg -loglevel error -y -i "$ogg" -ar 44100 -ac 2 -c:a pcm_s16le "$wav"
            fi
            cddaflags+=(-c "$wav")
        done
        ;;
    none)
        echo "NOTE: MUSIC=none - the game will run without music."
        ;;
    *)
        echo "ERROR: MUSIC must be ogg, cdda or none (got '$MUSIC')" >&2
        exit 1
        ;;
esac

# Padding: mkdcdisc's default data-track padding is REQUIRED for burned
# CD-Rs (real drives read the last track sectors unreliably) but bloats the
# image for SD/emulator testing. PAD=0 skips it (-N).
PAD="${PAD:-1}"
padflag=()
if [ "$PAD" = "0" ]; then
    padflag=(-N)
    echo "NOTE: PAD=0 - this image is for SD/emulator use, do NOT burn it."
fi

"$mkdcdisc" \
    -e "$root/rayverse.elf" \
    -D "$stage" \
    -o "$out" \
    -n "RAYVERSE" \
    "${cddaflags[@]}" \
    "${padflag[@]}"

echo "Wrote $out"
