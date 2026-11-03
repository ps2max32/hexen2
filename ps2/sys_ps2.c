/* sys_ps2.c: the system layer of Hexen II (uHexen2's GL client) on
   maxsdk: main, time, the Sys_ file calls (on the IOP file server, host:,
   as stdio is through ps2_file.c), errors. The video and input are in
   gl_vidps2.c and in_ps2.c, the sound driver in snd_ps2.c, the network
   (loopback only) in net_ps2.c. */

#include <fnmatch.h>

#include <mx/mx.h>
#include <mx/prof.h>
#include <mx/sif.h>
#include <mx/vm.h>
#include <mxgl.h>

#include "quakedef.h"
#include "debuglog.h"

void mx_exit(int status) __attribute__((noreturn));
void IN_PS2_Binds(void); /* in_ps2.c */

/* frames to run before the program ends by itself (0: never) with a
   picture, and more of the command line; set by the Makefile */
#include "runcfg.h"

cvar_t sys_nostdout = {"sys_nostdout", "0", CVAR_NONE};
cvar_t sys_throttle = {"sys_throttle", "0", CVAR_ARCHIVE};
qboolean isDedicated;

static quakeparms_t parms;

static void ps2_end(int status) __attribute__((noreturn));
static void ps2_end(int status) {
	mx_printf("@@PS2TEST_END@@ status=%d\n", status);
	mx_exit(status);
}

/* ---- files ------------------------------------------------------------------ */

/* "host:..." as the IOP file server takes it: the engine's paths are
   basedir "host:" + "/data1/..." */
static const char *host_path(const char *path, char *buf, size_t room) {
	const char *p = !strncmp(path, "host:", 5) ? path + 5 : path;
	while (*p == '/')
		p++;
	q_snprintf(buf, room, "host:%s", p);
	return buf;
}

int Sys_mkdir(const char *path, qboolean crash) {
	char hp[MAX_OSPATH];
	host_path(path, hp, sizeof hp);
	if (Sys_FileType(path) == FS_ENT_DIRECTORY)
		return 0;
	int rc = mx_fio_mkdir(hp, 0777) < 0 ? -1 : 0;
	if (rc != 0 && crash)
		Sys_Error("Unable to create directory %s", path);
	return rc;
}

int Sys_rmdir(const char *path) {
	char hp[MAX_OSPATH];
	return mx_fio_rmdir(host_path(path, hp, sizeof hp)) < 0 ? -1 : 0;
}

int Sys_unlink(const char *path) {
	char hp[MAX_OSPATH];
	return mx_fio_remove(host_path(path, hp, sizeof hp)) < 0 ? -1 : 0;
}

/* the file server has no rename: a copy, then the old one removed */
int Sys_rename(const char *oldp, const char *newp) {
	if (Sys_CopyFile(oldp, newp) != 0)
		return -1;
	return Sys_unlink(oldp);
}

long Sys_filesize(const char *path) {
	char hp[MAX_OSPATH];
	int fd = mx_fio_open(host_path(path, hp, sizeof hp), MX_FIO_RDONLY);
	if (fd < 0)
		return -1;
	long size = mx_fio_lseek(fd, 0, MX_FIO_SEEK_END);
	mx_fio_close(fd);
	return size;
}

int Sys_FileType(const char *path) {
	char hp[MAX_OSPATH];
	host_path(path, hp, sizeof hp);
	int fd = mx_fio_open(hp, MX_FIO_RDONLY);
	if (fd >= 0) {
		mx_fio_close(fd);
		return FS_ENT_FILE;
	}
	fd = mx_fio_dopen(hp);
	if (fd >= 0) {
		mx_fio_dclose(fd);
		return FS_ENT_DIRECTORY;
	}
	return FS_ENT_NONE;
}

int Sys_CopyFile(const char *frompath, const char *topath) {
	FILE *in = fopen(frompath, "rb");
	if (!in) {
		Con_Printf("%s: unable to open %s\n", __thisfunc__, frompath);
		return 1;
	}
	FILE *out = fopen(topath, "wb");
	if (!out) {
		Con_Printf("%s: unable to create %s\n", __thisfunc__, topath);
		fclose(in);
		return 1;
	}
	static char buf[8192];
	size_t n;
	int err = 0;
	while ((n = fread(buf, 1, sizeof buf, in)) > 0)
		if (fwrite(buf, 1, n, out) != n) {
			err = 1;
			break;
		}
	fclose(in);
	fclose(out);
	return err;
}

/* the names in a directory that match a pattern (fnmatch) */
static int find_fd = -1;
static char find_pattern[MAX_OSPATH];
static char find_name[MAX_OSPATH];

const char *Sys_FindFirstFile(const char *path, const char *pattern) {
	char hp[MAX_OSPATH];
	if (find_fd >= 0)
		Sys_Error("Sys_FindFirst without FindClose");
	find_fd = mx_fio_dopen(host_path(path, hp, sizeof hp));
	if (find_fd < 0)
		return NULL;
	q_strlcpy(find_pattern, pattern, sizeof find_pattern);
	return Sys_FindNextFile();
}

const char *Sys_FindNextFile(void) {
	u32 size;
	if (find_fd < 0)
		return NULL;
	while (mx_fio_dread(find_fd, find_name, sizeof find_name, &size) > 0)
		if (!fnmatch(find_pattern, find_name, FNM_PATHNAME))
			return find_name;
	return NULL;
}

void Sys_FindClose(void) {
	if (find_fd >= 0)
		mx_fio_dclose(find_fd);
	find_fd = -1;
}

/* ---- the system -------------------------------------------------------------- */

void Sys_MakeCodeWriteable(unsigned long startaddr, unsigned long length) {}

void Sys_PrintTerm(const char *msgtxt) {
	if (sys_nostdout.integer)
		return;
	/* Hexen II's high-bit characters (coloured text) as plain ones */
	char text[1024];
	size_t i = 0;
	for (const unsigned char *p = (const unsigned char *)msgtxt; *p; p++) {
		text[i++] = *p & 0x7f;
		if (i == sizeof text - 1) {
			text[i] = 0;
			mx_printf("%s", text);
			i = 0;
		}
	}
	text[i] = 0;
	mx_printf("%s", text);
}

void Sys_Error(const char *error, ...) {
	va_list ap;
	char text[MAX_PRINTMSG];
	va_start(ap, error);
	q_vsnprintf(text, sizeof text, error, ap);
	va_end(ap);
	mx_printf("\nFATAL ERROR: %s\n\n", text);
	/* the shutdown once: it can fail into here again */
	if (host_parms && host_parms->errstate++ == 0)
		Host_Shutdown();
	ps2_end(1);
}

void Sys_Quit(void) {
	Host_Shutdown();
	mx_printf("Sys_Quit\n");
	ps2_end(0);
}

static u64 cycles(void) {
	static u32 last, high;
	u32 c = mx_count();
	if (c < last)
		high++;
	last = c;
	return (u64)high << 32 | c;
}

double Sys_DoubleTime(void) {
	static u64 base;
	u64 c = cycles();
	if (!base)
		base = c;
	return (double)(c - base) / MX_EE_HZ;
}

/* no clock: save games show this date */
char *Sys_DateTimeString(char *buf) {
	strcpy(buf, "01/01/2026 00:00:00");
	return buf;
}

/* debuglog.c's log (-condebug) only: no POSIX files here */
int open(const char *path, int flags, ...) { return -1; }
int close(int fd) { return -1; }

const char *Sys_ConsoleInput(void) { return NULL; }
void Sys_Sleep(unsigned long msecs) {}
char *Sys_GetClipboardData(void) { return NULL; }

/* ---- main --------------------------------------------------------------------- */

/* maxsdk's virtual memory (<mx/vm.h>): malloc (the hunk) in up to 128 MiB,
   paged to a swap file next to the game's data; what the GS reads by DMA
   (libgl: arenas, textures) is pinned memory, frames taken as needed.
   MX_MEMORY=phys: no VM, everything in the 32 MiB */
#if PS2_VM
const struct mx_vm_config mx_vm_config = {
	.swap = "host:hexen2.swp",
};
#endif

int main(int argc, char **argv) {
	/* the command line: the settings for the PS2, then PS2_ARGS */
	/* the textures at full size (up to 256: libgl leaves their top levels
	   out while GS memory refills, MXGL_TEX_AUTO in gl_vidps2.c), trilinear;
	   the lightmaps a byte a texel (libgl draws them as Cd * As) */
	static char line[512] = "hexen2 -lm_1 +gl_texturemode GL_LINEAR_MIPMAP_LINEAR " PS2_ARGS;
	static char *args[64];
	int n = 0;
	for (char *p = strtok(line, " "); p && n < 63; p = strtok(NULL, " "))
		args[n++] = p;

	memset(&parms, 0, sizeof parms);
	parms.basedir = "host:";
	parms.userdir = "host:";
	parms.argc = n;
	parms.argv = args;
	host_parms = &parms;

	LOG_Init(&parms);
	COM_ValidateByteorder();

	/* the hunk: 32 MiB as uHexen2's default (STD_MEM_ALLOC), or
	   -heapsize <KiB> */
	parms.memsize = 32 * 1024 * 1024;
	for (int i = 1; i + 1 < n; i++)
		if (!strcmp(args[i], "-heapsize"))
			parms.memsize = atoi(args[i + 1]) * 1024;
	parms.membase = malloc(parms.memsize);
	if (!parms.membase)
		Sys_Error("no memory for the hunk");
	mx_printf("Hexen II (uHexen2) on maxsdk: hunk %d MiB, virtual memory %s\n", parms.memsize >> 20,
	          mx_vm_active() ? "on" : "off");

	Host_Init();
	IN_PS2_Binds();

	/* frames: those Host_Frame ran (host_framecount; it skips the calls
	   that come sooner than host_maxfps allows) */
	double oldtime = Sys_DoubleTime();
	int last = -1;
	for (;;) {
		double newtime = Sys_DoubleTime();
		Host_Frame(newtime - oldtime);
		oldtime = newtime;
		if (host_framecount == last)
			continue;
		last = host_framecount;
		if (last % 60 == 0)
			mx_printf("ps2: frame %d at %d ms\n", last, (int)(newtime * 1000));
#if PS2_PROF
		/* the profile of the second half of the run */
		if (PS2_FRAMES && last == PS2_FRAMES / 2)
			mx_prof_start(2000);
#endif
		if (PS2_FRAMES && last + 1 >= PS2_FRAMES) {
#if PS2_PROF
			mx_prof_stop();
			mx_prof_report(600);
#endif
			int r = mxgl_screenshot("host:hexen2.tga");
			mx_printf("ps2: %d frames, picture host:hexen2.tga: %d\n", last + 1, r);
			ps2_end(0);
		}
	}
	return 0;
}
