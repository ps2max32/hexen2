# Hexen II on maxsdk (PS2)

uHexen2 (Hammer of Thyrion, GPL) built for the PS2 with `libs/maxsdk` (clang, R5900) by
`Makefile`: its GL client (`engine/hexen2`, `engine/h2shared`, `common`). The port is in
`ps2/`; the engine changes are under `PLATFORM_PS2` (`common/arch_def.h`, defined for
`__PS2__`) and few: `common/net_sys.h` (the socket type, no sockets), `sys.h` (no user
directories), `snd_sys.[ch]` (the PS2 sound driver), `debuglog.c` (its includes),
`gl_draw.c` (the console's font whole, below).

```
make [FRAMES=n] [ARGS="+map demo1"]    $(OUT)/hexen2.elf (build/apps/hexen2)
make run                               copies a data1 directory (H2DATA, by default
                                       build/data/hexen2demo/data1) to
                                       build/pcsx2-test/data1 and runs it in PCSX2
```

`FRAMES=n` ends the program after n drawn frames with a picture (`host:hexen2.tga`) and
the test end marker; `ARGS` is added to the command line. It starts at the main menu (the
demo's `hexen.rc` has `startdemos` commented out); `ARGS="+map demo1"` goes to a level.

## Data

- Demo: the November 1997 demo (uHexen2's `hexen2demo_nov1997-linux-i586.tgz` on
  SourceForge): its `data1` (`pak0.pak`, `progs.dat`, `default.cfg`, `hexen.rc`, ...).
  Three levels (`demo1` .. `demo3`) and `ravdm1`.
- Retail: `data1` of a retail copy patched to 1.11 (uHexen2's `h2patch`); the mission pack
  (`portals`) is not tried yet.

## How

| Part | How |
|---|---|
| Renderer | uHexen2's GL renderer on `libs/libgl` (OpenGL 1.1 subset on the GS), the GL calls direct (no `GL_DLSYM`); one texture unit (no multitexture), no stencil (no `r_shadows`); lightmaps a byte a texel (`-lm_1`: `GL_LUMINANCE`, blend `ZERO, ONE_MINUS_SRC_COLOR`, which libgl draws as `Cd * As`, as Quake's); textures at most 128 a side (libgl's `GL_MAX_TEXTURE_SIZE`) but the 2D's that are a power of two up to 256 (`gl_draw.c`: the 256 x 128 console font halved lost every other column of its letters) |
| Video | `ps2/gl_vidps2.c` (from uHexen2's `gl_viddos.c` and apps/quake1's `vid_ps2.c`): the TV's mode, NTSC 640 x 448 or PAL 640 x 512 (the console's), or 1080i, with or without beam racing, 16 or 24-bit colour (below); the 2D in 640 x 448 / 512, in 1080i 640 x 360; trilinear; texture quality `gl_ps2texquality -1` (libgl's `MXGL_TEX_AUTO`), `gl_ps2texlimits 120,80,30` |
| Files | stdio on the IOP file server (`host:`), `ps2/ps2_file.c` (apps/quake1's); the `Sys_` file calls (find, mkdir, file size, copy; rename is a copy) on `mx_fio_*` in `ps2/sys_ps2.c`; basedir and userdir `host:` |
| Input | pad 0 (`ps2/in_ps2.c`): in the menus and the console the d-pad is the arrows, cross Enter, circle Escape, square Backspace; in the game R1 attack, cross jump, circle crouch, square use item, triangle next weapon, L1 / L2 inventory left / right, R2 run, L3 centre view (AUX keys, bound after the configs only where unbound: config.cfg keeps the player's own); left stick moves, right stick turns and looks; START menu, SELECT `+showinfo`, R3 the counters on/off |
| Memory | maxsdk's virtual memory (`mx_vm_config` in `ps2/sys_ps2.c`): `malloc` in up to 128 MiB, paged to `host:hexen2.swp`; the hunk 32 MiB as uHexen2's default (`-heapsize <KiB>`); built into VM (`MX_MEMORY=vm`, the default: started by mxlink directly, or by `mxload.elf`, as `make run` does), only maxsdk, libc and libgl in physical memory; stack 1 MiB. In PCSX2 only on the EE interpreter (`PCSX2_EE=interp`) |
| Sound | uHexen2's mixer (`snd_dma.c`, `snd_mix.c`) at 22050 Hz 16-bit stereo into a ring, sent to maxsdk's PCM stream (`mx/snd.h`, audsrv) by `ps2/snd_ps2.c` (`snddrv_ps2`) as far as the mixer has painted |
| Network | loopback only (`ps2/net_ps2.c`; `net_dgrm.c` left out) |
| Math | float literals (`-cl-single-precision-constant`) and float math.h (`ps2/ps2_math.h`): the FPU has no doubles |

Not done yet: CD audio and MIDI music (uHexen2's null drivers), network play, Quake's fast
paths (`PS2_DrawPoly`, the world in a buffer object, alias models through
`mxgl_MultiDraw`): the world and models are drawn with uHexen2's `glBegin`/`glEnd`.

## Video modes: 1080i, beam racing, 24-bit colour

Options, Video Modes sets three things, each at once (as apps/quake1):

- Resolution: the TV's mode or 1080i (`vid_mode 0` / `1`, not kept in the config: a TV
  without 1080i would show nothing at the next start).
- Beam racing (`vid_beam`, libgl's `MXGL_BEAM`): one buffer of a field's lines, drawn in
  strips just ahead of the beam every field; more GS memory for textures, at most a frame
  a field (60, PAL 50).
- Colour (`vid_rgb24`, `MXGL_RGB24`): 24-bit colour, with beam racing in the TV's mode;
  16-bit (dithered) otherwise.

Only the TV's mode (PAL, 16-bit, no beam racing) is tried yet.

## Status

In PCSX2 (EE interpreter, VM): the menu, and `demo1` drawn right (about 2650 triangles,
1.4 MB of textures in GS memory, none evicted). On the PS2 `demo1` runs, but a run has also
been seen to lose the loader's connection while the level loads (no crash report; not found
yet). Loading a level takes long on PCSX2's interpreter.
