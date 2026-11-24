/* retroshot.c - minimal headless libretro frontend (CC0, by Claude for Amy Studio).
 * Loads a libretro core, loads a ROM, runs N frames with no input, and writes the
 * last frame as a binary PPM (P6).
 * usage: retroshot core.so rom frames out.ppm systemdir [key=value ...]
 * key=value pairs answer RETRO_ENVIRONMENT_GET_VARIABLE (core options). */
#include <dlfcn.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libretro.h"

static const char *sysdir;
static char *keys[32], *vals[32]; static int nvars;
static enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_0RGB1555;
static uint8_t *frame; static unsigned fw, fh; static size_t fpitch;

static void logcb(enum retro_log_level l, const char *f, ...) { (void)l; va_list a; va_start(a, f); vfprintf(stderr, f, a); va_end(a); }
static bool env(unsigned cmd, void *data) {
    if (getenv("TRACE")) fprintf(stderr, "env %u%s%s\n", cmd, cmd == RETRO_ENVIRONMENT_GET_VARIABLE ? " " : "", cmd == RETRO_ENVIRONMENT_GET_VARIABLE ? ((struct retro_variable *)data)->key : "");
    switch (cmd) {
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        *(const char **)data = sysdir; return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: fmt = *(enum retro_pixel_format *)data; return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable *v = data;
        for (int i = 0; i < nvars; i++) if (!strcmp(v->key, keys[i])) { v->value = vals[i]; return true; }
        v->value = NULL; return false; }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: *(bool *)data = false; return true;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: ((struct retro_log_callback *)data)->log = logcb; return true;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE: *(bool *)data = true; return true;
    default: return false;
    }
}
static void video(const void *d, unsigned w, unsigned h, size_t pitch) {
    if (!d) return;
    free(frame); frame = malloc(pitch * h); memcpy(frame, d, pitch * h); fw = w; fh = h; fpitch = pitch;
}
static void audio(int16_t l, int16_t r) { (void)l; (void)r; }
static size_t audiob(const int16_t *d, size_t n) { (void)d; return n; }
static void poll(void) {}
static int16_t input(unsigned p, unsigned dv, unsigned i, unsigned id) { (void)p; (void)dv; (void)i; (void)id; return 0; }

int main(int argc, char **argv) {
    if (argc < 6) { fprintf(stderr, "usage: retroshot core.so rom frames out.ppm systemdir [key=value ...]\n"); return 2; }
    sysdir = argv[5];
    for (int i = 6; i < argc && nvars < 32; i++) { char *e = strchr(argv[i], '='); if (!e) continue; *e = 0; keys[nvars] = argv[i]; vals[nvars++] = e + 1; }
    void *h = dlopen(argv[1], RTLD_NOW); if (!h) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    void (*set_env)(retro_environment_t) = dlsym(h, "retro_set_environment");
    void (*set_video)(retro_video_refresh_t) = dlsym(h, "retro_set_video_refresh");
    void (*set_audio)(retro_audio_sample_t) = dlsym(h, "retro_set_audio_sample");
    void (*set_audiob)(retro_audio_sample_batch_t) = dlsym(h, "retro_set_audio_sample_batch");
    void (*set_poll)(retro_input_poll_t) = dlsym(h, "retro_set_input_poll");
    void (*set_input)(retro_input_state_t) = dlsym(h, "retro_set_input_state");
    void (*init)(void) = dlsym(h, "retro_init");
    bool (*load)(const struct retro_game_info *) = dlsym(h, "retro_load_game");
    void (*run)(void) = dlsym(h, "retro_run");
    void (*sysinfo)(struct retro_system_info *) = dlsym(h, "retro_get_system_info");
    struct retro_system_info si = { 0 }; sysinfo(&si);
    set_env(env); set_video(video); set_audio(audio); set_audiob(audiob); set_poll(poll); set_input(input);
    init();
    FILE *f = fopen(argv[2], "rb"); if (!f) { perror(argv[2]); return 1; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    void *rom = malloc(n); if (fread(rom, 1, n, f) != (size_t)n) return 1; fclose(f);
    struct retro_game_info gi = { argv[2], si.need_fullpath ? NULL : rom, si.need_fullpath ? 0 : (size_t)n, NULL };
    if (!load(&gi)) { fprintf(stderr, "load failed\n"); return 1; }
    int frames = atoi(argv[3]);
    for (int i = 0; i < frames; i++) run();
    if (!frame) { fprintf(stderr, "no frame\n"); return 1; }
    FILE *o = fopen(argv[4], "wb"); fprintf(o, "P6\n%u %u\n255\n", fw, fh);
    for (unsigned y = 0; y < fh; y++) for (unsigned x = 0; x < fw; x++) {
        uint8_t px[3];
        if (fmt == RETRO_PIXEL_FORMAT_XRGB8888) { uint32_t p = ((uint32_t *)(frame + y * fpitch))[x]; px[0] = p >> 16; px[1] = p >> 8; px[2] = p; }
        else if (fmt == RETRO_PIXEL_FORMAT_RGB565) { uint16_t p = ((uint16_t *)(frame + y * fpitch))[x]; px[0] = (p >> 11) << 3; px[1] = ((p >> 5) & 63) << 2; px[2] = (p & 31) << 3; }
        else { uint16_t p = ((uint16_t *)(frame + y * fpitch))[x]; px[0] = ((p >> 10) & 31) << 3; px[1] = ((p >> 5) & 31) << 3; px[2] = (p & 31) << 3; }
        fwrite(px, 1, 3, o);
    }
    fclose(o);
    printf("%s: %d frames, %ux%u, core %s %s\n", argv[4], frames, fw, fh, si.library_name, si.library_version);
    return 0;
}
