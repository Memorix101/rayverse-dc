# Rayverse
Work-in-progress modern port of Rayman 1 for PC (version 1.21), based on the disassembly of the original.

The aim is to provide a drop-in replacement for the original executable RAYMAN.EXE that works on modern platforms (including Windows, Linux and macOS).

It's recommended to use [Rayman Forever from GOG](https://www.gog.com/en/game/rayman_forever)

## Build instructions

### Dreamcast
Requires the [KallistiOS](https://kos-docs.dreamcast.wiki/) toolchain with the
`libtremor` kos-port. Put the Rayman 1 (PC v1.21) data files into `data/`, then:

```
source /opt/toolchains/dc/kos/environ.sh
make -f Makefile.dc          # builds rayverse.elf (for dcload / Flycast ELF boot)
tools/make_cdi.sh            # builds a bootable rayverse.cdi (data + music)
```

Useful `make_cdi.sh` variants:
```
PAD=0 tools/make_cdi.sh      # smaller image for emulator/SD use (do NOT burn)
MUSIC=cdda tools/make_cdi.sh # Red Book audio tracks instead of Tremor-decoded OGGs
MUSIC=none tools/make_cdi.sh # no music (fast test builds)
```

Saves go to the VMU in slot A1.

## Special thanks

Main collaborators on this project:
* **[RayCarrot](https://github.com/RayCarrot)**: many insights on the inner workings of the game; author of the [BinarySerializer](https://github.com/BinarySerializer/BinarySerializer.Ray1),
  [Rayman Control Panel](https://github.com/RayCarrot/RayCarrot.RCP.Metro), [Ray1Map](https://github.com/BinarySerializer/Ray1Map) 
  and [Ray1Editor](https://github.com/RayCarrot/RayCarrot.Ray1Editor) projects, among others.
* **[fuerchter](https://github.com/fuerchter)**: author of the matching decompilation of Rayman for PS1 project, see [rayman-ps1-decomp](https://github.com/fuerchter/rayman-ps1-decomp)
