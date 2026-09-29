/* Triggers of 2.0+ levels: colour channels (fades, copies with HSV,
 * pulses), groups (moves with easing, opacity fades, on / off) and the
 * trigger stream (position triggers in x order, then touch triggers).
 * Easing formulas are cocos2d's, as GD uses them (checked against GD by
 * gdsolver). */
#include "game.h"
#include <math.h>
#include <string.h>

static const uint8_t *rp;
static unsigned rv(void) {
  unsigned v = 0;
  for (int s = 0; s < 28; s += 7) {
    uint8_t b = *rp++;
    v |= (unsigned)(b & 127) << s;
    if (!(b & 128)) break;
  }
  return v;
}
static int rz(void) { unsigned v = rv(); return (v & 1) ? -(int)((v + 1) >> 1) : (int)(v >> 1); }
static uint16_t ticks_of(unsigned ms) { unsigned t = (ms * ND_HZ + 500) / 1000; return (uint16_t)(t > 65535 ? 65535 : t); }

/* ------------------------------------------------------------ groups */

bool gset_on(const Game *g, unsigned set) {
  if (!set) return true;
  const uint16_t *s = g->L->ext->gsets + set;
  for (unsigned k = 1; k <= s[0]; k++)
    if (s[k] < MAX_GROUPS && !g->gr[s[k]].on) return false;
  return true;
}

void gset_state(const Game *g, unsigned set, float *dx, float *dy, unsigned *alpha, bool *on) {
  *dx = *dy = 0;
  *alpha = 255;
  *on = true;
  if (!set) return;
  const uint16_t *s = g->L->ext->gsets + set;
  for (unsigned k = 1; k <= s[0]; k++) {
    if (s[k] >= MAX_GROUPS) continue;
    const GroupState *gr = &g->gr[s[k]];
    *dx += gr->dx;
    *dy += gr->dy;
    *alpha = *alpha * gr->alpha / 255;
    if (!gr->on) *on = false;
  }
}

/* ------------------------------------------------------------ easing */

static float bounce_out(float u) {
  if (u < 1 / 2.75f) return 7.5625f * u * u;
  if (u < 2 / 2.75f) { u -= 1.5f / 2.75f; return 7.5625f * u * u + .75f; }
  if (u < 2.5f / 2.75f) { u -= 2.25f / 2.75f; return 7.5625f * u * u + .9375f; }
  u -= 2.625f / 2.75f;
  return 7.5625f * u * u + .984375f;
}
static float elastic_out(float u, float per) {
  if (u <= 0 || u >= 1) return u;
  return powf(2, -10 * u) * sinf((u - per / 4) * 6.2831853f / per) + 1;
}
static float elastic_in(float u, float per) {
  if (u <= 0 || u >= 1) return u;
  u -= 1;
  return -powf(2, 10 * u) * sinf((u - per / 4) * 6.2831853f / per);
}

float ease(int kind, float rate, float u) {
  u = u < 0 ? 0 : u > 1 ? 1 : u;
  float p = rate > 0 ? rate : 2;
  switch (kind) {
    case 1: { float t = 2 * u; return t < 1 ? .5f * powf(t, p) : 1 - .5f * powf(2 - t, p); }
    case 2: return powf(u, p);
    case 3: return powf(u, 1 / p);
    case 4: return u < .5f ? elastic_in(2 * u, p) * .5f : elastic_out(2 * u - 1, p) * .5f + .5f;
    case 5: return elastic_in(u, p);
    case 6: return elastic_out(u, p);
    case 7: return u < .5f ? (1 - bounce_out(1 - 2 * u)) * .5f : bounce_out(2 * u - 1) * .5f + .5f;
    case 8: return 1 - bounce_out(1 - u);
    case 9: return bounce_out(u);
    case 10: return u < .5f ? .5f * powf(2, 10 * (2 * u - 1)) : .5f * (2 - powf(2, -10 * (2 * u - 1)));
    case 11: return u == 0 ? 0 : powf(2, 10 * (u - 1));
    case 12: return 1 - powf(2, -10 * u);
    case 13: return -.5f * (cosf(3.14159265f * u) - 1);
    case 14: return 1 - cosf(u * 1.5707963f);
    case 15: return sinf(u * 1.5707963f);
    case 16: {
      float o = 1.70158f * 1.525f;
      u *= 2;
      if (u < 1) return .5f * (u * u * ((o + 1) * u - o));
      u -= 2;
      return .5f * (u * u * ((o + 1) * u + o) + 2);
    }
    case 17: { float o = 1.70158f; return u * u * ((o + 1) * u - o); }
    case 18: { float o = 1.70158f; u -= 1; return u * u * ((o + 1) * u + o) + 1; }
    default: return u;
  }
}

/* ------------------------------------------------------------ channels */

static void chan_set(Channel *c, const uint8_t rgb[3], unsigned op, unsigned dur) {
  memcpy(c->from, c->cur, 3);
  memcpy(c->to, rgb, 3);
  c->op_from = c->op;
  c->op_to = (uint8_t)op;
  c->t = 0;
  c->dur = (uint16_t)dur;
  if (!dur) { memcpy(c->cur, rgb, 3); c->op = (uint8_t)op; }
}

static void chan_ease(Channel *c) {
  if (c->t >= c->dur) { memcpy(c->cur, c->to, 3); c->op = c->op_to; return; }
  c->t++;
  for (int k = 0; k < 3; k++) c->cur[k] = (uint8_t)(c->from[k] + ((int)c->to[k] - c->from[k]) * (int)c->t / (int)c->dur);
  c->op = (uint8_t)(c->op_from + ((int)c->op_to - c->op_from) * (int)c->t / (int)c->dur);
}

void hsv_shift(const uint8_t in[3], int h, int s64, int v64, unsigned flags, uint8_t out[3]) {
  float r = in[0] / 255.f, g = in[1] / 255.f, b = in[2] / 255.f;
  float mx = fmaxf(r, fmaxf(g, b)), mn = fminf(r, fminf(g, b)), d = mx - mn, hh = 0;
  if (d > 0) {
    if (mx == r) hh = fmodf((g - b) / d, 6);
    else if (mx == g) hh = (b - r) / d + 2;
    else hh = (r - g) / d + 4;
    hh *= 60;
  }
  float ss = mx > 0 ? d / mx : 0, vv = mx;
  hh = fmodf(hh + h + 720, 360);
  ss = (flags & CHF_SADD) ? ss + s64 / 64.f : ss * (s64 / 64.f);
  vv = (flags & CHF_VADD) ? vv + v64 / 64.f : vv * (v64 / 64.f);
  ss = ss < 0 ? 0 : ss > 1 ? 1 : ss;
  vv = vv < 0 ? 0 : vv > 1 ? 1 : vv;
  float c = vv * ss, x = c * (1 - fabsf(fmodf(hh / 60, 2) - 1)), m = vv - c, rr, gg, bb;
  int sec = (int)(hh / 60) % 6;
  switch (sec) {
    case 0: rr = c; gg = x; bb = 0; break;
    case 1: rr = x; gg = c; bb = 0; break;
    case 2: rr = 0; gg = c; bb = x; break;
    case 3: rr = 0; gg = x; bb = c; break;
    case 4: rr = x; gg = 0; bb = c; break;
    default: rr = c; gg = 0; bb = x; break;
  }
  out[0] = (uint8_t)((rr + m) * 255 + .5f);
  out[1] = (uint8_t)((gg + m) * 255 + .5f);
  out[2] = (uint8_t)((bb + m) * 255 + .5f);
}

/* ------------------------------------------------------------ start */

void trig_start(Game *g) {
  const LevelExt *x = g->L->ext;
  for (unsigned k = 0; k < MAX_GROUPS; k++) g->gr[k] = (GroupState){0, 0, 255, 255, 255, 1, 0, 0};
  for (unsigned c = 0; c < MAX_CHANNELS; c++) {
    Channel *ch = &g->ch[c];
    if (!x || c >= x->nchans) {
      if (c >= CH_COUNT) memset(ch->cur, 255, 3);
      ch->op = ch->op_to = 255;
      continue;
    }
    const LChan *l = &x->chans[c];
    memcpy(ch->cur, l->rgb, 3);
    memcpy(ch->to, l->rgb, 3);
    ch->op = ch->op_to = l->opacity;
    ch->flags = l->flags;
    ch->copy = l->copy;
    ch->h = l->h;
    ch->s = l->s;
    ch->v = l->v;
  }
  g->trig_pos = 0;
  g->trig_x = 0;
  g->nmv = g->npu = 0;
  memset(g->touch_bits, 0, sizeof(g->touch_bits));
  if (x) {
    rp = x->trig;
    g->trig_x = (float)rv();
    g->trig_pos = (uint32_t)(rp - x->trig);
  }
}

/* ------------------------------------------------------------ firing */

/* Reads a trigger's fields at rp; applies it only if `apply`. */
static void fire(Game *g, unsigned kind, bool apply) {
  switch (kind) {
    case TR_FADE: { unsigned v = *rp++; if (apply) g->fade_effect = (uint8_t)v; break; }
    case TR_TRAIL: { unsigned v = *rp++; if (apply) g->trail = v != 0; break; }
    case TR_COLOR: {
      unsigned c = *rp++;
      uint8_t rgb[3] = {rp[0], rp[1], rp[2]};
      unsigned op = rp[3], fl = rp[4];
      rp += 5;
      unsigned dur = ticks_of(rv());
      uint8_t copy = 255;
      int h = 0, s = 64, v = 64;
      unsigned hf = 0;
      if (fl & CHF_COPY) { copy = *rp++; h = rz(); s = rz(); v = rz(); hf = *rp++; }
      for (int k = 0; k < 2 && apply; k++) {
        unsigned t = k ? ((fl & 8) && c == CH_BG ? CH_G1 : 255u) : c;
        if (t >= MAX_CHANNELS) continue;
        Channel *ch = &g->ch[t];
        chan_set(ch, rgb, op, dur);
        ch->flags = (uint8_t)((fl & (CHF_BLEND | CHF_P1 | CHF_P2 | CHF_COPY)) | (hf & (CHF_SADD | CHF_VADD)));
        ch->copy = copy;
        ch->h = (int16_t)h; ch->s = (int16_t)s; ch->v = (int16_t)v;
      }
      break;
    }
    case TR_MOVE: {
      unsigned grp = rv();
      float dx = rz() * .25f, dy = rz() * .25f;
      unsigned dur = ticks_of(rv());
      unsigned easing = rp[0], rate = rp[1], fl = rp[2];
      rp += 3;
      if (grp >= MAX_GROUPS || !apply) break;
      if (!dur && !(fl & 3)) { g->gr[grp].dx += dx; g->gr[grp].dy += dy; break; }
      if (g->nmv >= MAX_MOVES) break;
      g->mv[g->nmv++] = (MoveAction){(uint16_t)grp, (uint8_t)easing, (uint8_t)fl, dx, dy, 0, 0, rate / 10.f, 0, (uint16_t)(dur ? dur : 1)};
      break;
    }
    case TR_ALPHA: {
      unsigned grp = rv(), op = *rp++, dur = ticks_of(rv());
      if (grp >= MAX_GROUPS || !apply) break;
      GroupState *gr = &g->gr[grp];
      gr->a_from = gr->alpha;
      gr->a_to = (uint8_t)op;
      gr->at = 0;
      gr->adur = (uint16_t)dur;
      if (!dur) gr->alpha = (uint8_t)op;
      break;
    }
    case TR_TOGGLE: {
      unsigned grp = rv(), on = *rp++;
      if (grp < MAX_GROUPS && apply) g->gr[grp].on = (uint8_t)on;
      break;
    }
    case TR_PULSE: {
      PulseAction p;
      p.target = (uint16_t)rv();
      memcpy(p.rgb, rp, 3);
      p.flags = rp[3];
      rp += 4;
      p.tin = ticks_of(rv());
      p.hold = ticks_of(rv());
      p.tout = ticks_of(rv());
      p.copy = 255; p.h = 0; p.s = 64; p.v = 64;
      if (p.flags & CHF_COPY) {
        p.copy = *rp++;
        p.h = (int16_t)rz(); p.s = (int16_t)rz(); p.v = (int16_t)rz();
        p.flags |= (uint8_t)(*rp++ & (CHF_SADD | CHF_VADD));
      }
      p.t = 0;
      if (!apply) break;
      if (g->npu < MAX_PULSES) g->pu[g->npu++] = p;
      else { memmove(g->pu, g->pu + 1, sizeof(PulseAction) * (MAX_PULSES - 1)); g->pu[MAX_PULSES - 1] = p; }
      break;
    }
    default: break;
  }
}

/* Reads one trigger at rp and fires it unless its groups are off. */
static void run_one(Game *g) {
  unsigned kind = *rp++, set = 0;
  if (kind & 0x80) { set = rv(); kind &= 0x7f; }
  fire(g, kind, gset_on(g, set));
}

void trig_touch(Game *g, unsigned idx) {
  const LevelExt *x = g->L->ext;
  if (!x || idx >= x->ntouch || idx >= sizeof(g->touch_bits) * 8) return;
  if (g->touch_bits[idx >> 3] >> (idx & 7) & 1) return;
  g->touch_bits[idx >> 3] |= (uint8_t)(1u << (idx & 7));
  rp = x->trig + x->touch[idx];
  run_one(g);
}

/* ------------------------------------------------------------ per tick */

void trig_step(Game *g, float player_dx) {
  const LevelExt *x = g->L->ext;
  if (!x) return;
  /* position triggers the player has passed */
  while (g->trig_pos < x->trig_len && g->trig_x < g->p.x) {
    rp = x->trig + g->trig_pos;
    if (*rp == 0) { g->trig_pos = x->trig_len; break; }   /* end of the position triggers */
    run_one(g);
    g->trig_x += (float)rv();
    g->trig_pos = (uint32_t)(rp - x->trig);
  }
  /* moves */
  for (unsigned k = 0; k < g->nmv;) {
    MoveAction *m = &g->mv[k];
    GroupState *gr = &g->gr[m->group];
    m->t++;
    float e = ease(m->easing, m->rate, (float)m->t / m->dur);
    gr->dx += m->tx * e - m->done_x;
    gr->dy += m->ty * e - m->done_y;
    m->done_x = m->tx * e;
    m->done_y = m->ty * e;
    if (m->flags & 1) gr->dx += player_dx;
    if (m->t >= m->dur) { *m = g->mv[--g->nmv]; continue; }
    k++;
  }
  /* opacity fades */
  for (unsigned k = 0; k < x->ngroups && k < MAX_GROUPS; k++) {
    GroupState *gr = &g->gr[k];
    if (gr->at >= gr->adur) continue;
    gr->at++;
    gr->alpha = (uint8_t)(gr->a_from + ((int)gr->a_to - gr->a_from) * (int)gr->at / (int)gr->adur);
  }
  /* pulses */
  for (unsigned k = 0; k < g->npu;) {
    PulseAction *p = &g->pu[k];
    p->t++;
    if (p->t > p->tin + p->hold + p->tout) { memmove(p, p + 1, sizeof(*p) * (g->npu - k - 1)); g->npu--; continue; }
    k++;
  }
  for (unsigned c = 0; c < x->nchans && c < MAX_CHANNELS; c++) chan_ease(&g->ch[c]);
}

/* Pulse strength 0..1 at its current time. */
float pulse_level(const PulseAction *p) {
  if (p->t < p->tin) return p->tin ? (float)p->t / p->tin : 1;
  if (p->t <= p->tin + p->hold) return 1;
  return p->tout ? 1 - (float)(p->t - p->tin - p->hold) / p->tout : 0;
}
