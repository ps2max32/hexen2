# Hexen II (uHexen2, Hammer of Thyrion) on maxsdk: the PS2 port.
#
#   make [OUT=<dir>] [FRAMES=n] [ARGS="+map demo1"]   $(OUT)/hexen2.elf
#   make run             hexen2.elf in PCSX2 with the game data in
#                        build/pcsx2-test/data1 (H2DATA: a data1 directory,
#                        the demo's or the retail one patched to 1.11,
#                        copied there once)
#
# uHexen2's GL renderer (engine/hexen2, engine/h2shared) draws through
# libs/libgl (an OpenGL 1.1 subset on the GS); ps2/ has the system, video,
# input, sound and network layers (sound: maxsdk's mx/snd.h stream). CD
# audio and MIDI music are uHexen2's null drivers, the network loopback only.

HERE     := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
PS2MAX   ?= $(abspath $(HERE)/../..)
MAXSDK   ?= $(PS2MAX)/libs/maxsdk
MXGL     ?= $(PS2MAX)/libs/libgl
OUT      ?= $(PS2MAX)/build/apps/hexen2
all: $(OUT)/hexen2.elf
include $(MXGL)/mk/mxgl.mk
# the program in VM (ld/maxsdk-vm.ld), started by mxlink; MX_MEMORY=phys:
# all of it in physical memory (32 MiB, no VM in PCSX2's recompiler)
MX_MEMORY ?= vm
include $(MAXSDK)/mk/maxsdk.mk
# in VM: Hexen II in VM, libgl in core
MX_CORE_OBJS = $(MXGL_OBJS)

PICOLIBC ?= $(PS2MAX)/build/libs/picolibc/install
LIBC_LIBS = $(MAXSDK_OUT)/libc/glue.c.o $(MAXSDK_OUT)/libmxc.a $(PICOLIBC)/lib/libc.a \
            $(MX_LIB) $(MAXSDK_OUT)/librt.a

E        := $(HERE)/engine
P        := $(HERE)/ps2
# unsuffixed literals and the math.h functions are float (ps2/ps2_math.h):
# the FPU has no doubles
H2CFLAGS  = $(filter-out -fno-common -ffreestanding -Wall -Wextra -Wno-unused-parameter -std=gnu11,$(CFLAGS)) \
            -std=gnu99 -w -MMD -fno-strict-aliasing -ffast-math -fapprox-func \
            -cl-single-precision-constant -nostdlibinc -isystem $(PICOLIBC)/include \
            -DGLQUAKE -D__PS2__ -DNDEBUG -D_NO_CDAUDIO -D_NO_MIDIDRV \
            -I$(E)/hexen2 -I$(E)/h2shared -I$(HERE)/common -I$(P) -I$(OUT)/ps2 -I$(MXGL_INC) $(MXGL_DEFS) \
            -include $(P)/ps2_stdio.h -include $(P)/ps2_math.h

# uHexen2's Makefile (engine/hexen2/Makefile): GLOBJS and COMMONOBJS without
# the platform drivers, sound without codecs
ENGINE   := gl_refrag gl_rlight gl_rmain gl_rmisc r_part gl_rsurf gl_screen gl_warp \
            gl_draw gl_mesh gl_model \
            snd_sys snd_dma snd_mix snd_mem bgmusic snd_codec cd_null midi_nul \
            net_loop net_main chase cl_demo cl_effect cl_inlude cl_input cl_main \
            cl_parse cl_string cl_tent cl_cmd console keys menu sbar view wad cmd q_endian \
            link_ops sizebuf strlcat strlcpy qsnprint msg_io common debuglog quakefs crc \
            cvar cfgfile host host_cmd host_string mathlib pr_cmds pr_edict pr_exec \
            sv_effect sv_main sv_move sv_phys sv_user world zone hashindex
PS2SRC   := sys_ps2 gl_vidps2 in_ps2 snd_ps2 net_ps2 ps2_file

OBJS     := $(patsubst %,$(OUT)/h2/%.o,$(ENGINE)) $(patsubst %,$(OUT)/ps2/%.o,$(PS2SRC))

vpath %.c $(E)/hexen2 $(E)/h2shared $(HERE)/common
$(OUT)/h2/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(H2CFLAGS) -c $< -o $@
$(OUT)/ps2/%.o: $(P)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(H2CFLAGS) -c $< -o $@

# FRAMES: end after that many frames with a picture (host:hexen2.tga);
# ARGS: more of the command line. In $(OUT)/ps2/runcfg.h, rewritten only
# when they change.
RUNCFG := $(OUT)/ps2/runcfg.h
$(shell mkdir -p $(OUT)/ps2; printf '#define PS2_FRAMES %s\n#define PS2_ARGS "%s"\n#define PS2_PROF %s\n#define PS2_VM %s\n' \
  '$(or $(FRAMES),0)' '$(ARGS)' '$(or $(PROF),0)' '$(if $(filter vm,$(MX_MEMORY)),1,0)' > $(RUNCFG).new; \
  cmp -s $(RUNCFG).new $(RUNCFG) || mv $(RUNCFG).new $(RUNCFG); rm -f $(RUNCFG).new)

$(OUT)/hexen2.elf: $(OBJS) $(MXGL_OBJS) $(HLSL_OBJS) $(MX_LIB) $(MX_CRT0) $(MX_LD) $(LIBC_LIBS)
	$(call link,$@,--defsym=__stack_size=0x100000 $(MX_CRT0) $(OBJS) $(MXGL_OBJS) $(HLSL_OBJS) $(LIBC_LIBS))

# ---- running --------------------------------------------------------------------
# the demo (hexen2demo_nov1997's data1: pak0.pak, progs.dat, default.cfg ...)
H2DATA   ?= $(PS2MAX)/build/data/hexen2demo/data1
RUNDIR   := $(PS2MAX)/build/pcsx2-test
run: $(OUT)/hexen2.elf
	@[ -f $(RUNDIR)/data1/pak0.pak ] || { mkdir -p $(RUNDIR)/data1 && cp -rv "$(H2DATA)"/. $(RUNDIR)/data1/; }
ifeq ($(MX_MEMORY),vm)
	cp $(OUT)/hexen2.elf $(RUNDIR)/hexen2.elf && echo "host:hexen2.elf" > $(RUNDIR)/mxload.txt
	PCSX2_EE=interp $(PCSX2_RUN) $(MX_LOADER) $(or $(TIMEOUT),1800)
else
	$(PCSX2_RUN) $(OUT)/hexen2.elf $(or $(TIMEOUT),600)
endif

clean:
	rm -rf $(OUT)

.PHONY: all run clean

-include $(shell find $(OUT) -name '*.d' 2>/dev/null)
