/* Entry point: load the disc, set up the HLE OS and run the recompiled game. */
#include "recomp.h"
#include "runtime.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "game_config.h"
#include "find_image.inc"

int log_level = 1;
static FILE *logfp;
extern int g_headless, g_dump_frames, g_max_frames;

void logf_(int lvl, const char *fmt, ...) {
    if (lvl > 0 && g_spec) return;   /* run-ahead fields would repeat every message */
    va_list ap;
    char buf[1024];
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    fprintf(stderr, "%s\n", buf);
    if (logfp) { fprintf(logfp, "%s\n", buf); fflush(logfp); }
}

void fatal(const char *fmt, ...) {
    va_list ap;
    char buf[1024];
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    logf_(0, "FATAL: %s", buf);
    if (RAM) {   /* the machine exists: its state, for debugging */
        logf_(0, "  pc=%06x d=%08x %08x %08x %08x %08x %08x %08x %08x", m68k.pc,
              D(0), D(1), D(2), D(3), D(4), D(5), D(6), D(7));
        logf_(0, "  a=%08x %08x %08x %08x %08x %08x %08x %08x",
              A(0), A(1), A(2), A(3), A(4), A(5), A(6), A(7));
        /* crude 68k stack backtrace: print code addresses found on the stack */
        for (int i = 0; i < 64; i++) {
            uint32_t v = RL(A(7) + i * 4);
            if (v >= recomp_code_lo && v < recomp_code_hi)
                logf_(0, "  stack+%03x: %06x (%s)", i * 4, v, func_name(v));
        }
        char rp[600];
        snprintf(rp, sizeof rp, "%s/ramdump.bin", g_data_dir);
        FILE *rf = fopen(rp, "wb");
        if (rf) { fwrite(RAM, 1, MEM_SIZE, rf); fclose(rf); }
    }
#ifdef _WIN32
    if (!g_headless) {
        MessageBoxA(NULL, buf, "Recomp fatal error", MB_ICONERROR);
    }
#endif
    exit(1);
}

static int file_exists(const char *p) {
    FILE *f = fopen(p, "rb");
    if (f) { fclose(f); return 1; }
    return 0;
}

#ifdef _WIN32
/* a host crash: report where, with the 68k state, instead of vanishing */
static LONG WINAPI crash_handler(EXCEPTION_POINTERS *ep) {
    g_spec = 0;
    fatal("host exception %08lx at %p (address %p)", ep->ExceptionRecord->ExceptionCode,
          ep->ExceptionRecord->ExceptionAddress,
          ep->ExceptionRecord->NumberParameters > 1 ? (void *)ep->ExceptionRecord->ExceptionInformation[1] : NULL);
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

int main(int argc, char **argv) {
#ifdef _WIN32
    SetUnhandledExceptionFilter(crash_handler);
#endif
    const char *img_arg = NULL;
    int no_update = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--headless")) g_headless = 1;
        else if (!strcmp(argv[i], "--no-update")) no_update = 1;
        else if (!strcmp(argv[i], "--portable")) {}     /* always portable: accepted for shortcuts */
        else if (!strcmp(argv[i], "--trace")) log_level = 2;
        else if (!strcmp(argv[i], "--trace3")) log_level = 3;
        else if (!strcmp(argv[i], "--dump") && i + 1 < argc) g_dump_frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) g_max_frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--aspect") || !strcmp(argv[i], "--filter") || !strcmp(argv[i], "--size")) {
            extern void host_video_option(const char *, const char *);
            if (i + 1 < argc) { host_video_option(argv[i], argv[i + 1]); i++; }
        }
        else if (!strcmp(argv[i], "--fullscreen") || !strcmp(argv[i], "--windowed") || !strcmp(argv[i], "--gdi")) {
            extern void host_video_option(const char *, const char *);
            host_video_option(argv[i], NULL);
        }
        else if (!strcmp(argv[i], "--interp")) { extern int g_interp_all; g_interp_all = 1; }
        else if (!strcmp(argv[i], "--fuzz") && i + 1 < argc) { extern int g_fuzz; g_fuzz = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--wav") && i + 1 < argc) { extern void host_wav_open(const char *); host_wav_open(argv[++i]); }
        else if (!strcmp(argv[i], "--script") && i + 1 < argc) { extern void host_set_script(const char *); host_set_script(argv[++i]); }
        else if (!strcmp(argv[i], "--invincible")) g_invincible = 1;
        else if (i + 1 < argc && frontend_option(argv[i], argv[i + 1])) i++;
        else img_arg = argv[i];
    }
    init_exe_dir();
    setup_data_dir();
    {
        char lp[600];
        snprintf(lp, sizeof lp, "%s/recomp.log", g_data_dir);
        logfp = fopen(lp, "w");
    }
    LOG("%s %s", GAME_TITLE, APP_VERSION);
    if (!g_headless && !no_update) {
        char v[16] = "1";
        ini_get("update_check", v, sizeof v);
        if (v[0] != '0') {
            extern void update_check_start(const char *exe_dir, int automatic);
            update_check_start(exe_dir, !_stricmp(v, "auto"));
        }
    }
    char img[1024];
    if (find_image(img_arg, img, sizeof img) && (g_headless || img_arg || pick_image(img, sizeof img))) {
        fatal("Could not find the " GAME_TITLE " disc image.\n\n"
              "Put the .bin/.cue in a 'rom' folder next to the program, or pass the path to the .cue or .bin as an argument.");
    }
    LOG("disc image: %s", img);
    if (disc_open(img)) fatal("failed to open disc image %s", img);

    RAM = (uint8_t *)calloc(MEM_SIZE + 16, 1);
    cpu_init();
    os9_init();
    host_init();
    video_init();
    audio_init();
    cdfm_init();
    sched_init();

    const DiscFile *df = disc_find(MAIN_MODULE);
    if (!df) fatal("program module '%s' not found on the disc (is this the right game?)", MAIN_MODULE);
    uint8_t *mod; uint32_t len;
    disc_read_file(df, &mod, &len);
    os9_load_main_module(mod, len);
    free(mod);
    sched_init();

    frontend_run();
    return 0;
}
