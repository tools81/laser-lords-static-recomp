/* Runtime-internal interfaces (HLE of CD-RTOS + host platform). */
#pragma once
#include <stdint.h>
#include <stdio.h>

/* ---- logging */
extern int log_level;
void logf_(int lvl, const char *fmt, ...);
#define LOG(...)   logf_(1, __VA_ARGS__)
#define TRACE(...) do { if (log_level >= 2) logf_(2, __VA_ARGS__); } while (0)
void fatal(const char *fmt, ...);
const char *func_name(uint32_t pc);

/* ---- cpu */
void cpu_init(void);
void cpu_run(void);
void cpu_run_until(volatile int *flag);

/* ---- timing / scheduler */
/* Emulated instructions per second.  A CD-i's 68070 is slow (slow bus,
   video sharing the RAM): Laser Lords' player animation is limited by the
   CPU, and this rate matches its walking and climbing speed on hardware.
   Override with /DCPU_IPS=n. */
#ifndef CPU_IPS
#define CPU_IPS        355000
#endif
#define FIELD_RATE     60           /* NTSC */
#define SECTOR_RATE    75           /* 1x CD */
void sched_init(void);
void sched_service(void);           /* called when m68k.cyc >= cyc_limit */
int64_t sched_now(void);

/* ---- memory allocator (OS-9 F$SRqMem) */
#define HEAP_BASE 0x00140000u
#define HEAP_END  0x00F00000u
uint32_t mem_alloc(uint32_t size, uint32_t *actual);
void mem_free(uint32_t addr, uint32_t size);

/* ---- OS-9 process / signals */
void os9_init(void);
void os9_load_main_module(const uint8_t *mod, uint32_t len);
void sig_send(uint32_t sig);        /* queue a signal for the current process */
void sig_send_pid(int pid, uint32_t sig);
int  os9_cur_pid(void);
int  sig_deliver(void);             /* deliver a pending signal if possible: returns 1 if pc changed */
void os9_video_field_event(void);   /* the video driver's "line_event" */

/* OS-9 error codes */
#define E_ILLFNC 0xD2   /* hmm placeholder */
#define E_UNKSVC 0xD0
#define E_BPNUM  0xC9
#define E_PNNF   0xD8
#define E_EOF    0xD3
#define E_NORAM  0xCF  /* E$MemFul is 0xCF? */
#define E_ILLPRM 0xBB   /* placeholder */
#define E_BMODE  0xCB
#define E_UNID   0xBC   /* placeholder */

/* ---- disc */
typedef struct {
    char name[32];
    uint32_t lsn;
    uint32_t size;
    uint8_t  fnum;
    uint16_t attr;
} DiscFile;

int  disc_open(const char *path);
int  disc_num_files(void);
const DiscFile *disc_find(const char *name);
const DiscFile *disc_file(int i);
void disc_read_raw(uint32_t lsn, uint8_t *buf2352);
int  disc_read_file(const DiscFile *f, uint8_t **out, uint32_t *len);

/* ---- CDFM */
void cdfm_init(void);
void cdfm_sector_tick(void);        /* one 1x sector period elapsed */
int  cdfm_open(const char *name, int mode);   /* returns handle or -1 */
void cdfm_close(int h);
int  cdfm_read(int h, uint32_t buf, uint32_t n, uint32_t *got);
int  cdfm_seek(int h, uint32_t pos);
uint32_t cdfm_getpos(int h);
uint32_t cdfm_size(int h);
int  cdfm_setstat(int h, uint32_t code);      /* uses m68k regs; returns error or 0 */
int  cdfm_getstat(int h, uint32_t code);
int  cdfm_busy(void);

/* ---- audio */
void audio_init(void);
int  audio_setstat(uint32_t code);          /* SS_SM / SS_SC / SS_SD on the audio path */
int  audio_getstat(uint32_t code);
void audio_feed_sector(const uint8_t *sector2352);   /* ADPCM sector to the audio processor */
void audio_field_tick(void);
void audio_shutdown(void);

/* ---- video (UCM) */
void video_init(void);
int  ucm_setstat(uint32_t code);
int  ucm_getstat(uint32_t code);
void video_field(void);             /* run display program for one field, render */
extern uint32_t *video_fb;          /* 32bpp host framebuffer */
extern int video_w, video_h;
extern int g_cursor_external, g_render_skip;
typedef struct {
    int show, x, y, xs, ys, h;      /* position of the pattern in 768x480 units */
    uint32_t color;
    uint16_t ptn[32];
} VideoCursor;
extern VideoCursor g_cursor;        /* the cursor as shown in the last field */
void video_cursor_draw(uint32_t *buf768x240, const VideoCursor *c);

/* ---- input */
typedef struct {
    int dx, dy;         /* relative motion this field (thumbpad) */
    int b1, b2, b3;
    int x, y;           /* absolute pointer position */
    int quit;
} InputState;
extern InputState input;
int  ptr_setstat(uint32_t code);
int  ptr_getstat(uint32_t code);

/* ---- host platform */
int  host_init(void);
void host_present(const uint32_t *fb, int w, int h);
void host_poll(void);
double host_time(void);
void host_sleep_until(double t);
void host_audio_init(int rate);
void host_audio_push(const int16_t *stereo, int frames);
int  host_audio_queued(void);
enum { HOTKEY_SAVE = 1, HOTKEY_SLOT, HOTKEY_LOAD, HOTKEY_INVINCIBLE, HOTKEY_RUNAHEAD, HOTKEY_INTERP, HOTKEY_UPSCALER };
int  host_take_hotkey(void);        /* 0 when none */

/* ---- front end (frontend.c): frame loop, presentation, save states */
void frontend_run(void);
void frontend_field_done(void);     /* a field was rendered and its audio mixed */
int  frontend_option(const char *opt, const char *val);
extern int g_edition_hd;            /* edition_orig.c / edition_hd.c */
extern int g_spec;                  /* speculative (run-ahead) emulation: no output */

/* ---- cheats (cheats.c) */
extern int g_invincible;            /* the player takes no damage */

/* paths */
extern char g_data_dir[512];
