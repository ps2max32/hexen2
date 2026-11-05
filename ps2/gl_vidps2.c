/* gl_vidps2.c: the video of Hexen II on maxsdk: the screen is libgl's
   (libs/libgl): the TV's (NTSC 640 x 448, PAL 640 x 512) or 1080i
   (vid_mode 1), with or without beam racing (vid_beam 1), in 16 or 24-bit
   colour (vid_rgb24 1: beam racing in the TV's mode only); the Display
   menu's video page sets them. Made from uHexen2's gl_viddos.c (the
   palette tables, GL_Init) and apps/quake1's vid_ps2.c (the modes). */

#define __GL_FUNC_EXTERN

#include <mx/mx.h>
#include <mxgl.h>

#include "quakedef.h"

#ifndef GL_SHARED_TEXTURE_PALETTE_EXT
#define GL_SHARED_TEXTURE_PALETTE_EXT 0x81FB
#endif
void APIENTRY glColorTableEXT(GLenum target, GLenum ifmt, GLsizei width, GLenum format, GLenum type,
                              const GLvoid *table);

#define WARP_WIDTH 320
#define WARP_HEIGHT 200

viddef_t vid;
modestate_t modestate = MS_UNINIT;
qboolean in_mode_set = false;

byte globalcolormap[VID_GRADES * 256];
float RTint[256], GTint[256], BTint[256];
unsigned short d_8to16table[256];
unsigned int d_8to24table[256];
unsigned int d_8to24TranslucentTable[256];
unsigned char *inverse_pal;

qboolean is_3dfx = false;
GLint gl_max_size = 256;
qboolean gl_tex_NPOT = false;
GLfloat gl_max_anisotropy = 1;
float gldepthmin, gldepthmax;
qboolean is8bit = false;
qboolean gl_mtexable = false;
qboolean have_stencil = false;
cvar_t _enable_mouse = {"_enable_mouse", "0", CVAR_ARCHIVE};

static const char *gl_extensions;
static qboolean vid_initialized = false;

/* 0: the TV's mode (NTSC or PAL, the console's), 1: 1080i (not kept: a
   TV without 1080i would show nothing at the next start) */
static cvar_t vid_mode = {"vid_mode", "0", CVAR_NONE};
/* beam racing (libgl's MXGL_BEAM): one buffer drawn just ahead of the beam,
   which leaves GS memory for twice the textures; at most a frame a field */
static cvar_t vid_beam = {"vid_beam", "0", CVAR_ARCHIVE};
/* 24-bit colour (MXGL_RGB24), with beam racing in the TV's mode */
static cvar_t vid_rgb24 = {"vid_rgb24", "0", CVAR_ARCHIVE};
/* the textures' top levels left out: -1 only while GS memory overflows or
   the swap file is read (MXGL_TEX_AUTO, the default); 0, 1, 2 always */
static cvar_t gl_ps2texquality = {"gl_ps2texquality", "-1", CVAR_ARCHIVE};
/* its limits (mxgl_texture_limits): GS memory high and low (% of the
   room), swap-ins a second */
static cvar_t gl_ps2texlimits = {"gl_ps2texlimits", "120 80 30", CVAR_ARCHIVE};

static void VID_MenuDraw(void);
static void VID_MenuKey(int key);

void VID_LockBuffer(void) {}
void VID_UnlockBuffer(void) {}
void VID_HandlePause(qboolean paused) {}
void VID_ChangeConsize(int dir) {}
float VID_ReportConsize(void) { return 1; }
void VID_ToggleFullscreen(void) {}
void VID_Shutdown(void) {}
/* no gamma ramps */
void VID_ShiftPalette(const unsigned char *palette) {}
void VID_SetPalette(const unsigned char *palette) {}

/* ---- the palette tables (gl_viddos.c) ---------------------------------------- */

const int ColorIndex[16] = {0, 31, 47, 63, 79, 95, 111, 127, 143, 159, 175, 191, 199, 207, 223, 231};
const unsigned int ColorPercent[16] = {25, 51, 76, 102, 114, 127, 140, 153,
                                       165, 178, 191, 204, 216, 229, 237, 247};

#define INVERSE_PALNAME "gfx/invpal.lmp"
static int ConvertTrueColorToPal(const unsigned char *true_color, const unsigned char *palette) {
	long min_dist = 256 * 256 + 256 * 256 + 256 * 256;
	int min_index = -1;
	for (int i = 0; i < 256; i++) {
		long dr = palette[3 * i] - true_color[0];
		long dg = palette[3 * i + 1] - true_color[1];
		long db = palette[3 * i + 2] - true_color[2];
		long dist = dr * dr + dg * dg + db * db;
		if (dist < min_dist) {
			min_dist = dist;
			min_index = i;
		}
	}
	return min_index;
}

/* the nearest palette index of each 15-bit colour, made once and kept in
   host:data1/gfx/invpal.lmp */
static void VID_CreateInversePalette(const unsigned char *palette) {
	long idx = 0;
	unsigned char true_color[3];
	Con_SafePrintf("Creating inverse palette\n");
	for (long r = 0; r < (1 << INVERSE_PAL_R_BITS); r++)
		for (long g = 0; g < (1 << INVERSE_PAL_G_BITS); g++)
			for (long b = 0; b < (1 << INVERSE_PAL_B_BITS); b++) {
				true_color[0] = (unsigned char)(r << (8 - INVERSE_PAL_R_BITS));
				true_color[1] = (unsigned char)(g << (8 - INVERSE_PAL_G_BITS));
				true_color[2] = (unsigned char)(b << (8 - INVERSE_PAL_B_BITS));
				inverse_pal[idx++] = ConvertTrueColorToPal(true_color, palette);
			}
	FS_CreatePath(FS_MakePath(FS_USERDIR, NULL, INVERSE_PALNAME));
	FS_WriteFile(INVERSE_PALNAME, inverse_pal, INVERSE_PAL_SIZE);
}

static void VID_InitPalette(const unsigned char *palette) {
	const unsigned char *pal = palette;
	unsigned int *table = d_8to24table;
	for (int i = 0; i < 256; i++, pal += 3)
		*table++ = (255u << 24) + (pal[0] << 0) + (pal[1] << 8) + (pal[2] << 16);
	d_8to24table[255] &= 0xffffff; /* 255 is transparent */

	table = d_8to24TranslucentTable;
	for (int i = 0; i < 16; i++) {
		int c = ColorIndex[i] * 3;
		unsigned r = palette[c], g = palette[c + 1], b = palette[c + 2];
		for (int p = 0; p < 16; p++) {
			*table++ = (ColorPercent[15 - p] << 24) + (r << 0) + (g << 8) + (b << 16);
			RTint[i * 16 + p] = (float)r / (float)ColorPercent[15 - p];
			GTint[i * 16 + p] = (float)g / (float)ColorPercent[15 - p];
			BTint[i * 16 + p] = (float)b / (float)ColorPercent[15 - p];
		}
	}

	int mark = Hunk_LowMark();
	inverse_pal = (unsigned char *)FS_LoadHunkFile(INVERSE_PALNAME, NULL);
	if (inverse_pal != NULL && fs_filesize != INVERSE_PAL_SIZE) {
		Hunk_FreeToLowMark(mark);
		inverse_pal = NULL;
	}
	if (inverse_pal == NULL) {
		inverse_pal = (unsigned char *)Hunk_AllocName(INVERSE_PAL_SIZE + 1, INVERSE_PALNAME);
		VID_CreateInversePalette(palette);
	}
}

/* ---- GL ------------------------------------------------------------------------ */

static void GL_Init(void) {
	Con_SafePrintf("GL_VENDOR: %s\n", (const char *)glGetString(GL_VENDOR));
	Con_SafePrintf("GL_RENDERER: %s\n", (const char *)glGetString(GL_RENDERER));
	Con_SafePrintf("GL_VERSION: %s\n", (const char *)glGetString(GL_VERSION));
	gl_extensions = (const char *)glGetString(GL_EXTENSIONS);
	/* 128 (MXGL_MAX_TEXTURE): the world's and the models' textures and
	   the 2D's pictures at most 128 a side; the console's font whole
	   (gl_draw.c's GL_Upload32) */
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &gl_max_size);

	/* one texture unit (libgl's second is a second pass); the stencil's one bit
	   is enough for the shadows (each pixel darkened once), -nostencil: without */
	gl_mtexable = false;
	{
		GLint bits = 0;
		glGetIntegerv(GL_STENCIL_BITS, &bits);
		have_stencil = bits > 0 && !COM_CheckParm("-nostencil");
	}

	glCullFace(GL_FRONT);
	glEnable(GL_TEXTURE_2D);
	glEnable(GL_ALPHA_TEST);
	glAlphaFunc(GL_GREATER, 0.632); /* 1 - e^-1, as gl_viddos.c */
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glShadeModel(GL_FLAT);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
}

/* the mode of vid_mode, vid_beam, vid_rgb24: the 2D (console, status bar,
   menus) in the TV's 640 x 448 or 640 x 512, in 1080i in 640 x 360 (the
   size it has at 448 lines); the 3D in all of the screen
   (GL_BeginRendering) */
static int vid_cur = -1;
static void VID_ApplyMode(void) {
	int hd = vid_mode.integer == 1;
	int flags = (vid_beam.integer ? MXGL_BEAM : 0) | (vid_rgb24.integer ? MXGL_RGB24 : 0);
	int want = (hd ? MXGL_1080I : MXGL_SD) | flags;
	if (want == vid_cur)
		return;
	vid_cur = want;
	in_mode_set = true;
	if (mxgl_set_mode(want) < 0) {
		Con_Printf("Beam racing: not enough memory\n");
		Cvar_SetValueQuick(&vid_beam, 0);
		vid_cur &= ~MXGL_BEAM;
	}
	in_mode_set = false;
	modestate = MS_FULLDIB;
	int w, h;
	mxgl_screen(&w, &h);
	vid.conwidth = vid.width = 640;
	vid.conheight = vid.height = hd ? 360 : h;
	vid.recalc_refdef = 1;
	if (vid_initialized)
		Con_CheckResize();
	flags = mxgl_mode_flags();
	Con_SafePrintf("Video mode %dx%d (%s)%s%s.\n", w, h,
	               hd ? "1080i" : mxgl_mode() == MXGL_PAL ? "PAL" : "NTSC",
	               flags & MXGL_BEAM ? ", beam racing" : "", flags & MXGL_RGB24 ? ", 24-bit colour" : "");
}

void GL_BeginRendering(int *x, int *y, int *width, int *height) {
	VID_ApplyMode();
	static int q = -2;
	if (gl_ps2texquality.integer != q) {
		mxgl_texture_quality(q = gl_ps2texquality.integer);
		int hi = 120, lo = 80, sw = 30;
		sscanf(gl_ps2texlimits.string, "%d%*c%d%*c%d", &hi, &lo, &sw);
		mxgl_texture_limits(hi, lo, sw);
	}
	*x = *y = 0;
	mxgl_screen(width, height);
}

void GL_EndRendering(void) {
	if (scr_skipupdate)
		return;
	mxgl_end_frame();
	mxgl_frame_done();
}

void D_ShowLoadingSize(void) {}

void VID_Init(const unsigned char *palette) {
	Cvar_RegisterVariable(&vid_mode);
	Cvar_RegisterVariable(&vid_beam);
	Cvar_RegisterVariable(&vid_rgb24);
	Cvar_RegisterVariable(&gl_ps2texquality);
	Cvar_RegisterVariable(&gl_ps2texlimits);
	Cvar_RegisterVariable(&_enable_mouse);
	Cvar_RegisterVariable(&gl_lightmapfmt);

	VID_InitPalette(palette);

	vid.numpages = 2;
	vid.maxwarpwidth = WARP_WIDTH;
	vid.maxwarpheight = WARP_HEIGHT;
	vid.colormap = host_colormap;
	vid.fullbright = 256 - LittleLong(*((int *)vid.colormap + 2048));

	mxgl_init();
	VID_ApplyMode();
	GL_SetupLightmapFmt();
	GL_Init();

	/* the shared palette, 255 its transparent entry: libgl keeps the textures
	   made of its colours as 8-bit indices */
	glColorTableEXT(GL_SHARED_TEXTURE_PALETTE_EXT, GL_RGBA, 256, GL_RGBA, GL_UNSIGNED_BYTE, d_8to24table);
	glEnable(GL_SHARED_TEXTURE_PALETTE_EXT);

	vid_initialized = true;
	vid.recalc_refdef = 1;
	Con_SafePrintf("Video initialized.\n");
	vid_menudrawfn = VID_MenuDraw;
	vid_menukeyfn = VID_MenuKey;
}

/* ---- the video menu: resolution, beam racing, colour; X or left / right
   changes the line's setting, which is set at once ---------------------- */

static int vid_cursor;
#define VID_LINES 3

static void VID_MenuDraw(void) {
	ScrollTitle("gfx/menu/title7.lmp");
	int flags = mxgl_mode_flags();
	M_Print(32, 92, "      Resolution");
	M_PrintWhite(168, 92, vid_mode.integer == 1 ? "1080i" : "TV (NTSC/PAL)");
	M_Print(32, 100, "     Beam racing");
	M_PrintWhite(168, 100, flags & MXGL_BEAM ? "on" : "off");
	M_Print(32, 108, "          Colour");
	M_PrintWhite(168, 108, flags & MXGL_RGB24 ? "24-bit" : vid_rgb24.integer ? "16 (24: TV+beam)" : "16-bit");
	M_DrawCharacter(152, 92 + 8 * vid_cursor, 12 + ((int)(realtime * 4) & 1));
	M_Print(32, 132, "Beam racing: more room for");
	M_Print(32, 140, "textures, at most 60 (PAL 50)");
	M_Print(32, 148, "frames a second");
	M_Print(32, 164, "1080i needs a TV that takes it");
	M_Print(32, 172, "(component cable)");
}

static void VID_MenuKey(int key) {
	switch (key) {
	case K_ESCAPE:
		vid_cursor = 0;
		M_Menu_Options_f();
		break;
	case K_UPARROW:
		S_LocalSound("raven/menu1.wav");
		vid_cursor = (vid_cursor + VID_LINES - 1) % VID_LINES;
		break;
	case K_DOWNARROW:
		S_LocalSound("raven/menu1.wav");
		vid_cursor = (vid_cursor + 1) % VID_LINES;
		break;
	case K_LEFTARROW:
	case K_RIGHTARROW:
	case K_ENTER:
		S_LocalSound("raven/menu2.wav");
		if (vid_cursor == 0)
			Cvar_SetValueQuick(&vid_mode, vid_mode.integer != 1);
		else if (vid_cursor == 1)
			Cvar_SetValueQuick(&vid_beam, !vid_beam.integer);
		else
			Cvar_SetValueQuick(&vid_rgb24, !vid_rgb24.integer);
		VID_ApplyMode();
		break;
	}
}
