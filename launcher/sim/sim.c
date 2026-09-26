/* Glue for the simulator build (NumPlay.nwb), which runs natively inside the
 * Epsilon simulator.
 *
 * - Rust (Tetris) calls eadk_keyboard_scan and eadk_timing_millis as real
 *   functions; the simulator's eadk.h only has them inline.
 * - Tetris's storage helpers get an in-memory stand-in.
 * - Recording: with NUMPLAY_SCRIPT set (keys over time, "500-600:right,900:ok"),
 *   the keyboard follows the script instead of the user, and with
 *   NUMPLAY_FRAMES set to a directory, the screen is saved there as raw
 *   RGB565 frames (NUMPLAY_FPS per second). The app quits when the script ends
 *   (NUMPLAY_END_MS). tools/record.py turns the frames into PNGs and GIFs. */
#include <dlfcn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  uint16_t x, y, width, height;
} rect_t;
void eadk_display_pull_rect(rect_t rect, uint16_t *pixels);
void eadk_timing_msleep(uint32_t ms);

/* ---------------------------------------------------------------- recording */
typedef struct {
  uint32_t a, b;
  int key;
} press_t;
static press_t presses[512];
static int npresses = -1;
static uint32_t t0, end_ms, frame_ms = 40, next_frame, nframes;
static const char *frames_dir;

static const struct {
  const char *name;
  int key;
} names[] = {{"left", 0},   {"up", 1},      {"down", 2},      {"right", 3}, {"ok", 4},   {"back", 5},
             {"home", 6},   {"shift", 12},  {"alpha", 13},    {"backspace", 17}, {"exe", 52}, {"0", 48},
             {"1", 42},     {"2", 43},      {"3", 44},        {"4", 36},    {"5", 37},   {"6", 38},
             {"7", 30},     {"8", 31},      {"9", 32},        {"plus", 45}, {"minus", 46}, {"toolbox", 16},
             {"var", 15},   {"xnt", 14},    {"ln", 19}};

static void (*real_do_scan)(void);
static uint32_t (*real_scan_low)(void), (*real_scan_high)(void);
static uint32_t (*real_millis_low)(void), (*real_millis_high)(void);
static uint16_t (*real_event_get)(int32_t *);

static uint32_t now_real(void) {
  if (!real_millis_low) {
    real_millis_low = dlsym(RTLD_MAIN_ONLY, "_eadk_timing_millis_low");
    real_millis_high = dlsym(RTLD_MAIN_ONLY, "_eadk_timing_millis_high");
  }
  return real_millis_low ? real_millis_low() : 0;
}

static void load(void) {
  if (npresses >= 0) return;
  npresses = 0;
  real_do_scan = dlsym(RTLD_MAIN_ONLY, "_eadk_keyboard_scan_do_scan");
  real_scan_low = dlsym(RTLD_MAIN_ONLY, "_eadk_keyboard_scan_low");
  real_scan_high = dlsym(RTLD_MAIN_ONLY, "_eadk_keyboard_scan_high");
  real_event_get = dlsym(RTLD_MAIN_ONLY, "eadk_event_get");
  const char *s = getenv("NUMPLAY_SCRIPT");
  frames_dir = getenv("NUMPLAY_FRAMES");
  if (getenv("NUMPLAY_FPS")) frame_ms = 1000 / (uint32_t)atoi(getenv("NUMPLAY_FPS"));
  if (getenv("NUMPLAY_END_MS")) end_ms = (uint32_t)atoi(getenv("NUMPLAY_END_MS"));
  t0 = now_real();
  if (!s) {
    npresses = -2;  /* not recording */
    return;
  }
  char buf[8192];
  strncpy(buf, s, sizeof buf - 1);
  buf[sizeof buf - 1] = 0;
  for (char *item = strtok(buf, ","); item && npresses < 512; item = strtok(NULL, ",")) {
    char *colon = strchr(item, ':');
    if (!colon) continue;
    *colon = 0;
    uint32_t a = (uint32_t)atoi(item), b = a + 120;
    char *dash = strchr(item, '-');
    if (dash) b = (uint32_t)atoi(dash + 1);
    int key = -1;
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
      if (!strcmp(colon + 1, names[i].name)) key = names[i].key;
    if (key < 0) key = atoi(colon + 1);
    presses[npresses++] = (press_t){a, b, key};
    if (b + 1500 > end_ms) end_ms = b + 1500;
  }
}

static bool recording(void) {
  load();
  return npresses >= 0;
}

static void grab(uint32_t t) {
  if (!frames_dir || t < next_frame) return;
  static uint16_t px[320 * 240];
  eadk_display_pull_rect((rect_t){0, 0, 320, 240}, px);
  char path[1024];
  snprintf(path, sizeof path, "%s/%06u_%06u.raw", frames_dir, nframes++, t);
  FILE *f = fopen(path, "wb");
  if (f) {
    fwrite(px, sizeof px, 1, f);
    fclose(f);
  }
  next_frame = t + frame_ms;
}

static uint64_t scripted_state(uint32_t t) {
  uint64_t s = 0;
  for (int i = 0; i < npresses; i++)
    if (t >= presses[i].a && t < presses[i].b) s |= (uint64_t)1 << presses[i].key;
  return s;
}

static uint64_t state;
void _eadk_keyboard_scan_do_scan(void) {
  if (!recording()) {
    real_do_scan();
    state = (uint64_t)real_scan_high() << 32 | real_scan_low();
    return;
  }
  uint32_t t = now_real() - t0;
  grab(t);
  if (end_ms && t > end_ms) {
    fflush(stdout);
    exit(0);
  }
  state = scripted_state(t);
}
uint32_t _eadk_keyboard_scan_low(void) { return (uint32_t)state; }
uint32_t _eadk_keyboard_scan_high(void) { return (uint32_t)(state >> 32); }

/* Games that wait for events (Chess) get the scripted presses as events. */
uint16_t eadk_event_get(int32_t *timeout) {
  if (!recording()) return real_event_get(timeout);
  static uint64_t previous;
  for (;;) {
    uint32_t t = now_real() - t0;
    grab(t);
    if (end_ms && t > end_ms) exit(0);
    uint64_t s = scripted_state(t), pressed = s & ~previous;
    previous = s;
    for (int k = 0; k < 64; k++)
      if (pressed >> k & 1) return (uint16_t)k;
    if (timeout && *timeout <= 0) return 216;  /* Ion::Events::None */
    eadk_timing_msleep(10);
    if (timeout) *timeout -= 10;
  }
}

/* ---------------------------------------------------------------- for Rust */
uint64_t eadk_keyboard_scan(void) {
  _eadk_keyboard_scan_do_scan();
  return state;
}
uint64_t eadk_timing_millis(void) {
  now_real();
  return real_millis_high ? ((uint64_t)real_millis_high() << 32 | real_millis_low()) : 0;
}

/* ---------------------------------------------------------------- Tetris's files */
static struct {
  char name[32];
  uint8_t data[256];
  uint32_t len;
  bool used;
} files[8];

static int find_file(const char *name) {
  for (int i = 0; i < 8; i++)
    if (files[i].used && !strcmp(files[i].name, name)) return i;
  return -1;
}
bool extapp_fileExists(const char *name) { return find_file(name) >= 0; }
const uint8_t *extapp_fileRead(const char *name, uint32_t *len) {
  int i = find_file(name);
  if (i < 0) return NULL;
  *len = files[i].len;
  return files[i].data;
}
bool extapp_fileWrite(const char *name, const uint8_t *content, uint32_t len) {
  int i = find_file(name);
  for (int k = 0; i < 0 && k < 8; k++)
    if (!files[k].used) i = k;
  if (i < 0 || len > sizeof files[i].data || strlen(name) >= sizeof files[i].name) return false;
  strcpy(files[i].name, name);
  if (content) memcpy(files[i].data, content, len);
  files[i].len = len;
  files[i].used = true;
  return true;
}
bool extapp_fileErase(const char *name) {
  int i = find_file(name);
  if (i >= 0) files[i].used = false;
  return i >= 0;
}

/* Rust's prebuilt core for the host references an unwinding personality;
 * NumPlay's Rust code aborts on panic and never unwinds. */
void rust_eh_personality(void) {}

/* Tetris's app lifetime (storage.c provides it on the calculator). */
#include <setjmp.h>
static jmp_buf rust_leave;
static bool rust_running;
int np_app_run(void (*game)(void)) {
  if (!setjmp(rust_leave)) {
    rust_running = true;
    game();
  }
  rust_running = false;
  return 0;
}
void np_app_leave(void) {
  if (rust_running) longjmp(rust_leave, 1);
}
