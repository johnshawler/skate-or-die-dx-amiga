/* Headless test harness for KEGS 1.38 (added to the source tree by build.sh, see kegs-harness.patch for the hooks).
 * Controlled by environment variables (all optional; the harness is off unless HX_OUT is set):
 *   HX_OUT=dir           output directory: frames.csv (one line per 60 Hz frame), shr_NNNNNN.bin (E1:$2000-$9FFF), watch.csv
 *   HX_SCRIPT=file       input timeline, lines:  J <frame> <jx> <jy> <b0> <b1>   joystick from that frame on (jx,jy in -1,0,1)
 *                                                K <frame> <a2code hex> <1=down|0=up>   ADB keyboard code event
 *                        S <frame>               start a block of polled segments at the first poll at/after that frame
 *                        H <jx> <jy> <b0> <b1> <n>   hold that input for n joystick polls (= game passes), segments run in order
 *   HX_PAD=lo,mid,hi     paddle position 0-255 for joystick -1/0/+1 (default 0,128,255)
 *   HX_FRAMES=n          exit after n frames (emulated time = n/60.0 s; the IIGS frame is 17030 1 MHz cycles)
 *   HX_SHOT_EVERY=k      dump the SHR memory every k frames (0 = never);  HX_SHOT_FROM=f first frame to dump (default 0)
 *   HX_WATCH=b:addr:len,...   bytes (hex bank, hex address, decimal length) to log per frame in watch.csv
 * Emulated time is what is logged; the host runs as fast as it can (no sleeping while the harness is on). */
#include "defc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern byte *g_memory_ptr, *g_slow_memory_ptr;
extern word32 g_vbl_count;
extern dword64 g_last_vbl_dfcyc;
extern int g_paddle_buttons, g_paddle_val[4];
extern Kimage g_mainwin_kimage;
extern int g_cur_a2_stat; extern word32 g_c029_val_some;
void paddle_update_trigger_dcycs(dword64 dfcyc);
void adb_physical_key_update(Kimage *kimage_ptr, int raw_a2code, word32 unicode_c, int is_up);

int hx_active = 0;
unsigned hx_pad_triggers = 0, hx_doc_writes = 0, hx_key_events = 0, hx_c029 = 0;
static int hx_jx, hx_jy, hx_b0, hx_b1;
static int hx_pad_idx[3] = { 0, 128, 255 };    /* paddle position (0-255) sent for joystick -1, 0, +1; HX_PAD="lo,mid,hi" */
typedef struct { int frame, type, a, b, c, d; } HxEv;
static HxEv *hx_ev; static int hx_nev, hx_next;
/* polled sequences: "S frame" starts a block at the first joystick poll at or after that frame; "H jx jy b0 b1 n" holds that input for n polls */
typedef struct { int jx, jy, b0, b1, n; } HxSeg;
static HxSeg hx_seg[20000]; static int hx_nseg;
static int hx_blk_frame[256], hx_blk_first[256], hx_nblk, hx_cur_blk = -1, hx_cur_seg = -1, hx_seg_left;
static unsigned hx_poll_total;
static word32 hx_pc[4]; static int hx_npc; static unsigned hx_pc_hits[4];
void set_bp(word32 addr, word32 end_addr, word32 acc_type);
int hx_is_pc(word32 a) { int i; for(i = 0; i < hx_npc; i++) if(hx_pc[i] == (a & 0xffffff)) return 1; return 0; }
void hx_pc_hit(word32 a) { int i; for(i = 0; i < hx_npc; i++) if(hx_pc[i] == (a & 0xffffff)) hx_pc_hits[i]++; }
static FILE *hx_csv, *hx_wcsv;
static char hx_dir[512];
static long hx_frames, hx_shot_every, hx_shot_from;
static struct { int bank, addr, len; } hx_w[32]; static int hx_nw;
static byte hx_prev[0x8000];
static word32 hx_hash_prev;

static byte *hx_mem(int bank, int addr)
{
	if(bank == 0xe0) return &g_slow_memory_ptr[addr];
	if(bank == 0xe1) return &g_slow_memory_ptr[0x10000 + addr];
	return &g_memory_ptr[(bank << 16) + addr];
}

void hx_init(void)
{
	const char *o = getenv("HX_OUT"), *s = getenv("HX_SCRIPT"), *w = getenv("HX_WATCH"), *t;
	char path[600], line[256];
	if(!o) return;
	hx_active = 1;
	if((t = getenv("HX_PAD"))) sscanf(t, "%d,%d,%d", &hx_pad_idx[0], &hx_pad_idx[1], &hx_pad_idx[2]);
	snprintf(hx_dir, sizeof(hx_dir), "%s", o);
	hx_frames = (t = getenv("HX_FRAMES")) ? atol(t) : 0;
	hx_shot_every = (t = getenv("HX_SHOT_EVERY")) ? atol(t) : 0;
	hx_shot_from = (t = getenv("HX_SHOT_FROM")) ? atol(t) : 0;
	snprintf(path, sizeof(path), "%s/frames.csv", o);
	hx_csv = fopen(path, "w");
	fprintf(hx_csv, "frame,vbl_cycles_1mhz,pad_polls,key_events,doc_writes,shr_changed_bytes,shr_hash,c029,poll_total,pc0,pc1\n");
	if(s) {
		FILE *f = fopen(s, "r"); int n = 0;
		hx_ev = (HxEv *)calloc(20000, sizeof(HxEv));
		while(f && fgets(line, sizeof(line), f)) {
			HxEv e; char ty; int a, b, c, d, frame;
			memset(&e, 0, sizeof(e));
			if(sscanf(line, " J %d %d %d %d %d", &frame, &a, &b, &c, &d) == 5) { e.frame = frame; e.type = 'J'; e.a = a; e.b = b; e.c = c; e.d = d; }
			else if(sscanf(line, " S %d", &frame) == 1) { if(hx_nblk < 256) { hx_blk_frame[hx_nblk] = frame; hx_blk_first[hx_nblk++] = hx_nseg; } continue; }
			else if(sscanf(line, " H %d %d %d %d %d", &a, &b, &c, &d, &frame) == 5) { if(hx_nseg < 20000) { HxSeg *g = &hx_seg[hx_nseg++]; g->jx = a; g->jy = b; g->b0 = c; g->b1 = d; g->n = frame; } continue; }
			else if(sscanf(line, " %c %d %x %d", &ty, &frame, &a, &b) == 4 && ty == 'K') { e.frame = frame; e.type = 'K'; e.a = a; e.b = b; }
			else continue;
			if(n < 20000) hx_ev[n++] = e;
		}
		hx_nev = n; if(f) fclose(f);
	}
	if((t = getenv("HX_PCHOOK"))) {          /* HX_PCHOOK=031082,... : count executions of these 65816 addresses (the games' main-loop pass hooks) */
		char *p = strdup(t), *tok = strtok(p, ",");
		while(tok && hx_npc < 4) { hx_pc[hx_npc] = (word32)strtoul(tok, NULL, 16); set_bp(hx_pc[hx_npc], hx_pc[hx_npc], 4); hx_npc++; tok = strtok(NULL, ","); }
	}
	if(w) {
		char *p = strdup(w), *tok = strtok(p, ",");
		while(tok && hx_nw < 32) { sscanf(tok, "%x:%x:%d", &hx_w[hx_nw].bank, &hx_w[hx_nw].addr, &hx_w[hx_nw].len); hx_nw++; tok = strtok(NULL, ","); }
		snprintf(path, sizeof(path), "%s/watch.csv", o);
		hx_wcsv = fopen(path, "w");
	}
}

/* called before each frame is run */
void hx_frame_begin(void)
{
	while(hx_next < hx_nev && hx_ev[hx_next].frame <= (int)g_vbl_count) {
		HxEv *e = &hx_ev[hx_next++];
		if(e->type == 'J') { hx_jx = e->a; hx_jy = e->b; hx_b0 = e->c; hx_b1 = e->d; }
		else { adb_physical_key_update(&g_mainwin_kimage, e->a, 0, !e->b); hx_key_events++; }
	}
	g_paddle_buttons = 0xc | (hx_b0 ? 1 : 0) | (hx_b1 ? 2 : 0);
}

/* called from paddle_trigger (a read/write of $C070): the game polls the joystick */
void hx_paddle_trigger(dword64 dfcyc)
{
	hx_pad_triggers++; hx_poll_total++;
	/* polled sequences: start the next block when its frame has come, then step through its segments one poll at a time */
	if(hx_cur_blk + 1 < hx_nblk && (int)g_vbl_count >= hx_blk_frame[hx_cur_blk + 1]) { hx_cur_blk++; hx_cur_seg = hx_blk_first[hx_cur_blk] - 1; hx_seg_left = 0; }
	if(hx_cur_blk >= 0) {
		int end = (hx_cur_blk + 1 < hx_nblk) ? hx_blk_first[hx_cur_blk + 1] : hx_nseg;
		if(hx_seg_left <= 0 && hx_cur_seg + 1 < end) { HxSeg *g = &hx_seg[++hx_cur_seg]; hx_jx = g->jx; hx_jy = g->jy; hx_b0 = g->b0; hx_b1 = g->b1; hx_seg_left = g->n; }
		if(hx_seg_left > 0) hx_seg_left--;
	}
	g_paddle_val[0] = (hx_pad_idx[hx_jx + 1] - 128) * 256;
	g_paddle_val[1] = (hx_pad_idx[hx_jy + 1] - 128) * 256;
	g_paddle_val[2] = 32767;
	g_paddle_val[3] = 32767;
	g_paddle_buttons = 0xc | (hx_b0 ? 1 : 0) | (hx_b1 ? 2 : 0);
	paddle_update_trigger_dcycs(dfcyc);
}

/* called after each frame has run */
void hx_frame_end(void)
{
	byte *scr = hx_mem(0xe1, 0x2000);
	word32 h = 2166136261u; int i, changed = 0;
	char path[600];
	for(i = 0; i < 0x8000; i++) { h = (h ^ scr[i]) * 16777619u; if(scr[i] != hx_prev[i]) changed++; }
	memcpy(hx_prev, scr, 0x8000);
	fprintf(hx_csv, "%u,%llu,%u,%u,%u,%d,%08x,%02x,%u,%u,%u\n", g_vbl_count, (unsigned long long)(g_last_vbl_dfcyc >> 16), hx_pad_triggers, hx_key_events, hx_doc_writes, changed, h, (unsigned)(((g_cur_a2_stat & 0xa0) | g_c029_val_some) & 0xff), hx_poll_total, hx_pc_hits[0], hx_pc_hits[1]);
	hx_pc_hits[0] = hx_pc_hits[1] = 0; hx_pad_triggers = 0; hx_doc_writes = 0; hx_key_events = 0;
	if(hx_shot_every && (long)g_vbl_count >= hx_shot_from && (g_vbl_count % hx_shot_every) == 0) {
		FILE *f; snprintf(path, sizeof(path), "%s/shr_%06u.bin", hx_dir, g_vbl_count);
		f = fopen(path, "wb"); if(f) { fwrite(scr, 1, 0x8000, f); fclose(f); }
	}
	if(hx_wcsv && hx_nw) {
		int k, j; fprintf(hx_wcsv, "%u", g_vbl_count);
		for(k = 0; k < hx_nw; k++) { fprintf(hx_wcsv, ","); for(j = 0; j < hx_w[k].len; j++) fprintf(hx_wcsv, "%02x", hx_mem(hx_w[k].bank, hx_w[k].addr)[j]); }
		fprintf(hx_wcsv, "\n");
	}
	if(hx_frames && (long)g_vbl_count >= hx_frames) { fclose(hx_csv); if(hx_wcsv) fclose(hx_wcsv); fflush(stdout); exit(0); }
}
