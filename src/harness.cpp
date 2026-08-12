/*
	=======================================================================
	HARNESS.CPP - noctis-iv-lr universe-generation output harness
	=======================================================================

	PURPOSE
	-------
	Drive the REAL generation code of noctis-iv-lr (NOCTIS-0.CPP /
	NOCTIS-1.CPP / Brtl.cpp) headlessly and emit stable hash fingerprints
	(and optionally raw dumps) for every generated artifact.

	It never modifies generation logic: it only allocates the same buffers
	main() would, seeds the same global inputs (Parsis coordinates, landing
	point, secs), calls the same generation functions and hashes / dumps the
	resulting global state.

	The harness is intentionally driven entirely by command-line arguments
	so it can be scripted.

	COMMAND LINE
	------------
	  nivtest <command> [options]

	Global options (any command):
	  -secs <n>          set the global time counter `secs` (default 0).
	  -o <FILE>          append textual results to FILE (default: stdout).
	  -dump <DIR>        also write C-style lowercase raw dumps to DIR
	                     (surfmap.bin / atmover.bin / height.bin /
	                      objects.bin / surftex.bin / sky.bin).
	  NIVDUMP=<DIR>      also write Rust-style uppercase raw dumps to DIR
	                     (HEIGHT.bin / OBJECTS.bin / SURFTEX.bin / SKY.bin /
	                      palette.raw / SURFTEX_BEFORE.bin).

	Commands:
	  selftest
	      Emit hash self-test vectors + a fixed rand()/fast_random() sequence.

	  system  -x <X> -y <Y> -z <Z>
	      Solar-system generation for the star at Parsis coordinates (X,Y,Z).
	      Runs extract_ap_target_infos()+prepare_nearstar() and dumps every
	      star & body property.

	  scan  -x <X> -y <Y> -z <Z>
	      One-line summary hash of a whole system.

	  find  -x0 <X> -y0 <Y> -z0 <Z> [-step <S>] [-n <N>] [-class <C>]
	            [-ptype <T>] [-minnop <K>] [-max <M>]
	      Coordinate-grid star discovery helper.

	  planet  -x <X> -y <Y> -z <Z> -p <index>
	      Planet texture / global-map generation for body <index>. Calls
	      surface() and hashes the surface map (46080 = 360x128), the
	      atmosphere overlay (32400) and the palette slice (192).

	  planet-all  -x <X> -y <Y> -z <Z>
	      Compact one-line fingerprint for EVERY body.

	  sector  -x <X> -y <Y> -z <Z> -p <index> -lon <L> -lat <B>
	                [-sc <type>] [-albedo <A>] [-night <N>] [-gap <32hex>]
	      Surface SECTOR generation for a landing at longitude L / latitude B.
	      Hashes p_surfacemap heightmap (40000) and objectschart (40000) and
	      emits the C-only gap line (16 bytes read from p_surfacemap[40000..]).

	  surftex  -x <X> -y <Y> -z <Z> -p <index> -lon <L> -lat <B>
	                 [-sc <type>] [-albedo <A>] [-night <N>] [-gap <32hex>]
	      Same setup as `sector` but focuses on the surface TEXTURE buffer
	      (txtr aliasing p_background) and the sky buffer.

	Notes on determinism
	--------------------
	The generator mixes two RNGs: brtl (Borland LCG, brtl_srand/brtl_rand)
	and fast_random(). Both are re-seeded deterministically from the inputs,
	so given identical inputs the output is identical.
*/

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstdarg>
#include <unistd.h>
#include <sys/wait.h>

#include "brtl.h"
#include "noctis-d.h"
#include "noctis-0.h"

/* Functions living in NOCTIS-1.CPP but not declared in noctis-0.h. */
extern void build_surface(void);
extern void create_sky(int8_t atmosphere);
extern int8_t sctype;

/* Scenario type constants (from noctis-1.cpp). */
#define SC_OCEAN  1
#define SC_PLAINS 2
#define SC_DESERT 3
#define SC_ICY    4

/* nearstar_p_orb_seed is defined in NOCTIS-0.CPP but not declared in the
   header (mirrors the reference harness). */
extern double nearstar_p_orb_seed[maxbodies];

/* Landing-context globals defined in NOCTIS-1.CPP. */
extern float dsd1, dsd2;
extern float nray1, nray2;
extern float latitude;
extern float sun_x, sun_y, sun_z;
extern int32_t T_SCALE;
extern int8_t quartz, groundflares, gtx;
extern float rockscaling, rockpeaking, treescaling;
extern int16_t rockdensity;

/* ----------------------------------------------------------------------- */
/* hashing (FNV-1a + CRC-32, byte-compatible with NIVHASH.C)               */
/* ----------------------------------------------------------------------- */

typedef uint32_t nh_u32;

static nh_u32 nh__reflect(nh_u32 v, int bits)
{
	nh_u32 r = 0;
	for (int i = 0; i < bits; i++) {
		if (v & (1UL << i))
			r |= (1UL << (bits - 1 - i));
	}
	return r;
}

static nh_u32 nh_fnv1a(const uint8_t *data, size_t len)
{
	nh_u32 h = 2166136261UL; /* NH_FNV_OFFSET */
	for (size_t i = 0; i < len; i++) {
		h ^= (nh_u32)data[i];
		h *= 16777619UL; /* NH_FNV_PRIME */
	}
	return h;
}

static nh_u32 nh_fnv1a_update(nh_u32 h, const char *s, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		h ^= (nh_u32)(uint8_t)s[i];
		h *= 16777619UL;
	}
	return h;
}

static nh_u32 nh_crc32(const uint8_t *data, size_t len)
{
	nh_u32 crc = 0xFFFFFFFFUL;
	for (size_t i = 0; i < len; i++) {
		crc ^= (nh__reflect((nh_u32)data[i], 8) << 24);
		for (int k = 0; k < 8; k++) {
			if (crc & 0x80000000UL)
				crc = ((crc << 1) ^ 0x04C11DB7UL);
			else
				crc = (crc << 1);
		}
	}
	return nh__reflect(crc, 32) ^ 0xFFFFFFFFUL;
}

/* ----------------------------------------------------------------------- */
/* output routing                                                          */
/* ----------------------------------------------------------------------- */

static FILE *g_out = NULL;
static char  g_dumpdir[256] = "";

static void out(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(g_out ? g_out : stdout, fmt, ap);
	va_end(ap);
}

/* Write a raw buffer to <dumpdir>/<name> if dumping is enabled. */
static void dump_file(const char *name, const uint8_t *buf, size_t len)
{
	char path[320];
	FILE *f;
	if (!g_dumpdir[0])
		return;
	snprintf(path, sizeof(path), "%s/%s", g_dumpdir, name);
	f = fopen(path, "wb");
	if (!f) {
		out("; WARN cannot open dump %s\n", path);
		return;
	}
	fwrite(buf, 1, len, f);
	fclose(f);
}

/* Emit both hashes of a buffer on a single labelled line. */
static void hashline(const char *label, const uint8_t *buf, size_t len)
{
	nh_u32 fnv = nh_fnv1a(buf, len);
	nh_u32 crc = nh_crc32(buf, len);
	out("%-16s len=%-6zu fnv=%08lX crc=%08lX\n",
	    label, len, (unsigned long)fnv, (unsigned long)crc);
}

/* ----------------------------------------------------------------------- */
/* buffer allocation - mirrors the essential part of noctis.cpp main()     */
/* ----------------------------------------------------------------------- */

/* Allocate the generation buffers. p_surfacemap and objectschart are laid
   out contiguously with a 16-byte gap between them, exactly mirroring the
   C far-heap layout (farmalloc header between the two allocations) so that
   build_surface's inclination loop reads the same bytes as the reference. */
static int alloc_buffers(void)
{
	uint8_t *base;

	/* p_background / s_background as main() allocates them. */
	p_background = (uint8_t *)malloc(pl_bytes);
	s_background = (uint8_t *)malloc(st_bytes + 64);
	if (!p_background || !s_background)
		return 0;

	/* contiguous ps_bytes | 16 gap | oc_bytes region */
	base          = (uint8_t *)malloc(ps_bytes + 16 + oc_bytes);
	if (!base)
		return 0;
	p_surfacemap = base;
	objectschart = (struct quadrant *)(base + ps_bytes + 16);
	ruinschart   = (uint8_t *)objectschart; /* oc alias */
	txtr         = p_background;            /* txtr alias */

	for (int i = 0; i < 200; i++)
		m200[i] = (uint16_t)(i * 200);

	return lens_flares_init();
}

/* ----------------------------------------------------------------------- */
/* argument helpers                                                        */
/* ----------------------------------------------------------------------- */

static double argd(int argc, char **argv, const char *key, double def)
{
	for (int i = 1; i < argc - 1; i++)
		if (strcmp(argv[i], key) == 0)
			return atof(argv[i + 1]);
	return def;
}

static long argl(int argc, char **argv, const char *key, long def)
{
	for (int i = 1; i < argc - 1; i++)
		if (strcmp(argv[i], key) == 0)
			return atol(argv[i + 1]);
	return def;
}

static int has(int argc, char **argv, const char *key)
{
	for (int i = 1; i < argc; i++)
		if (strcmp(argv[i], key) == 0)
			return 1;
	return 0;
}

static const char *args(int argc, char **argv, const char *key, const char *def)
{
	for (int i = 1; i < argc - 1; i++)
		if (strcmp(argv[i], key) == 0)
			return argv[i + 1];
	return def;
}

/* ----------------------------------------------------------------------- */
/* shared setup: place a star at (x,y,z) and generate its system           */
/* ----------------------------------------------------------------------- */

static void gen_system(double x, double y, double z)
{
	/* -x, -y, -z are user-facing Parsis (the galactic chart negates y). */
	ap_target_x = x;
	ap_target_y = -y;
	ap_target_z = z;

	_delay = 0; /* so prepare_nearstar copies ap_target_* */
	extract_ap_target_infos();
	prepare_nearstar();
}

/* Emit the full per-body property table for the current nearstar. */
static void dump_system_props(void)
{
	out("STAR x=%.0f y=%.0f z=%.0f\n", ap_target_x, ap_target_y, ap_target_z);
	out("  identity=%.6f\n", nearstar_identity);
	out("  class=%d ray=%.6f spin=%d rgb=%d,%d,%d\n",
	    (int)nearstar_class, (double)nearstar_ray, (int)nearstar_spin,
	    (int)nearstar_r, (int)nearstar_g, (int)nearstar_b);
	out("  nop=%d nob=%d labeled=%d\n",
	    nearstar_nop, nearstar_nob, nearstar_labeled);

	for (int n = 0; n < nearstar_nob; n++) {
		out("  BODY %2d type=%d owner=%d moonid=%d ray=%.6f orb_ray=%.6f\n",
		    n, (int)nearstar_p_type[n], nearstar_p_owner[n],
		    (int)nearstar_p_moonid[n],
		    (double)nearstar_p_ray[n], (double)nearstar_p_orb_ray[n]);
		out("          tilt=%.8f orb_tilt=%.8f orb_orient=%.8f "
		    "orb_ecc=%.8f orb_seed=%.6f ring=%.6f\n",
		    (double)nearstar_p_tilt[n], (double)nearstar_p_orb_tilt[n],
		    (double)nearstar_p_orb_orient[n], (double)nearstar_p_orb_ecc[n],
		    (double)nearstar_p_orb_seed[n], (double)nearstar_p_ring[n]);
	}
}

/* Compute the exact seedval draw_planets() passes to surface() for body n. */
static double surface_seedval_for(int n)
{
	int is_moon = (nearstar_p_owner[n] > -1);
	if (is_moon) {
		if (nearstar_p_type[n])
			return 1000000.0 * nearstar_ray * nearstar_p_type[n]
			     * nearstar_p_orb_orient[n];
		else
			return 2000000.0 * n * nearstar_ray
			     * nearstar_p_orb_orient[n];
	} else {
		if (nearstar_p_type[n])
			return 1000000.0 * nearstar_p_type[n]
			     * nearstar_p_orb_seed[n] * nearstar_p_orb_tilt[n]
			     * nearstar_p_orb_ecc[n] * nearstar_p_orb_orient[n];
		else
			return 2000000.0 * n
			     * nearstar_p_orb_seed[n] * nearstar_p_orb_tilt[n]
			     * nearstar_p_orb_ecc[n] * nearstar_p_orb_orient[n];
	}
}

/* ----------------------------------------------------------------------- */
/* TEST SET 1 : solar system                                               */
/* ----------------------------------------------------------------------- */

static int cmd_system(int argc, char **argv)
{
	double x = argd(argc, argv, "-x", 0);
	double y = argd(argc, argv, "-y", 0);
	double z = argd(argc, argv, "-z", 0);

	gen_system(x, y, z);
	out("=== SYSTEM ===\n");
	dump_system_props();
	return 0;
}

/* find - scan a coordinate grid and print coordinates of matching systems. */
static int cmd_find(int argc, char **argv)
{
	double x0 = argd(argc, argv, "-x0", 3800000.0);
	double y0 = argd(argc, argv, "-y0", -4300000.0);
	double z0 = argd(argc, argv, "-z0", -900000.0);
	double step = argd(argc, argv, "-step", 100000.0);
	int n    = (int)argl(argc, argv, "-n", 12);
	int wclass = (int)argl(argc, argv, "-class", -1);
	int wptype = (int)argl(argc, argv, "-ptype", -1);
	int minnop = (int)argl(argc, argv, "-minnop", 0);
	int maxm   = (int)argl(argc, argv, "-max", 40);
	int found = 0;

	out("=== FIND (class=%d ptype=%d minnop=%d) ===\n",
	    wclass, wptype, minnop);
	for (int ix = 0; ix < n && found < maxm; ix++)
	for (int iy = 0; iy < n && found < maxm; iy++)
	for (int iz = 0; iz < n && found < maxm; iz++) {
		int has_ptype = 0;
		double x = x0 + ix * step;
		double y = y0 + iy * step;
		double z = z0 + iz * step;
		gen_system(x, y, z);
		if (wclass >= 0 && (int)nearstar_class != wclass)
			continue;
		if (nearstar_nop < minnop)
			continue;
		if (wptype >= 0) {
			for (int k = 0; k < nearstar_nob; k++)
				if ((int)nearstar_p_type[k] == wptype)
					has_ptype = 1;
			if (!has_ptype)
				continue;
		}
		out("FOUND x=%.0f y=%.0f z=%.0f class=%d nop=%d nob=%d\n",
		    x, y, z, (int)nearstar_class, nearstar_nop, nearstar_nob);
		found++;
	}
	out("; %d matches\n", found);
	return 0;
}

/* Compact one-line-ish system fingerprint. */
static int cmd_scan(int argc, char **argv)
{
	double x = argd(argc, argv, "-x", 0);
	double y = argd(argc, argv, "-y", 0);
	double z = argd(argc, argv, "-z", 0);
	nh_u32 h = 2166136261UL;
	char rec[64];

	gen_system(x, y, z);

	sprintf(rec, "%d %d %d %d",
	        (int)nearstar_class, nearstar_nop, nearstar_nob,
	        (int)nearstar_spin);
	h = nh_fnv1a_update(h, rec, strlen(rec));
	for (int n = 0; n < nearstar_nob; n++) {
		sprintf(rec, "%d %d %d", (int)nearstar_p_type[n],
		        nearstar_p_owner[n], (int)nearstar_p_moonid[n]);
		h = nh_fnv1a_update(h, rec, strlen(rec));
	}
	out("SCAN x=%.0f y=%.0f z=%.0f class=%d nop=%d nob=%d fnv=%08lX\n",
	    x, y, z, (int)nearstar_class, nearstar_nop, nearstar_nob,
	    (unsigned long)h);
	return 0;
}

/* ----------------------------------------------------------------------- */
/* TEST SET 2 : planet textures / global surface map                       */
/* ----------------------------------------------------------------------- */

static void gen_planet_surface(int n)
{
	int is_moon = (nearstar_p_owner[n] > -1);
	uint8_t colorbase = is_moon ? 128 : 192;
	double seedval = surface_seedval_for(n);

	if (is_moon) {
		uint8_t *save = p_background;
		p_background = s_background;
		surface(n, nearstar_p_type[n], seedval, colorbase);
		p_background = save;
	} else {
		surface(n, nearstar_p_type[n], seedval, colorbase);
	}
}

static void report_planet(int n)
{
	int is_moon = (nearstar_p_owner[n] > -1);
	uint8_t *surf = is_moon ? s_background : p_background;

	out("PLANET body=%d type=%d owner=%d seedval=%.6f\n",
	    n, (int)nearstar_p_type[n], nearstar_p_owner[n],
	    surface_seedval_for(n));
	out("  rtperiod=%d rotation=%d term_start=%d term_end=%d\n",
	    nearstar_p_rtperiod[n], nearstar_p_rotation[n],
	    nearstar_p_term_start[n], nearstar_p_term_end[n]);

	if (nearstar_p_type[n] == 10) {
		out("  (companion star - surface() is a no-op)\n");
		return;
	}

	/* SAMPLED_BYTES = 360 * 128 = 46080. */
	hashline("surface_map", surf, 46080UL);
	hashline("atmo_overlay", (uint8_t *)objectschart, 32400UL);
	{
		int base3 = (is_moon ? 128 : 192) * 3;
		hashline("palette64", (uint8_t *)(tmppal + base3), 192UL);
	}

	/* C-style lowercase dumps. */
	dump_file("surfmap.bin", surf, 64800UL);
	dump_file("atmover.bin", (uint8_t *)objectschart, 32400UL);
}

static int cmd_planet(int argc, char **argv)
{
	double x = argd(argc, argv, "-x", 0);
	double y = argd(argc, argv, "-y", 0);
	double z = argd(argc, argv, "-z", 0);
	int    p = (int)argl(argc, argv, "-p", 0);

	gen_system(x, y, z);
	if (p < 0 || p >= nearstar_nob) {
		out("; ERROR body index %d out of range (nob=%d)\n", p, nearstar_nob);
		return 2;
	}
	out("=== PLANET TEXTURE ===\n");
	gen_planet_surface(p);
	report_planet(p);
	return 0;
}

/* Flush every output stream so a fork()ed child never duplicates buffered text. */
static void flush_out(void)
{
	if (g_out)
		fflush(g_out);
	fflush(stdout);
}

static int cmd_planet_all(int argc, char **argv)
{
	double x = argd(argc, argv, "-x", 0);
	double y = argd(argc, argv, "-y", 0);
	double z = argd(argc, argv, "-z", 0);

	gen_system(x, y, z);
	out("=== PLANET TEXTURES (all %d bodies) ===\n", nearstar_nob);
	flush_out();
	int skipped = 0;
	for (int n = 0; n < nearstar_nob; n++) {
		flush_out();
		pid_t pid = fork();
		if (pid == 0) {
			/* Child: generate ONLY this body. A crash (e.g. segfault)
			   only kills this child; the parent skips and continues. */
			int is_moon = (nearstar_p_owner[n] > -1);
			gen_planet_surface(n);
			uint8_t *surf = is_moon ? s_background : p_background;
			nh_u32 surf_fnv = nh_fnv1a(surf, 46080UL);
			nh_u32 atmo_fnv = nh_fnv1a((uint8_t *)objectschart, 32400UL);
			int base3 = (is_moon ? 128 : 192) * 3;
			nh_u32 pal_fnv = nh_fnv1a((uint8_t *)(tmppal + base3), 192UL);
			out("PLANET %d type=%d is_moon=%d surf=%08lX atmo=%08lX pal=%08lX\n",
			    n, (int)nearstar_p_type[n], is_moon,
			    (unsigned long)surf_fnv, (unsigned long)atmo_fnv,
			    (unsigned long)pal_fnv);
			flush_out();
			_exit(0);
		} else if (pid > 0) {
			int status;
			waitpid(pid, &status, 0);
			if (WIFSIGNALED(status)) {
				out("; body %d crashed (signal %d), skipping\n", n, WTERMSIG(status));
				skipped++;
			} else if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
				out("; body %d exited with status %d, skipping\n", n, WEXITSTATUS(status));
				skipped++;
			}
		} else {
			out("; fork failed for body %d, skipping\n", n);
			skipped++;
		}
	}
	flush_out();
	(void)skipped;
	return 0;
}

/* ----------------------------------------------------------------------- */
/* TEST SETS 3 & 4 : surface sector heightmap + surface textures           */
/* ----------------------------------------------------------------------- */

static void setup_landing(int argc, char **argv, int n)
{
	int lon = (int)argl(argc, argv, "-lon", 0);
	int lat = (int)argl(argc, argv, "-lat", 60);
	long albedo_arg = argl(argc, argv, "-albedo", -1);
	int  sctype_arg = (int)argl(argc, argv, "-sc", -1);

	ip_targetted   = (char)n;
	landing_pt_lon = lon;
	landing_pt_lat = lat;
	landing_point  = 1;

	latitude = (float)(abs(landing_pt_lat - 60)) * 1.5;
	nightzone = (int)argl(argc, argv, "-night", 0);
	albedo = (albedo_arg >= 0) ? (int)albedo_arg : 32;

	dsd  = 1000.0;
	dsd1 = 1000.0;
	dsd2 = 1000.0;
	nray1 = nearstar_ray;
	nray2 = nearstar_ray;
	sun_x = -1000.0;
	sun_y = 0.0;
	sun_z = 0.0;

	global_surface_seed = (int32_t)((nearstar_p_ray[n]
	                    + nearstar_p_orb_ray[n]
	                    + nearstar_p_orb_orient[n]) * 4112.0);
	if (nearstar_p_type[n] == 3) {
		brtl_srand((uint16_t)(global_surface_seed + landing_pt_lon));
		if (latitude > 25 + (global_surface_seed % 15) + brtl_random(5))
			global_surface_seed++;
	}

	if (sctype_arg >= 1 && sctype_arg <= 4) {
		sctype = (char)sctype_arg;
	} else if (nearstar_p_type[n] == 3) {
		long cpos;
		brtl_srand((uint16_t)(landing_pt_lon * landing_pt_lat));
		if (brtl_random(100) > 5) {
			cpos = (long)(555 * nearstar_p_orb_orient[n]);
			sctype = (char)((cpos % 4) + 1);
		} else {
			sctype = (char)(brtl_random(4) + 1);
		}
		if (albedo < 25)
			sctype = SC_OCEAN;
		if (latitude > 75)
			sctype = SC_ICY;
		if (latitude > 60 && brtl_random(3))
			sctype = SC_ICY;
	} else {
		sctype = SC_PLAINS;
	}
}

/* Set the 16 gap bytes after p_surfacemap from -gap, or a reference default. */
static void set_gap(const char *gap_hex)
{
	uint8_t *gap = p_surfacemap + 40000;
	if (gap_hex && strlen(gap_hex) == 32) {
		for (int i = 0; i < 16; i++) {
			unsigned v = 0;
			for (int j = 0; j < 2; j++) {
				char c = gap_hex[2 * i + j];
				unsigned d = (c <= '9') ? (c - '0') : (c | 32) - 'a' + 10;
				v = (v << 4) | d;
			}
			gap[i] = (uint8_t)v;
		}
	} else {
		/* Reference default (measured from the C harness under DOSBox-X):
		   12 zero bytes + 0xC5 0x09 0xF0 0x54. */
		static const uint8_t def[16] = {0, 0, 0, 0, 0, 0, 0, 0,
		                                0, 0, 0, 0, 0xC5, 0x09, 0xF0, 0x54};
		for (int i = 0; i < 16; i++)
			gap[i] = def[i];
	}
}

static void report_sector_heightmap(int n)
{
	out("SECTOR body=%d type=%d lon=%d lat=%d sctype=%d albedo=%d night=%d\n",
	    n, (int)nearstar_p_type[n], landing_pt_lon, landing_pt_lat,
	    (int)sctype, albedo, nightzone);
	out("  global_surface_seed=%ld\n", (long)global_surface_seed);

	hashline("heightmap", p_surfacemap, 40000UL);
	hashline("objectchart", (uint8_t *)objectschart, 40000UL);

	dump_file("height.bin", p_surfacemap, 40000UL);
	dump_file("objects.bin", (uint8_t *)objectschart, 40000UL);
	{
		uint8_t *g = p_surfacemap + 40000;
		out("gap               len=16   ");
		for (int i = 0; i < 16; i++)
			out("%02X", g[i]);
		out("\n");
	}
}

static void report_surface_texture(int n)
{
	out("SURFTEX body=%d type=%d lon=%d lat=%d sctype=%d albedo=%d\n",
	    n, (int)nearstar_p_type[n], landing_pt_lon, landing_pt_lat,
	    (int)sctype, albedo);
	out("  T_SCALE=%ld quartz=%d groundflares=%d gtx=%d\n",
	    (long)T_SCALE, (int)quartz, (int)groundflares, (int)gtx);
	out("  rockscaling=%.3f rockpeaking=%.3f rockdensity=%d treescaling=%.3f\n",
	    rockscaling, rockpeaking, (int)rockdensity, treescaling);

	/* surf_texture: 254x256 = 65024 (txtr aliases p_background). */
	hashline("surf_texture", txtr, 65024UL);
	hashline("sky_texture", s_background, 46080UL); /* SAMPLED_BYTES */
	dump_file("surftex.bin", txtr, 65024UL);
	dump_file("sky.bin", s_background, 64800UL);
}

static int cmd_sector(int argc, char **argv)
{
	double x = argd(argc, argv, "-x", 0);
	double y = argd(argc, argv, "-y", 0);
	double z = argd(argc, argv, "-z", 0);
	int    p = (int)argl(argc, argv, "-p", 0);

	gen_system(x, y, z);
	if (p < 0 || p >= nearstar_nob) {
		out("; ERROR body index %d out of range (nob=%d)\n", p, nearstar_nob);
		return 2;
	}
	gen_planet_surface(p);
	setup_landing(argc, argv, p);
	set_gap(args(argc, argv, "-gap", (const char *)0));
	out("=== SURFACE SECTOR (heightmap) ===\n");
	build_surface();
	report_sector_heightmap(p);
	return 0;
}

static int cmd_surftex(int argc, char **argv)
{
	double x = argd(argc, argv, "-x", 0);
	double y = argd(argc, argv, "-y", 0);
	double z = argd(argc, argv, "-z", 0);
	int    p = (int)argl(argc, argv, "-p", 0);

	gen_system(x, y, z);
	if (p < 0 || p >= nearstar_nob) {
		out("; ERROR body index %d out of range (nob=%d)\n", p, nearstar_nob);
		return 2;
	}
	gen_planet_surface(p);
	setup_landing(argc, argv, p);
	out("=== SURFACE SECTOR (texture) ===\n");
	set_gap(args(argc, argv, "-gap", (const char *)0));
	build_surface();

	/* planetary_main per-type overrides (NOCTIS-1.CPP). */
	{
		int8_t sky_atmo = 1;
		switch (nearstar_p_type[p]) {
		case 1:
		case 4:
		case 7:
			sky_brightness = 0;
			sky_atmo = 0;
			break;
		case 2:
			sky_brightness = 63 - nightzone * 31;
			break;
		}
		memset(s_background, sky_brightness, st_bytes);
		create_sky(sky_atmo);
	}
	report_surface_texture(p);
	return 0;
}

/* ----------------------------------------------------------------------- */
/* selftest : hash vectors + RNG sequences                                 */
/* ----------------------------------------------------------------------- */

static int cmd_selftest(int argc, char **argv)
{
	static const char *v[] = {"", "a", "abc",
		"The quick brown fox jumps over the lazy dog"};
	(void)argc;
	(void)argv;

	out("=== SELFTEST ===\n");
	out("-- hash vectors --\n");
	for (int i = 0; i < 4; i++) {
		nh_u32 f = nh_fnv1a((const uint8_t *)v[i], strlen(v[i]));
		nh_u32 c = nh_crc32((const uint8_t *)v[i], strlen(v[i]));
		out("  \"%s\" fnv=%08lX crc=%08lX\n", v[i],
		    (unsigned long)f, (unsigned long)c);
	}

	out("-- Borland random() sequence, srand(1) --\n");
	brtl_srand(1);
	for (int k = 0; k < 8; k++)
		out("  rand=%d\n", brtl_rand());

	out("-- fast_random() sequence, fast_srand(12345) --\n");
	fast_srand(12345L);
	for (int k = 0; k < 8; k++)
		out("  fast=%ld\n", (long)fast_random(0x7FFFL));

	out("-- flandom()/fast_flandom() --\n");
	brtl_srand(1);
	out("  flandom=%.8f\n", (double)flandom());
	fast_srand(1L);
	out("  fast_flandom=%.8f\n", (double)fast_flandom());
	return 0;
}

/* ----------------------------------------------------------------------- */
/* main                                                                    */
/* ----------------------------------------------------------------------- */

static void usage(void)
{
	printf("nivtest - noctis-iv-lr generation output harness\n");
	printf("commands: selftest | system | planet | planet-all | sector |\n");
	printf("          surftex | scan | find\n");
	printf("see src/harness.cpp header for the full option list.\n");
}

int main(int argc, char **argv)
{
	const char *ofile;
	const char *cmd;
	const char *nivdump;
	int rc = 0;

	if (argc < 2) {
		usage();
		return 1;
	}
	cmd = argv[1];

	ofile = args(argc, argv, "-o", (const char *)0);
	if (ofile) {
		g_out = fopen(ofile, "a");
		if (!g_out) {
			printf("cannot open output %s\n", ofile);
			return 1;
		}
	}
	{
		const char *d = args(argc, argv, "-dump", (const char *)0);
		if (d)
			snprintf(g_dumpdir, sizeof(g_dumpdir), "%s", d);
	}
	/* Rust-style dump dir via NIVDUMP env var. */
	nivdump = getenv("NIVDUMP");
	if (nivdump && !g_dumpdir[0])
		snprintf(g_dumpdir, sizeof(g_dumpdir), "%s", nivdump);

	secs = (double)argl(argc, argv, "-secs", 0L);

	if (strcmp(cmd, "selftest") == 0) {
		rc = cmd_selftest(argc, argv);
	} else {
		if (!alloc_buffers()) {
			out("; FATAL not enough memory to allocate buffers\n");
			if (g_out)
				fclose(g_out);
			return 3;
		}
		if      (strcmp(cmd, "system")     == 0) rc = cmd_system(argc, argv);
		else if (strcmp(cmd, "scan")       == 0) rc = cmd_scan(argc, argv);
		else if (strcmp(cmd, "find")       == 0) rc = cmd_find(argc, argv);
		else if (strcmp(cmd, "planet")     == 0) rc = cmd_planet(argc, argv);
		else if (strcmp(cmd, "planet-all") == 0) rc = cmd_planet_all(argc, argv);
		else if (strcmp(cmd, "sector")     == 0) rc = cmd_sector(argc, argv);
		else if (strcmp(cmd, "surftex")    == 0) rc = cmd_surftex(argc, argv);
		else {
			usage();
			rc = 1;
		}
	}

	if (g_out)
		fclose(g_out);
	return rc;
}
