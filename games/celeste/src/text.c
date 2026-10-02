#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")   /* not drawn every frame: smaller over faster */
#endif
/* Dialog: Celeste's Textbox, FancyText and the portraits, line by line from
 * the game's code. The text is laid out in the game's 1920x1080 interface
 * units (exactly, from the font's own advances) and drawn at 1/6.
 *
 * The dialog comes from data.bin's DIALOG, decoded into the texture cache's
 * free space when a page is laid out; only the current page's nodes are kept. */
#include "text.h"
#include "level.h"
#include "entities.h"

/* ---------------------------------------------------------------- the dialog in data.bin */
/* DIALOG: u16 n, u16 0, u32 key hashes (sorted), then per key u16 pack, u16 length, u32 offset */
static bool dialog_find(const char *key, uint16_t *pack, uint16_t *len, uint32_t *off) {
  uint32_t h = 2166136261u;
  for (const char *s = key; *s; s++) {
    char c = *s >= 'a' && *s <= 'z' ? (char)(*s - 32) : *s;
    h = (h ^ (uint8_t)c) * 16777619u;
  }
  const uint8_t *d = section(SEC_DIALOG);
  int n = rd16(d), lo = 0, hi = n - 1;
  while (lo <= hi) {
    int m = (lo + hi) / 2;
    uint32_t v = rd32(d + 4 + 4 * m);
    if (v == h) {
      const uint8_t *p = d + 4 + 4 * n + 8 * m;
      *pack = rd16(p), *len = rd16(p + 2), *off = rd32(p + 4);
      return true;
    }
    if (v < h) lo = m + 1;
    else hi = m - 1;
  }
  return false;
}
bool dialog_has(const char *key) {
  uint16_t p, l;
  uint32_t o;
  return dialog_find(key, &p, &l, &o);
}
typedef struct { uint8_t *dst; uint32_t n; } CopySink;
static void copy_sink(const uint8_t *p, uint32_t n, void *ctx) {
  CopySink *c = ctx;
  memcpy(c->dst + c->n, p, n);
  c->n += n;
}
/* the text of a key, in scratch memory (good until textures load); Dialog.Get's "[KEY]" if missing */
static const char *dialog_text(const char *key, int *len) {
  uint16_t pack, n;
  uint32_t off, cap;
  if (!dialog_find(key, &pack, &n, &off)) {
    static char missing[48];
    path2(missing, "[", key, -1);
    size_t k = strlen(missing);
    missing[k] = ']', missing[k + 1] = 0;
    *len = (int)k + 1;
    return missing;
  }
  uint8_t *buf = res_scratch(n, &cap);
  if (cap < n) n = (uint16_t)cap;
  CopySink c = {buf, 0};
  res_stream(pack, off, n, copy_sink, &c);
  *len = n;
  return (const char *)buf;
}

/* Dialog.Clean: the text without its commands */
int dialog_clean(const char *key, char *out, int cap) {
  int n, k = 0;
  const char *s = dialog_text(key, &n);
  for (int i = 0; i < n && k < cap - 1; i++) {
    if (s[i] == '{') {
      if ((i + 2 < n && s[i + 1] == 'n' && s[i + 2] == '}') || (i + 6 < n && !memcmp(s + i, "{break}", 7))) out[k++] = '\n';
      while (i < n && s[i] != '}') i++;
      continue;
    }
    if (s[i] == '\\') continue;
    out[k++] = s[i];
  }
  out[k] = 0;
  return k;
}

/* ---------------------------------------------------------------- FancyText */
enum { N_CHAR, N_NEWLINE, N_NEWPAGE, N_WAIT, N_TRIGGER, N_PORTRAIT, N_ANCHOR };
enum { CF_SHAKE = 1, CF_WAVE = 2, CF_IMPACT = 4, CF_PUNCT = 8, CF_BIG = 16, CF_ROTNEG = 32, CF_ROTBIG = 64, CF_MESSED = 128 };
typedef struct {
  uint8_t type, flags;
  uint16_t v;           /* char: its code; wait: ms; trigger: index | 0x8000 silent; anchor: 0 top 1 middle 2 bottom;
                           portrait: its slot */
  int16_t pos;          /* Position: interface pixels x4 */
  uint16_t width;       /* LineWidth x4 */
  uint16_t color;
  uint16_t delay;       /* Delay x 10000 */
  uint16_t index;       /* Index (waves) */
  uint8_t fade;         /* Fade x 255 */
  int8_t yoff;          /* YOffset (messed up text) */
} TNode;
typedef struct {
  uint8_t bank, begin, idle, talk;   /* 255: none; bank 254: {portrait none} */
  int8_t side;
  uint8_t flags;                     /* 1 upside down, 2 flipped, 4 pop, 8 glitchy */
  uint16_t textbox, overlay;
} Port;
#define MAX_NODES 176
#define MAX_PORTS 8
static TNode nodes[MAX_NODES];
static int nnodes;          /* in the buffer */
static int base;            /* global index of nodes[0] */
static Port ports[MAX_PORTS];
static int nports;

/* the textbox's sizes (interface pixels) */
#define LINE_H 63           /* Renogare 64's LineHeight - 1 */
#define LINES 3             /* (int)(240 / LINE_H) */
#define PAD 41.5f           /* (272 - LINE_H * LINES) / 2 */
#define MAXW_NOPORT 1605    /* 1688 - PAD * 2 */
#define MAXW 1333           /* MAXW_NOPORT - 240 - 32 */

typedef struct {
  char key[40];
  int src_pos, src_len;     /* the parse: how far in the text */
  bool done;
  float pos;                /* currentPosition */
  int line, page, char_index;
  uint16_t color, colors[8];
  int ncolors;
  float scale, delay;
  bool shake, wave, impact, messed;
  int last_port[2];
} Parser;
static Parser P;

/* the 64 px face's advance and kerning in interface pixels (the data keeps them x16 at 1/6) */
static int adv(uint32_t c) {
  const uint8_t *g = font_glyph_rec(FONT_S, c);
  return g ? (int)((rd16(g + 8) * 6 + 8) / 16) : -1;
}
static int kern(uint32_t a, uint32_t b) {
  int k = font_kerning_x16(FONT_S, a, b);
  return k >= 0 ? (k * 6 + 8) / 16 : -((-k * 6 + 8) / 16);
}

static TNode *add_node(int type) {
  if (nnodes >= MAX_NODES) return NULL;
  TNode *n = &nodes[nnodes++];
  memset(n, 0, sizeof *n);
  n->type = (uint8_t)type;
  return n;
}

static void calc_line_width(void) {
  int i = nnodes - 1;
  TNode *last = NULL;
  for (; i >= 0 && !last; i--) {
    if (nodes[i].type == N_CHAR) last = &nodes[i];
    else if (nodes[i].type == N_NEWLINE || nodes[i].type == N_NEWPAGE) return;
  }
  if (!last) return;
  float sc = (last->flags & CF_BIG) ? 1.5f : 1;
  uint16_t w = (uint16_t)(last->pos + adv(last->v) * sc * 4);
  last->width = w;
  for (; i >= 0 && nodes[i].type != N_NEWLINE && nodes[i].type != N_NEWPAGE; i--)
    if (nodes[i].type == N_CHAR) nodes[i].width = w;
}

static void add_new_line(void) {
  calc_line_width();
  P.line++;
  P.pos = 0;
  if (P.line > LINES) {
    P.page++;
    P.line = 0;
    add_node(N_NEWPAGE);
  } else
    add_node(N_NEWLINE);
}

static bool is_comma(uint32_t c) { return c == ','; }
static bool is_period(uint32_t c) { return c == '.' || c == '!' || c == '?'; }

/* AddWord */
static void add_word(const char *w, int n, int maxw) {
  /* size.Measure: advances and kerning with the next character */
  float width = 0;
  for (const char *s = w, *end = w + n; s < end;) {
    uint32_t c = utf8_next(&s, end);
    int a = adv(c);
    if (a < 0) continue;
    width += a;
    if (s < end) {
      const char *t = s;
      width += kern(c, utf8_next(&t, end));
    }
  }
  if (P.pos + width * P.scale > maxw) add_new_line();
  uint32_t prev = 0;
  for (const char *s = w, *end = w + n; s < end;) {
    uint32_t c = utf8_next(&s, end);
    bool last = s >= end;
    uint32_t before = prev;
    prev = c;
    if ((P.pos == 0 && c == ' ') || c == '\\') continue;
    int a = adv(c);
    if (a < 0) continue;
    float d = 0;
    if (last && before != '\\') {
      if (is_comma(c)) d = 0.15f;
      else if (is_period(c)) d = 0.3f;
    }
    TNode *nd = add_node(N_CHAR);
    if (!nd) return;
    nd->v = (uint16_t)c;
    nd->index = (uint16_t)P.char_index++;
    nd->pos = (int16_t)(P.pos * 4);
    nd->color = P.color;
    float delay = P.impact ? 0.0034999999f : P.delay + d;
    nd->delay = (uint16_t)(delay * 10000 + 0.5f);
    nd->flags = (uint8_t)((P.shake ? CF_SHAKE : 0) | (P.wave ? CF_WAVE : 0) | (P.impact ? CF_IMPACT : 0) |
                          (is_comma(c) || is_period(c) ? CF_PUNCT : 0) | (P.scale > 1 ? CF_BIG : 0));
    if (P.messed) {
      nd->flags |= CF_MESSED | (rndi(2) ? CF_ROTNEG : 0) | (rndi(2) ? CF_ROTBIG : 0);
      static const int8_t yo[4] = {-3, -6, 3, 6};
      nd->yoff = yo[rndi(4)];
    }
    P.pos += a * P.scale;
    if (!last) P.pos += kern(c, c) * P.scale;   /* the game looks up the kerning with the same character */
  }
}

static float parse_float(const char *s, int n, bool *ok) {
  float v = 0, frac = 0, k = 1;
  bool dot = false, neg = false, any = false;
  for (int i = 0; i < n; i++) {
    char c = s[i];
    if (i == 0 && c == '-') neg = true;
    else if (c == '.' && !dot) dot = true;
    else if (c >= '0' && c <= '9') {
      any = true;
      if (dot) k *= 0.1f, frac += (c - '0') * k;
      else v = v * 10 + (c - '0');
    } else {
      *ok = false;
      return 0;
    }
  }
  *ok = any;
  return (neg ? -1 : 1) * (v + frac);
}
static uint16_t hex_color(const char *s, int n) {
  uint32_t v = 0;
  for (int i = 0; i < 6 && i < n; i++) {
    char c = s[i];
    v = v << 4 | (uint32_t)(c >= 'a' ? c - 'a' + 10 : c >= 'A' ? c - 'A' + 10 : c - '0');
  }
  return rgb(v);
}
static bool tok_is(const char *t, int n, const char *s) { return (int)strlen(s) == n && !memcmp(t, s, (size_t)n); }
static bool is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }
/* Regex.Split(text, "(\s|\{|\})"): a whitespace character, a brace, or a run of anything else */
static int token(const char *src, int len, int *at, const char **t) {
  int i = *at;
  if (i >= len) return 0;
  *t = src + i;
  if (is_space(src[i]) || src[i] == '{' || src[i] == '}') {
    *at = i + 1;
    return 1;
  }
  int j = i;
  while (j < len && !is_space(src[j]) && src[j] != '{' && src[j] != '}') j++;
  *at = j;
  return j - i;
}

/* FancyText.Parse, going on until the buffer holds the next page (or the text ends) */
static int parse_maxw = MAXW;   /* the lines' width (interface pixels) */
static void parse_more(void) {
  if (P.done || !g_res_can_load) return;   /* (never while strips are drawn: the decoder uses the strip) */
  int len;
  const char *src = dialog_text(P.key, &len);
  int maxw = parse_maxw;
  for (;;) {
    /* enough: a page beyond what is shown is complete */
    int pages_ahead = 0;
    for (int i = 0; i < nnodes; i++)
      if (nodes[i].type == N_NEWPAGE) pages_ahead++;
    if (pages_ahead >= 2 || nnodes > MAX_NODES - 48) return;
    const char *t;
    int n = token(src, len, &P.src_pos, &t);
    if (!n) break;
    if (!(n == 1 && t[0] == '{')) {
      add_word(t, n, maxw);
      continue;
    }
    /* a command: its name, then its words up to the brace */
    const char *name;
    int nn = token(src, len, &P.src_pos, &name);
    const char *arg[6];
    int argn[6], nargs = 0;
    for (;;) {
      const char *a;
      int an = token(src, len, &P.src_pos, &a);
      if (!an || (an == 1 && a[0] == '}')) break;
      if (an == 1 && is_space(a[0])) continue;
      if (nargs < 6) arg[nargs] = a, argn[nargs++] = an;
    }
    bool ok;
    float f = parse_float(name, nn, &ok);
    if (ok) {
      TNode *w = add_node(N_WAIT);
      if (w) w->v = (uint16_t)(f * 1000 + 0.5f);
      continue;
    }
    if (name[0] == '#') {
      const char *c = nn > 1 ? name + 1 : nargs ? arg[0] : NULL;
      int cn = nn > 1 ? nn - 1 : nargs ? argn[0] : 0;
      if (!cn) {
        P.color = P.ncolors ? P.colors[--P.ncolors] : 0xD69A;   /* LightGray */
        continue;
      }
      if (P.ncolors < 8) P.colors[P.ncolors++] = P.color;
      if (tok_is(c, cn, "red")) P.color = 0xF800;
      else if (tok_is(c, cn, "green")) P.color = 0x0400;
      else if (tok_is(c, cn, "blue")) P.color = 0x001F;
      else P.color = hex_color(c, cn);
      continue;
    }
    if (tok_is(name, nn, "break")) {
      calc_line_width();
      P.page++;
      P.line = 0;
      P.pos = 0;
      add_node(N_NEWPAGE);
    } else if (tok_is(name, nn, "n")) {
      add_new_line();
    } else if (tok_is(name, nn, ">>")) {
      float k = nargs ? parse_float(arg[0], argn[0], &ok) : 0;
      P.delay = nargs && ok ? 0.01f / k : 0.01f;
    } else if (tok_is(name, nn, "/>>")) {
      P.delay = 0.01f;
    } else if (tok_is(name, nn, "anchor")) {
      TNode *a = add_node(N_ANCHOR);
      if (a && nargs) a->v = (argn[0] && (arg[0][0] | 32) == 'b') ? 2 : (argn[0] && (arg[0][0] | 32) == 'm') ? 1 : 0;
    } else if (tok_is(name, nn, "portrait") || tok_is(name, nn, "left") || tok_is(name, nn, "right")) {
      int slot;
      if (name[0] == 'p') {
        slot = nports++ % MAX_PORTS;
        Port *pt = &ports[slot];
        memset(pt, 0, sizeof *pt);
        if (nargs && tok_is(arg[0], argn[0], "none")) {
          pt->bank = 254;
        } else if (nargs) {
          int v[8] = {255, 255, 255, 255, 0, 0, 65535, 65535}, k = 0, cur = 0, neg = 0;
          for (int i = 0; i <= argn[0] && k < 8; i++) {
            char c = i < argn[0] ? arg[0][i] : ',';
            if (c == '-') neg = 1;
            else if (c == ',') v[k++] = neg ? -cur : cur, cur = 0, neg = 0;
            else cur = cur * 10 + (c - '0');
          }
          pt->bank = (uint8_t)v[0], pt->begin = (uint8_t)v[1], pt->idle = (uint8_t)v[2], pt->talk = (uint8_t)v[3];
          pt->side = (int8_t)v[4], pt->flags = (uint8_t)v[5], pt->textbox = (uint16_t)v[6], pt->overlay = (uint16_t)v[7];
          P.last_port[pt->side > 0] = slot;
        }
      } else
        slot = P.last_port[name[0] == 'r'];
      TNode *pn = add_node(N_PORTRAIT);
      if (pn) pn->v = (uint16_t)slot;
    } else if (tok_is(name, nn, "trigger") || tok_is(name, nn, "silent_trigger")) {
      int k = 0;
      if (nargs) {
        for (int i = 0; i < argn[0]; i++) k = k * 10 + (arg[0][i] - '0');
        TNode *tn = add_node(N_TRIGGER);
        if (tn) tn->v = (uint16_t)(k | (name[0] == 's' ? 0x8000 : 0));
      }
    } else if (tok_is(name, nn, "*")) P.shake = true;
    else if (tok_is(name, nn, "/*")) P.shake = false;
    else if (tok_is(name, nn, "~")) P.wave = true;
    else if (tok_is(name, nn, "/~")) P.wave = false;
    else if (tok_is(name, nn, "!")) P.impact = true;
    else if (tok_is(name, nn, "/!")) P.impact = false;
    else if (tok_is(name, nn, "%")) P.messed = true;
    else if (tok_is(name, nn, "/%")) P.messed = false;
    else if (tok_is(name, nn, "big")) P.scale = 1.5f;
    else if (tok_is(name, nn, "/big")) P.scale = 1;
    else if (tok_is(name, nn, "s")) {
      int k = 1;
      if (nargs) {
        k = 0;
        for (int i = 0; i < argn[0]; i++) k = k * 10 + (arg[0][i] - '0');
      }
      P.pos += 5 * k;
    } else if (tok_is(name, nn, "savedata")) {
      add_word("Madeline", 8, maxw);
    }
  }
  calc_line_width();
  P.done = true;
}

static void parse_start(const char *key) {
  memset(&P, 0, sizeof P);
  parse_maxw = MAXW;
  strncpy(P.key, key, sizeof P.key - 1);
  P.color = 0xD69A;   /* FancyText.DefaultColor: LightGray */
  P.scale = 1;
  P.delay = 0.01f;
  nnodes = base = nports = 0;
  parse_more();
}
/* drops the nodes before the global index `upto` (a page that is done) */
static void drop_before(int upto) {
  int k = upto - base;
  if (k <= 0) return;
  if (k > nnodes) k = nnodes;
  memmove(nodes, nodes + k, sizeof(TNode) * (size_t)(nnodes - k));
  nnodes -= k;
  base += k;
  parse_more();
}
static int node_count(void) { return P.done ? base + nnodes : 1 << 30; }
static TNode *node_at(int i) {
  if (i - base >= nnodes) parse_more();
  return i >= base && i - base < nnodes ? &nodes[i - base] : NULL;
}

/* ---------------------------------------------------------------- Textbox */
typedef struct {
  TextEvent events;
  void *ctx;
  int index, start, page;
  int last;                 /* the type of the last node done, -1 none */
  float ease, gradient, timer, delay, buildup;
  int co, sub;              /* RunRoutine's place, and an inner coroutine's */
  float wait;
  uint8_t anchor, waiting, disable_input, in_trigger, can_skip, easing_open, easing_close, opened, finished;
  uint8_t skip_started, frozen;
  int8_t port;              /* the portrait slot shown, -1 none */
  uint8_t port_exists, port_idling;
  Sprite spr;
  Wiggler wiggle;
  uint16_t textbox, overlay;
  uint32_t shake_seed;
  Port cur;                 /* this.portrait */
  int8_t has_cur;
  int ev;
} Box;
static const EntClass TEXTBOX;
static uint16_t port_tex[48];
static int nport_tex;

static void idle_anim(Box *b) {
  if (!b->port_idling && b->has_cur && b->cur.idle != 255) {
    spr_play(&b->spr, b->cur.idle, false);
    b->port_idling = 1;
  }
}
static void talk_anim(Box *b) {
  if (b->port_idling && b->has_cur && b->cur.talk != 255) {
    spr_play(&b->spr, b->cur.talk, false);
    b->port_idling = 0;
  }
}
static bool continue_pressed(Box *b) {
  uint32_t edge = g_in.keys & ~g_in.prev;
  if (edge & (K_OK | K_JUMP | K_DASH)) return !b->disable_input;   /* MenuConfirm, MenuCancel */
  return false;
}

/* the portrait's frames, and those of the animations they go to, kept loaded */
static void load_portrait(const Port *p) {
  nport_tex = 0;
  uint16_t todo[16];
  int ntodo = 0;
  uint8_t seen[32] = {0};
  if (p->begin != 255) todo[ntodo++] = p->begin;
  if (p->idle != 255) todo[ntodo++] = p->idle;
  if (p->talk != 255) todo[ntodo++] = p->talk;
  while (ntodo) {
    uint16_t a = todo[--ntodo];
    if (a < 256 && (seen[a >> 3] >> (a & 7) & 1)) continue;
    if (a < 256) seen[a >> 3] |= (uint8_t)(1 << (a & 7));
    uint16_t tex[32], gotos[8];
    int ng = 0, n = bank_anim_textures(p->bank, a, tex, 32, gotos, &ng);
    for (int i = 0; i < n; i++) {
      bool have = false;
      for (int j = 0; j < nport_tex; j++) have |= port_tex[j] == tex[i];
      if (!have && nport_tex < 48) port_tex[nport_tex++] = tex[i];
    }
    for (int i = 0; i < ng && ntodo < 16; i++)
      if (gotos[i] != 0xFFFF) todo[ntodo++] = gotos[i];
  }
  if (p->textbox != 65535 && nport_tex < 48) port_tex[nport_tex++] = p->textbox;
  if (p->overlay != 65535 && nport_tex < 48) port_tex[nport_tex++] = p->overlay;
  level_extra_textures(port_tex, nport_tex);
}

/* the coroutine: CO_YIELD ends this update; a wait first runs down (Monocle's waitTimer) */
#define CO_BEGIN switch (b->co) { case 0:
#define CO_YIELD do { b->co = __LINE__; return; case __LINE__:; } while (0)
#define CO_WAIT(t) do { b->wait = (t); CO_YIELD; } while (0)
#define CO_END } b->finished = 1

/* EaseOpen / EaseClose as nested coroutines: true while running (an extra update before and after) */
static bool ease_open(Box *b) {
  switch (b->sub) {
    case 0:
      b->sub = 1;
      return true;
    case 1:
      if (b->ease < 1) {
        b->easing_open = 1;
        b->sub = 2;
      } else {
        b->sub = 3;
        return true;
      }
      /* fall through */
    case 2:
      if ((b->ease += RAW_DT / 0.4f) < 1) {
        b->gradient = fmaxf(b->gradient, b->ease);
        return true;
      }
      b->ease = b->gradient = 1;
      b->easing_open = 0;
      b->sub = 3;
      return true;
    default:
      b->sub = 0;
      return false;
  }
}
static bool ease_close(Box *b, bool final) {
  switch (b->sub) {
    case 0:
      b->sub = 1;
      return true;
    case 1:
      b->easing_close = 1;
      b->sub = 2;
      /* fall through */
    case 2:
      if ((b->ease -= RAW_DT / 0.4f) > 0) {
        if (final) b->gradient = b->ease;
        return true;
      }
      b->ease = 0;
      b->easing_close = 0;
      b->sub = 3;
      return true;
    default:
      b->sub = 0;
      return false;
  }
}
#define CO_OPEN do { while (ease_open(b)) CO_YIELD; } while (0)
#define CO_CLOSE(f) do { while (ease_close(b, f)) CO_YIELD; } while (0)

/* RunRoutine: one update of it */
static void run_update(Ent *e) {
  Box *b = ST(e, Box);
  if (b->wait > 0) {   /* (the textbox runs on raw time) */
    b->wait -= RAW_DT;
    return;
  }
  TNode *cur;
  CO_BEGIN;
  b->last = -1;
  b->buildup = 0;
  while (b->index < node_count()) {
    cur = node_at(b->index);
    if (!cur) break;
    b->delay = 0;
    if (cur->type == N_ANCHOR) {
      if (b->ease >= 1 && cur->v != b->anchor) CO_CLOSE(false);
      b->anchor = (uint8_t)node_at(b->index)->v;
    } else if (cur->type == N_PORTRAIT) {
      b->port = (int8_t)cur->v;
      {
        Port *next = &ports[b->port];
        if (b->ease >= 1 && (!b->has_cur || next->bank != b->cur.bank || next->side != b->cur.side)) CO_CLOSE(false);
      }
      {
        Port *next = &ports[b->port];
        b->textbox = T__textbox_default;
        b->overlay = 0xFFFF;
        b->port_exists = next->bank < 254;
        if (b->port_exists) {
          if (!b->has_cur || next->bank != b->cur.bank) spr_init(&b->spr, next->bank);
          load_portrait(next);
          b->textbox = next->textbox;
          b->overlay = next->overlay;
          b->can_skip = 0;
          bool changed = !b->has_cur || b->cur.bank != next->bank || b->cur.idle != next->idle ||
                         b->cur.talk != next->talk || b->cur.begin != next->begin;
          b->cur = *next;
          b->has_cur = 1;
          if (next->flags & 4) wiggler_restart(&b->wiggle);
          if (!changed) goto opened;
          if (next->begin == 255) goto begun;
          spr_play(&b->spr, next->begin, true);
        } else {
          b->has_cur = 0;
          goto plain;
        }
      }
      CO_OPEN;
      while (b->spr.anim == b->cur.begin && b->spr.playing) CO_YIELD;
    begun:
      if (b->cur.idle != 255) {
        b->port_idling = 1;
        spr_play(&b->spr, b->cur.idle, true);
      }
    opened:
      CO_OPEN;
      b->can_skip = 1;
      goto next_node;
    plain:
      CO_OPEN;
    } else if (cur->type == N_NEWPAGE) {
      idle_anim(b);
      if (b->ease >= 1) {
        b->waiting = 1;
        CO_WAIT(0.1f);
        while (!continue_pressed(b)) CO_YIELD;
        b->waiting = 0;
      }
      b->start = b->index + 1;
      b->page++;
      drop_before(b->start);
    } else if (cur->type == N_WAIT) {
      idle_anim(b);
      b->delay = cur->v / 1000.f;
    } else if (cur->type == N_TRIGGER) {
      b->in_trigger = 1;
      idle_anim(b);
      if (!(node_at(b->index)->v & 0x8000)) CO_CLOSE(false);
      b->ev = node_at(b->index)->v & 0x7FFF;
      if (b->events) {   /* yield return events[n](): a nested coroutine */
        CO_YIELD;
        while (!b->events(b->ctx, b->ev)) CO_YIELD;
        CO_YIELD;
      }
      b->in_trigger = 0;
    } else if (cur->type == N_CHAR) {
      if (b->ease < 1) CO_OPEN;
      cur = node_at(b->index);
      {
        bool flag = false;
        if (b->index - 5 > b->start)
          for (int i = b->index; i < b->index + 4 && i < node_count(); i++) {
            TNode *t = node_at(i);
            if (t && t->type == N_NEWPAGE) {
              flag = true;
              idle_anim(b);
            }
          }
        if (!flag && !(cur->flags & CF_PUNCT)) talk_anim(b);
      }
      if (b->last == N_NEWPAGE) {
        b->index--;
        CO_WAIT(0.2f);
        b->index++;
      }
      b->delay = node_at(b->index)->delay / 10000.f + b->buildup;
    }
  next_node:
    b->last = node_at(b->index) ? node_at(b->index)->type : -1;
    b->index++;
    if (b->delay < 0.016f) {
      b->buildup += b->delay;
      continue;
    }
    b->buildup = 0;
    if (b->delay > 0.5f) idle_anim(b);
    CO_WAIT(b->delay);
  }
  idle_anim(b);
  if (b->ease > 0) {
    b->waiting = 1;
    while (!continue_pressed(b)) CO_YIELD;
    b->waiting = 0;
    b->start = node_count();
    CO_CLOSE(true);
  }
  b->opened = 0;
  ent_remove(e);
  level_extra_textures(NULL, 0);
  CO_END;
}

/* SkipDialog */
static void skip_update(Ent *e) {
  Box *b = ST(e, Box);
  if (b->skip_started) b->disable_input = 0;
  b->skip_started = 1;
  if (!b->waiting && b->can_skip && !b->easing_open && !b->easing_close && continue_pressed(b)) {
    b->disable_input = 1;
    for (int guard = 0; guard < 4000 && !b->waiting && b->can_skip && !b->easing_open && !b->easing_close &&
                        !b->in_trigger && !b->finished && b->opened;
         guard++)
      run_update(e);
  }
}

static void box_update(Ent *e) {
  Box *b = ST(e, Box);
  if (g_level.frozen || g_level.paused) return;   /* FrozenOrPaused */
  skip_update(e);
  if (!b->opened) return;
  run_update(e);
  if (!b->opened) return;
  if (level_on_interval(0.05f)) b->shake_seed = (uint32_t)rndi(1 << 30);
  if (b->port_exists && b->ease >= 1) spr_update(&b->spr);
  b->timer += DT;
  wiggler_update(&b->wiggle);
  int n = b->index < node_count() ? b->index : node_count();
  for (int i = b->start; i < n; i++) {
    TNode *t = node_at(i);
    if (t && t->type == N_CHAR && t->fade < 255) {
      int f = t->fade + (int)(8 * DT * 255 + 0.5f);
      t->fade = (uint8_t)(f > 255 ? 255 : f);
    }
  }
}

/* ---------------------------------------------------------------- drawing */
typedef struct {
  float x, y, sx, sy, alpha;   /* the text's origin (screen), scale, alpha */
  int start;
  uint32_t seed;
} FancyDraw;
static FancyDraw TD;

static void blend_px(uint16_t *row, int x, uint16_t col, int a) {
  if ((unsigned)x >= VIEW_W || a <= 0) return;
  row[x] = a >= 255 ? col : blend565(row[x], col, a + 1);
}
/* a glyph of `size` at screen (x, y) (its top-left), scaled by k (nearest pixels) */
static void glyph_strip(uint16_t *strip, int sy0, int sy1, int size, const uint8_t *g, float x, float y, float k,
                        uint16_t col, int alpha) {
  int w = g[6], h = g[7];
  if (!w) return;
  const uint8_t *bits = font_glyph_bits(size, g);
  int stride = (w + 1) / 2;
  int W = (int)ceilf(w * k), H = (int)ceilf(h * k);
  int X0 = (int)floorf(x + 0.5f), Y0 = (int)floorf(y + 0.5f);
  for (int r = 0; r < H; r++) {
    int Y = Y0 + r;
    if (Y < sy0 || Y >= sy1) continue;
    int gy = (int)(r / k);
    if (gy >= h) continue;
    uint16_t *row = strip + (Y - sy0) * VIEW_W;
    for (int c = 0; c < W; c++) {
      int gx = (int)(c / k);
      if (gx >= w) continue;
      int v = (bits[gy * stride + (gx >> 1)] >> ((gx & 1) * 4)) & 15;
      if (v) blend_px(row, X0 + c, col, v * 17 * alpha / 255);
    }
  }
}

/* ActiveFont.Draw(text, position, justify, scale, color), in interface units: each line justified
 * by its own width, the block by its height */
static float line_width(const char *s, const char *end, int size) {
  float w = 0;
  while (s < end && *s != '\n') {
    uint32_t c = utf8_next(&s, end);
    const char *q = s;
    uint32_t next = q < end && *q != '\n' ? utf8_next(&q, end) : 0;
    w += font_glyph_advance(size, c, next);
  }
  return w;
}
void text_into(uint16_t *strip, int sy0, int sy1, const char *s, float px, float py, float jx, float jy, float scale,
               uint16_t col, uint8_t alpha) {
  int size = scale > 1.01f ? FONT_M : FONT_S;   /* the 64 face, or the 192 one at 1/9 (scale 2) */
  float k = size == FONT_M ? scale / 2 : scale;
  const char *end = s + strlen(s);
  float lh = font_line_height(size), lines = 1;
  for (const char *q = s; q < end; q++) lines += *q == '\n';
  float x0 = px / 6, y = py / 6 - lines * lh * k * jy;
  if (y > sy1 || y + lines * lh * k < sy0) return;
  float x = x0 - line_width(s, end, size) * k * jx;
  while (s < end) {
    uint32_t c = utf8_next(&s, end);
    if (c == '\n') {
      y += lh * k;
      x = x0 - line_width(s, end, size) * k * jx;
      continue;
    }
    const uint8_t *g = font_glyph_rec(size, c);
    if (!g) continue;
    if (y + lh * k >= sy0 && y < sy1)
      glyph_strip(strip, sy0, sy1, size, g, x + rds16(g + 2) / 16.f * k, y + (int16_t)rd16(g + 4) * k, k, col, alpha);
    const char *q = s;
    x += font_glyph_advance(size, c, q < end && *q != '\n' ? utf8_next(&q, end) : 0) * k;
  }
}
static void draw_strip(uint16_t *strip, int sy0, int sy1, void *ctx) {
  const TextDraw *d = ctx;
  text_into(strip, sy0, sy1, d->s, d->x, d->y, d->jx, d->jy, d->scale, d->col, d->alpha);
}
void text_draw(const TextDraw *d) {
  if (!d->s || !d->alpha) return;
  int lines = 1;
  for (const char *q = d->s; *q; q++) lines += *q == '\n';
  int size = d->scale > 1.01f ? FONT_M : FONT_S;
  float k = size == FONT_M ? d->scale / 2 : d->scale, h = lines * font_line_height(size) * k;
  float y = d->y / 6 - h * d->jy;
  gfx_custom(draw_strip, (void *)d, (int)floorf(y) - 1, (int)ceilf(y + h) + 1);
}
/* UISTR: a menu's word from the dialog, straight from flash ("" when there is none) */
const char *ui_str(const char *key) {
  uint32_t h = 2166136261u;
  for (const char *s = key; *s; s++) h = (h ^ (uint8_t)(*s >= 'a' && *s <= 'z' ? *s - 32 : *s)) * 16777619u;
  const uint8_t *d = section(SEC_UISTR);
  int n = rd16(d), lo = 0, hi = n - 1;
  while (lo <= hi) {
    int m = (lo + hi) / 2;
    uint32_t v = rd32(d + 4 + 4 * m);
    if (v == h) return (const char *)d + 4 + 6 * n + rd16(d + 4 + 4 * n + 2 * m);
    if (v < h) lo = m + 1;
    else hi = m - 1;
  }
  return "";
}

/* ActiveFont.Measure(text).X, in interface units at scale 1 */
float text_measure(const char *s) {
  float w = 0;
  const char *end = s + strlen(s);
  while (s < end) {
    float lw = line_width(s, end, FONT_M) * 3;   /* the 192 face at 1/9 is the 64 one at 1/3 */
    if (lw > w) w = lw;
    while (s < end && *s != '\n') s++;
    if (s < end) s++;
  }
  return w;
}
/* PixelFontSize.AutoNewline(text, width), in place (the text grows by nothing: spaces become newlines,
 * words wider than the width get one inserted, up to cap) */
void text_auto_newline(char *s, int cap, float width) {
  char out[256];
  int n = 0;
  float num = 0;
  const char *p = s;
  while (*p && n < (int)sizeof out - 2) {
    const char *w = p;   /* a word, or one whitespace character (Regex.Split "(\s)") */
    if (*p == ' ' || *p == '\n') p++;
    else
      while (*p && *p != ' ' && *p != '\n') p++;
    char word[128];
    int wl = (int)(p - w) < 127 ? (int)(p - w) : 127;
    memcpy(word, w, (size_t)wl);
    word[wl] = 0;
    float x = text_measure(word);
    if (x + num > width) {
      out[n++] = '\n';
      num = 0;
      if (wl == 1 && word[0] == ' ') continue;
    }
    for (int i = 0; i < wl && n < (int)sizeof out - 1; i++) out[n++] = word[i];
    num += x;
  }
  if (n > cap - 1) n = cap - 1;
  memcpy(s, out, (size_t)n);
  s[n] = 0;
}

/* FancyText.Text.Draw from `start` to its page's end, justified (0.5, 0.5) at TD's origin */
static void text_strip(uint16_t *strip, int sy0, int sy1, void *ctx) {
  (void)ctx;
  int end = node_count();
  float widest = 0, lines = 0, cur = 0;
  for (int i = TD.start; i < end; i++) {
    TNode *t = node_at(i);
    if (!t || t->type == N_NEWPAGE) break;
    if (t->type == N_NEWLINE) {
      if (cur == 0) cur = 1;
      lines += cur;
      cur = 0;
    } else if (t->type == N_CHAR) {
      if (t->width / 4.f > widest) widest = (float)(int)(t->width / 4);
      cur = fmaxf(cur, (t->flags & CF_BIG) ? 1.5f : 1);
    }
  }
  lines += cur;
  /* in interface units, then to the screen */
  float px = TD.x - 0.5f * widest * TD.sx, py = TD.y - 0.5f * lines * 64 * TD.sy;
  cur = 0;
  uint32_t seed = TD.seed;
  for (int i = TD.start; i < end; i++) {
    TNode *t = node_at(i);
    if (!t || t->type == N_NEWPAGE) break;
    if (t->type == N_NEWLINE) {
      if (cur == 0) cur = 1;
      py += 64 * cur * TD.sy;
      cur = 0;
    }
    if (t->type != N_CHAR) continue;
    float fade = t->fade / 255.f, scale = (t->flags & CF_BIG) ? 1.5f : 1;
    cur = fmaxf(cur, scale);
    float num = ((t->flags & CF_IMPACT) ? 2 - fade : 1) * scale;
    float vx = TD.sx * num, vy = TD.sy * num, v = fmaxf(vx, vy);
    float zx = 0, zy = 0;
    if (t->flags & CF_SHAKE) {
      seed = seed * 1103515245u + 12345u;
      zx = (float)((int)((seed >> 16) % 3) - 1) * 2;
      seed = seed * 1103515245u + 12345u;
      zy = (float)((int)((seed >> 16) % 3) - 1) * 2;
    }
    if (t->flags & CF_WAVE) zy += sinf(t->index * 0.25f + g_level.raw_time_active * 8) * 4;
    zy += -8 * (1 - fade) + t->yoff * fade;
    float x = px + t->pos / 4.f * TD.sx;
    /* the 64 face up to its size, the 192 face (shown at 1/6, 32 px) above */
    int size = v > 1.01f ? FONT_M : FONT_S;
    float k = size == FONT_M ? v / 2 : v;
    const uint8_t *g = font_glyph_rec(size, t->v);
    if (!g) continue;
    float gx = (x + zx * vx) / 6 + rds16(g + 2) / 16.f * k, gy = (py + zy * vy) / 6 + (int16_t)rd16(g + 4) * k;
    int a = (int)(fade * TD.alpha * 255);
    glyph_strip(strip, sy0, sy1, size, g, gx, gy, k, t->color, a);
  }
}

static void box_render(Ent *e) {
  Box *b = ST(e, Box);
  if (g_level.frozen || g_level.paused) return;
  float num = ease_cube_inout(b->ease);
  if (num < 0.05f) return;
  float vx = 116, vy = 58;   /* interface units */
  if (b->anchor == 2) vy = 1080 - 58 - 272;
  else if (b->anchor == 1) vy = 404;
  vy += (int)(136 * (1 - num));
  gfx_hud(true);
  /* the box, squashed while it opens */
  float cx = (vx + 844) / 6, cy = (vy + 272 * num / 2) / 6;
  Tex t;
  if (tex_get(b->textbox, &t))
    gfx_tex_ex(b->textbox, cx, cy, t.fw * t.scale / 2.f, t.fh * t.scale / 2.f, 1, num, 0, 0xFFFF, 255, 0);
  else if (tex_get(T__textbox_default, &t))
    gfx_tex_ex(T__textbox_default, cx, cy, t.fw / 2.f, t.fh / 2.f, 1, num, 0, 0xFFFF, 255, 0);
  if (b->waiting) {
    float w = (!b->has_cur || b->cur.side < 0) ? 1688 : 1432;
    float bx = vx + w - 48, by = vy + 272 - 40 + (fmodf(b->timer, 1) < 0.25f ? 6 : 0);
    if (tex_get(T__textboxbutton, &t)) gfx_tex_ex(T__textboxbutton, bx / 6, by / 6, t.fw / 2.f, t.fh / 2.f, 1, 1, 0, 0xFFFF, 255, 0);
  }
  if (b->port_exists && b->has_cur) {
    Sprite s = b->spr;
    float px = b->cur.side > 0 ? vx + 1688 - 240 - 16 : vx + 16;
    float sx = b->cur.side > 0 ? -1.f : 1.f;   /* (the frames are stored at the portrait's 1.5 scale) */
    if (b->cur.flags & 2) sx = -sx;
    float sy = ((272 * num - 32) / 240) * ((b->cur.flags & 1) ? -1 : 1);
    float w = 0.9f + b->wiggle.value * 0.1f;
    s.sx = sx * w, s.sy = sy * w;
    s.flipx = s.sx < 0, s.flipy = s.sy < 0;
    s.sx = fabsf(s.sx), s.sy = fabsf(s.sy);
    s.alpha = (uint8_t)(255 * num);
    if (fabsf(sy * w) > 0.05f) spr_draw(&s, (px + 120) / 6, (vy + 272 * num * 0.5f) / 6);
  }
  if (b->overlay != 0xFFFF && tex_get(b->overlay, &t)) {
    int f = b->has_cur && b->cur.side > 0 ? GF_FLIPX : 0;
    gfx_tex_ex(b->overlay, cx, cy, t.fw / 2.f, t.fh / 2.f, 1, num, 0, 0xFFFF, 255, (uint8_t)f);
  }
  /* the text */
  int lines = 1;
  for (int i = b->start; i < node_count(); i++) {
    TNode *n = node_at(i);
    if (!n || n->type == N_NEWPAGE) break;
    if (n->type == N_NEWLINE) lines++;
  }
  float ox = PAD + (b->has_cur && b->cur.side < 0 ? 256 : 0), oy = PAD;
  float hx = (!b->has_cur ? MAXW_NOPORT : MAXW) / 2.f, hy = LINES * LINE_H * num / 2;
  float k = lines >= 4 ? 0.75f : 1;
  TD = (FancyDraw){vx + ox + hx, vy + oy + hy, k, num * k, num, b->start, b->shake_seed};
  gfx_custom(text_strip, NULL, (int)((vy) / 6) - 4, (int)((vy + 272) / 6) + 8);
  gfx_hud(false);
}

static const EntClass TEXTBOX = {.size = sizeof(Box), .name = "textbox", .update = box_update, .render = box_render};

Ent *textbox_say(const char *key, TextEvent events, void *ctx) {
  for (int i = 0; i < g_nents; i++)   /* one at a time: the nodes are shared */
    if (g_ents[i].cls == &TEXTBOX && g_ents[i].dead != 1) ent_remove(&g_ents[i]);
  Ent *e = ent_new(&TEXTBOX, 0, 0);
  if (!e) return NULL;
  e->tags = TAG_PAUSE_UPDATE | TAG_HUD;
  e->collidable = 0;
  Box *b = ST(e, Box);
  b->events = events, b->ctx = ctx;
  b->opened = 1;
  b->can_skip = 1;
  b->port = -1;
  b->textbox = T__textbox_default;
  b->overlay = 0xFFFF;
  wiggler_init(&b->wiggle, 0.4f, 4);
  parse_start(key);
  return e;
}
/* Textbox.PortraitName / PortraitAnimation: the shown portrait's bank and its idle animation */
bool textbox_portrait(int *bank, int *idle) {
  for (int i = 0; i < g_nents; i++) {
    Ent *e = &g_ents[i];
    if (e->cls != &TEXTBOX || e->dead == 1) continue;
    Box *b = ST(e, Box);
    if (!b->has_cur || b->cur.bank >= 254) return false;
    *bank = b->cur.bank, *idle = b->cur.idle;
    return true;
  }
  return false;
}
bool textbox_opened(const Ent *e) { return e && e->cls == &TEXTBOX && e->dead != 1 && ST(e, Box)->opened; }
void textbox_set_frozen(Ent *e) {
  if (e) e->tags |= TAG_FROZEN_UPDATE;
}

/* ---------------------------------------------------------------- MiniTextbox */
/* a line or two at the top of the screen, a portrait beside, while the game goes on; it closes by itself */
#define MINI_LEN 120
typedef struct {
  float ease, wait;
  uint8_t co, closing, has_port, nlines;
  int index, len;
  Port port;
  Sprite spr;
  uint16_t box;
  char text[MINI_LEN];       /* UTF-8, '\n' between the lines */
  uint8_t delay[MINI_LEN];   /* each character's delay, in 2 ms */
  char line[3][MINI_LEN / 2];
  TextDraw td[3];
} Mini;
static const EntClass MINI;
static int utf8_len(uint8_t c) { return c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4; }
static void mini_update(Ent *e) {
  Mini *m = ST(e, Mini);
  if (!m->closing && (g_player.dead || g_level.transitioning)) m->closing = 1;   /* RetryPlayerCorpse; OnOutBegin */
  if (m->has_port) spr_update(&m->spr);
  if (m->closing) {
    if ((m->ease -= RAW_DT * 4) <= 0) ent_remove(e);
    return;
  }
  if (m->wait > 0) {   /* (on raw time) */
    m->wait -= RAW_DT;
    return;
  }
  switch (m->co) {
    case 0:
      if ((m->ease += RAW_DT * 4) < 1) return;
      m->ease = 1;
      m->co = 1;
      if (m->has_port && m->port.begin != 255) {
        spr_play(&m->spr, m->port.begin, false);
        return;
      }
      /* fall through */
    case 1:
      if (m->has_port && m->port.begin != 255 && m->spr.anim == m->port.begin && m->spr.playing) return;
      if (m->has_port && m->port.talk != 255) spr_play(&m->spr, m->port.talk, false);
      m->co = 2;
      /* fall through */
    case 2: {
      float num = 0;
      while (m->index < m->len) {
        num += m->delay[m->index++] * 0.002f;
        if (num > 0.016f) {
          m->wait = num;
          return;
        }
      }
      if (m->has_port && m->port.idle != 255) spr_play(&m->spr, m->port.idle, false);
      m->wait = 3;
      m->co = 3;
      return;
    }
    default: m->closing = 1; return;
  }
}
static void mini_render(Ent *e) {
  Mini *m = ST(e, Mini);
  if (m->ease <= 0 || g_level.frozen || g_level.paused || g_player.dead || g_level.skipping_cutscene) return;
  gfx_hud(true);
  Tex t;
  float cy = 130;   /* 72 + (1920 - 1688) / 4 */
  if (tex_get(m->box, &t)) gfx_tex_ex(m->box, 160, cy / 6, t.fw * t.scale / 2.f, t.fh * t.scale / 2.f, 1, m->ease, 0, 0xFFFF, 255, 0);
  if (m->has_port) {   /* 112 interface pixels (the frames are kept for 240) */
    Sprite s = m->spr;
    s.sx = 112.f / 240, s.sy = 112.f / 240 * m->ease;
    spr_draw(&s, 188 / 6.f, cy / 6);
  }
  /* the characters shown so far, a line at a time (the text keeps its place as it grows) */
  int l = 0, k = 0, c = 0;
  for (int i = 0; i < 3; i++) m->line[i][0] = 0;
  for (const char *p = m->text; *p && c < m->index && l < 3; c++) {
    if (*p == '\n') {
      m->line[l][k] = 0;
      l++, k = 0, p++;
      continue;
    }
    int n = utf8_len((uint8_t)*p);
    if (k + n < MINI_LEN / 2) memcpy(&m->line[l][k], p, (size_t)n), k += n;
    p += n;
  }
  if (l < 3) m->line[l][k] = 0;
  float lh = LINE_H * 0.75f * m->ease;
  for (int i = 0; i < m->nlines && i < 3; i++) {
    if (!m->line[i][0]) continue;
    m->td[i] = (TextDraw){m->line[i], 276, cy - m->nlines * lh / 2 + i * lh, 0, 0, 0.75f, 0xD69A, 255};
    text_draw(&m->td[i]);
  }
  gfx_hud(false);
}
static const EntClass MINI = {.size = sizeof(Mini), .name = "miniTextbox", .update = mini_update, .render = mini_render};

Ent *mini_textbox(const char *key) {
  for (int i = 0; i < g_nents; i++)
    if (g_ents[i].cls == &TEXTBOX && g_ents[i].dead != 1) return NULL;   /* (the nodes are the textbox's) */
  bool others = false;
  for (int i = 0; i < g_nents; i++)
    if (g_ents[i].cls == &MINI && g_ents[i].dead != 1) ST(&g_ents[i], Mini)->closing = 1, others = true;
  Ent *e = ent_new(&MINI, 0, 0);
  if (!e) return NULL;
  e->tags = TAG_HUD | TAG_TRANSITION_UPDATE;
  e->collidable = 0;
  Mini *m = ST(e, Mini);
  m->box = T__textbox_default_mini;
  m->wait = others ? 0.3f : 0;
  memset(&P, 0, sizeof P);   /* FancyText.Parse(text, 1688 - 112 - 32, 2) */
  strncpy(P.key, key, sizeof P.key - 1);
  P.color = 0xD69A, P.scale = 1, P.delay = 0.01f;
  nnodes = base = nports = 0;
  parse_maxw = 1544;
  parse_more();
  parse_maxw = MAXW;
  int at = 0;
  m->nlines = 1;
  for (int i = 0; i < nnodes; i++) {
    TNode *n = &nodes[i];
    if (n->type == N_NEWPAGE) break;
    if (n->type == N_PORTRAIT && !m->has_port && ports[n->v].bank < 254) {
      m->port = ports[n->v];
      m->has_port = 1;
      spr_init(&m->spr, m->port.bank);
      if (m->port.idle != 255) spr_play(&m->spr, m->port.idle, false);
      m->box = m->port.textbox == T__textbox_theo ? T__textbox_theo_mini
               : m->port.textbox == T__textbox_badeline ? T__textbox_badeline_mini : T__textbox_default_mini;
    } else if ((n->type == N_CHAR || n->type == N_NEWLINE) && at < MINI_LEN - 4 && m->len < MINI_LEN) {
      uint32_t c = n->type == N_NEWLINE ? '\n' : n->v;
      if (c == '\n') {
        if (!at || m->nlines >= 3) continue;
        m->nlines++;
      }
      if (c < 0x80) m->text[at++] = (char)c;
      else if (c < 0x800) m->text[at++] = (char)(0xC0 | c >> 6), m->text[at++] = (char)(0x80 | (c & 63));
      else m->text[at++] = (char)(0xE0 | c >> 12), m->text[at++] = (char)(0x80 | (c >> 6 & 63)), m->text[at++] = (char)(0x80 | (c & 63));
      m->delay[m->len++] = n->type == N_CHAR ? (uint8_t)(n->delay < 5100 ? n->delay / 20 : 255) : 0;
    }
  }
  m->text[at] = 0;
  P.done = true, nnodes = 0;   /* (the next textbox starts over) */
  return e;
}
