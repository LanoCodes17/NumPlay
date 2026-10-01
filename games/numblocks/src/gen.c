/* NumBlocks world generator: Minecraft 1.8.8's overworld generator in C.
 *
 * Ported from the decompiled 1.8.8 server (Spigot v1_8_R3 names in comments):
 * GenLayer* (biomes), ChunkProviderGenerate (terrain noise, surface),
 * WorldGenCaves / WorldGenCanyon (caves, ravines), BiomeDecorator and the
 * WorldGen* features (population), WorldServer's spawn search.
 *
 * ---------------------------------------------------------------------------
 * DEVIATIONS FROM MINECRAFT 1.8.8 (everything else is meant to be exact)
 * ---------------------------------------------------------------------------
 * Arithmetic
 *  - java.util.Random, GenLayer LCGs and all integer logic are bit exact.
 *  - Perlin/simplex noise: the cell index and the fraction of every sample are
 *    computed exactly (double/fixed point setup per octave), but the gradient,
 *    fade and lerp math, the octave sums and the density formula run in float
 *    instead of double. Results differ by ~1e-6 relative, so a block flips only
 *    where the density is within that of 0 (very rare). Java's quirk of reusing
 *    stale x-lerps while the y cell does not change is kept.
 *  - Terrain interpolation is done per column (trilinear, float) rather than
 *    with Java's incremental double steps: same maths, different rounding.
 *  - MathHelper.sin/cos: the 65536-entry table is replaced by a float
 *    polynomial on the same table index (within ~1 ulp of the table value).
 *  - Caves, ravines, ore veins, lakes, big oak trees use float where Java uses
 *    double (cave positions are kept in 32.32 fixed point so they do not drift).
 *  - Voronoi biome zoom uses exact integer distances (double only for ties).
 *  - Bedrock: nextInt(5) is drawn 256 times per column; the draws whose value is
 *    unused are skipped with an LCG jump (identical unless a draw would have hit
 *    Java's rejection loop, probability 1.4e-9 per draw).
 * Session-dependent behaviour of Minecraft, made deterministic
 *  - WorldGenBigTree keeps the height of the first big oak of a biome for the
 *    whole server session (and shortens it when blocked): here every big oak
 *    draws its own height, as the first one of a session would.
 *  - The decorator's huge mushroom keeps the first mushroom's colour for the
 *    session: here each one draws its colour.
 *  - Mutable biome top blocks (mega taiga podzol, extreme hills stone...) used
 *    when caves expose dirt: the biome's default top block is used.
 * Population (decoration)
 *  - Minecraft populates chunk P (writing into the 32x32 area P+8..P+24 that
 *    spans four chunks) once P and its +x/+z neighbours exist, reading the live
 *    world, so the result depends on chunk load order. Here gen_slab(C) replays
 *    the four populations that write into C ((cx-1,cz-1), (cx,cz-1), (cx-1,cz),
 *    (cx,cz)); each population P decides everything that affects its random
 *    stream from a "view" that is identical whatever chunk is being generated:
 *    the undecorated terrain of the 3x3 chunks around (a cached per-column
 *    summary: top block, water floor, filler depth; no caves) plus P's own
 *    earlier writes (height map, top block, plants). So objects crossing chunk
 *    borders are always complete and consistent, but a population does not see
 *    the other populations' trees, and does not see caves except where
 *    Minecraft's outcome needs them: dungeons and lakes query the real caves
 *    and ravines of their box (exact walk). Writes landing in C are re-checked
 *    against C's real blocks (caves included), e.g. leaves never replace logs.
 *  - Light is not computed: mushrooms need "shade" = an opaque block above in
 *    the view; snow and ice ignore block light.
 *  - Dungeon chests and spawners are placed; chest loot is not stored, but the
 *    random numbers Minecraft draws for it are (so later features match).
 *  - Structures are not generated (see TODO below), which matches Minecraft
 *    only where no structure exists.
 * TODO: structures (villages, strongholds, mineshafts, temples, monuments).
 * ---------------------------------------------------------------------------
 */
#include "gen.h"

#include <string.h>

#include "blocks.h"

#if defined(__clang__)
#pragma clang fp contract(off)
#endif

typedef int64_t i64;
typedef uint64_t u64;
typedef int32_t i32;
typedef uint32_t u32;

#define SEA 63

/* ======================================================================== */
/* java.util.Random                                                          */
/* ======================================================================== */

#define JMUL 0x5DEECE66DULL
#define JADD 0xBULL
#define JMASK ((1ULL << 48) - 1)

typedef struct {
    u64 s;
} JRand;

static void jr_seed(JRand *r, i64 seed) { r->s = ((u64)seed ^ JMUL) & JMASK; }

static inline i32 jr_next(JRand *r, int bits) {
    r->s = (r->s * JMUL + JADD) & JMASK;
    return (i32)(u32)(r->s >> (48 - bits));
}

static i32 jr_int(JRand *r, i32 n) {
    if ((n & -n) == n) return (i32)(((i64)n * (i64)jr_next(r, 31)) >> 31);
    i32 bits, val;
    do {
        bits = jr_next(r, 31);
        val = bits % n;
    } while ((i32)((u32)bits - (u32)val + (u32)(n - 1)) < 0);
    return val;
}

static i64 jr_long(JRand *r) {
    i64 hi = jr_next(r, 32);
    i64 lo = jr_next(r, 32);
    return (i64)((u64)hi << 32) + lo;
}

static inline int jr_bool(JRand *r) { return jr_next(r, 1) != 0; }

static inline float jr_float(JRand *r) { return (float)jr_next(r, 24) * (1.0f / 16777216.0f); }

/* nextDouble as the exact 53-bit integer numerator (value = n / 2^53) */
static inline i64 jr_double_bits(JRand *r) {
    i64 a = jr_next(r, 26);
    i64 b = jr_next(r, 27);
    return (a << 27) + b;
}

static inline double jr_double(JRand *r) { return (double)jr_double_bits(r) * (1.0 / 9007199254740992.0); }

/* nextDouble rounded to float (one rounding from the exact value) */
static inline float jr_doublef(JRand *r) { return (float)jr_double_bits(r) * (1.0f / 9007199254740992.0f); }

/* LCG jump: advance by n steps (n < 1024) */
static u64 jump_mul[10], jump_add[10];

static void jr_jump_init(void) {
    u64 m = JMUL, a = JADD;
    for (int i = 0; i < 10; i++) {
        jump_mul[i] = m;
        jump_add[i] = a;
        a = (a * m + a) & JMASK;
        m = (m * m) & JMASK;
    }
}

static void jr_skip(JRand *r, unsigned n) {
    for (int i = 0; n; i++, n >>= 1)
        if (n & 1) r->s = (r->s * jump_mul[i] + jump_add[i]) & JMASK;
}

/* ======================================================================== */
/* MathHelper                                                                */
/* ======================================================================== */

/* sin of table index i (0..65535): (float)Math.sin(i * 2pi / 65536), as MathHelper's table.
 * A float kernel (fdlibm's sinf/cosf kernels) on the first quadrant gets within an ulp; a
 * 2-bit-per-entry correction table (generated on a host from the exact values) makes it exact.
 * The kernels must not be contracted into FMAs, or the corrections would not apply. */
#if defined(__GNUC__) && !defined(__clang__)
#define NO_CONTRACT __attribute__((optimize("fp-contract=off")))
#else
#define NO_CONTRACT
#endif
/* MathHelper sin table corrections (generated by tests/gen/t_sintab2.c): 2 bits per
 * first-quadrant index, value = correction in ulps + 1, 3 = see SIN_EXC */
static const uint8_t SIN_CORR[4097] = {
85,20,84,69,80,84,21,84,21,21,68,84,1,20,96,21,85,68,21,84,84,84,84,21,
69,65,84,85,85,69,65,65,85,65,69,20,21,69,21,81,68,21,85,65,84,5,69,65,
85,81,81,20,5,85,85,21,84,5,22,68,21,84,1,20,1,21,65,21,81,5,84,16,
21,64,85,20,0,85,129,85,81,165,85,21,4,86,21,69,84,85,68,64,85,69,1,100,
69,85,65,69,21,5,85,69,4,69,85,5,16,85,21,5,85,21,5,16,85,21,4,101,
85,1,4,5,20,1,80,69,65,68,21,81,5,85,85,20,80,85,20,1,84,85,1,65,
85,0,17,84,85,5,80,85,85,1,21,81,65,80,21,21,0,85,5,150,85,85,69,17,
20,84,153,21,80,17,5,0,81,86,85,85,85,80,0,160,89,85,84,65,68,0,84,85,
21,85,20,64,16,164,86,21,85,20,5,0,101,85,21,69,21,68,0,84,85,85,68,0,
16,80,85,85,85,17,1,64,0,106,85,85,69,84,21,16,85,85,85,85,1,16,65,85,
85,64,81,0,1,148,85,85,17,1,16,1,64,85,21,81,21,17,64,84,85,69,21,21,
0,17,144,85,80,85,21,16,64,80,85,85,21,1,0,0,84,85,85,1,69,65,0,84,
85,80,20,20,1,0,84,85,85,4,1,1,0,85,21,85,68,5,1,0,85,17,21,81,
21,1,4,85,84,69,84,1,65,64,85,81,84,64,86,85,89,85,21,85,85,17,85,85,
68,80,84,84,89,85,85,85,85,21,81,85,81,20,0,4,0,170,85,85,85,85,85,65,
69,20,0,68,4,0,65,149,101,85,69,85,81,85,21,17,84,65,4,85,64,101,85,85,
85,85,69,1,84,20,84,16,65,21,80,101,85,85,85,69,21,4,85,1,80,4,85,0,
84,86,85,85,85,85,85,17,85,5,65,85,16,4,100,101,85,85,85,21,81,64,17,1,
16,16,64,16,105,86,85,85,85,85,81,65,65,68,85,64,0,64,149,85,85,85,17,85,
21,5,64,81,64,17,5,65,85,101,85,85,68,85,69,1,80,81,21,17,0,80,85,85,
85,85,85,21,81,21,20,0,1,85,16,164,85,85,85,85,85,81,85,20,0,81,0,17,
0,148,85,81,85,69,81,17,1,16,1,64,16,4,1,85,85,85,85,85,85,80,4,0,
65,21,16,64,0,85,85,85,85,85,85,84,20,64,4,64,4,0,80,89,85,85,85,21,
21,85,84,17,1,69,0,1,100,85,85,21,69,85,21,65,69,20,4,84,1,0,80,85,
85,85,21,80,20,20,1,16,5,1,85,64,84,85,21,85,81,1,68,0,17,0,65,5,
0,0,85,85,85,20,69,21,20,85,17,4,0,64,16,0,85,21,21,81,21,5,68,0,
21,64,4,16,68,64,85,85,85,85,21,21,21,20,1,68,64,0,64,80,85,85,20,80,
1,69,85,20,89,169,149,101,149,85,85,85,85,85,85,85,81,69,17,21,64,17,81,85,
16,81,0,5,64,1,88,102,149,85,85,101,85,85,85,85,84,85,85,17,69,21,84,20,
0,68,4,20,16,65,85,65,4,64,101,86,85,89,85,85,85,85,69,85,85,85,69,69,
85,81,85,0,20,69,85,16,81,65,1,80,64,96,86,90,85,85,85,85,85,69,81,85,
85,85,85,21,81,84,80,21,0,20,65,85,16,1,0,1,1,84,102,165,86,85,85,85,
21,85,85,21,64,21,5,80,68,1,20,17,0,4,4,0,1,1,64,64,0,101,101,85,
149,85,101,85,85,85,85,65,69,84,80,17,85,21,81,0,4,4,0,17,5,4,17,0,
148,101,101,85,85,85,85,85,85,85,85,81,1,85,21,64,81,84,81,20,64,17,5,20,
16,0,68,64,85,102,85,85,85,85,85,85,21,85,85,17,85,81,81,21,21,85,69,84,
80,68,0,4,4,64,64,0,89,89,85,89,85,85,85,21,85,65,85,5,80,85,64,17,
85,69,20,80,68,1,69,80,5,65,64,96,89,85,85,85,85,85,85,69,81,20,69,21,
85,69,84,21,81,0,17,5,69,4,64,4,21,0,80,84,101,85,85,85,85,85,85,85,
85,81,85,85,84,20,21,20,81,85,68,85,17,5,5,4,64,4,0,85,85,85,86,85,
85,85,84,21,85,85,85,5,20,65,69,69,21,4,85,16,84,65,80,65,4,0,144,85,
85,85,85,85,85,85,85,5,65,69,85,68,85,81,84,16,21,5,4,16,81,21,16,0,
1,0,85,153,85,85,85,85,21,69,81,84,85,20,69,81,64,17,0,69,65,65,1,68,
80,65,64,64,16,0,85,85,85,86,85,85,85,69,85,20,21,85,84,0,69,1,69,1,
68,84,16,1,4,20,4,20,5,68,85,85,85,85,85,85,85,85,85,21,80,84,84,84,
84,85,85,20,80,20,80,20,80,4,64,4,64,84,85,89,85,81,21,85,85,69,65,84,
84,81,69,21,20,65,64,80,65,80,65,0,16,1,4,80,16,85,86,85,85,65,85,85,
85,85,81,85,85,85,20,80,4,64,20,16,65,65,80,65,0,16,84,16,148,85,85,85,
85,85,85,21,80,84,85,81,81,81,17,85,69,1,17,21,4,5,17,64,16,16,1,0,
84,85,85,85,85,85,85,69,85,69,69,84,69,80,20,20,68,85,68,5,65,69,4,4,
84,16,20,64,85,85,85,85,85,85,85,1,85,85,68,21,69,69,84,65,1,5,85,4,
0,1,0,0,16,84,0,64,85,85,85,69,85,84,0,85,80,20,20,5,84,0,5,21,
81,5,5,68,0,4,21,16,16,1,0,84,85,85,85,85,85,85,69,81,80,5,80,1,
68,64,81,85,80,69,0,68,21,4,16,0,16,0,4,85,85,85,85,84,85,20,80,17,
84,64,81,21,1,20,85,68,38,18,17,22,34,81,17,17,18,18,17,17,81,17,17,17,
17,17,17,209,209,29,17,17,93,72,149,137,152,84,132,68,84,69,69,68,68,68,69,68,
68,68,4,68,68,68,68,68,68,7,67,48,87,101,21,37,101,85,22,18,17,81,17,81,
17,81,85,21,81,85,81,21,69,21,17,21,20,85,81,21,85,85,85,85,85,69,85,85,
68,84,68,69,68,85,20,85,84,68,68,69,84,68,68,65,21,5,64,84,97,85,18,97,
85,85,21,86,82,21,85,85,81,81,80,85,81,21,17,21,81,17,21,81,17,81,21,69,
84,68,69,84,88,84,69,85,73,68,85,85,68,85,69,68,69,85,85,84,65,68,68,68,
68,4,68,85,21,81,85,97,85,17,86,21,85,17,81,17,85,85,85,81,21,17,21,81,
16,84,21,85,65,1,81,85,85,68,85,73,85,85,84,68,84,69,85,84,85,85,85,85,
85,69,85,81,84,69,85,0,84,68,85,82,85,85,85,85,85,82,21,17,21,85,85,17,
81,85,17,21,16,85,81,21,85,81,21,21,17,80,69,69,84,68,85,85,85,68,84,85,
69,69,84,68,69,84,68,85,5,64,85,69,84,68,5,84,4,69,85,85,85,82,85,81,
85,81,85,85,85,85,85,17,81,85,21,21,81,17,85,21,16,69,21,81,1,89,69,85,
84,73,68,85,73,84,68,69,88,85,68,84,69,85,84,69,68,85,20,69,85,84,84,84,
68,85,85,21,85,21,81,85,17,21,81,17,85,21,81,17,21,81,85,21,21,81,69,1,
81,21,85,16,145,149,84,84,85,89,85,69,84,149,84,69,68,85,85,69,85,68,69,68,
64,69,5,68,68,69,68,5,37,85,85,37,21,86,85,21,38,85,85,85,85,21,21,81,
85,17,21,81,81,1,81,17,17,85,65,5,85,152,148,85,85,85,69,84,132,85,73,84,
68,85,85,84,85,0,84,68,68,85,84,69,68,64,68,80,85,81,37,21,85,21,85,21,
85,21,17,85,85,21,21,85,81,81,85,80,81,81,21,21,5,84,21,85,84,84,69,68,
85,84,85,84,84,85,85,85,85,84,145,69,65,69,69,69,80,21,4,69,69,84,68,102,
97,81,85,85,81,86,85,85,85,17,86,37,81,81,85,81,81,85,81,17,17,81,65,17,
85,81,85,85,148,84,69,84,85,85,69,84,84,68,85,73,84,85,85,85,85,85,85,81,
84,84,69,68,69,68,84,85,37,81,33,101,85,21,21,37,85,85,21,81,81,17,21,21,
17,81,17,21,5,65,21,85,1,21,85,69,85,84,84,133,73,85,68,69,88,69,85,85,
85,85,85,85,85,68,68,20,69,69,81,84,20,69,85,21,38,85,81,82,21,81,17,85,
81,81,21,21,20,85,84,5,65,85,85,1,16,81,21,17,0,69,84,69,133,149,68,69,
84,85,84,69,85,21,69,20,85,84,68,69,69,84,85,81,68,1,69,68,4,85,85,85,
18,85,21,97,21,85,85,81,21,81,21,17,17,85,81,81,85,69,85,85,17,17,1,16,
132,149,84,84,84,85,69,85,85,85,85,69,84,85,84,84,69,69,84,69,68,68,85,68,
4,20,5,68,20,38,85,85,85,85,37,85,85,85,21,85,81,81,21,81,85,17,85,17,
17,81,21,81,17,17,21,85,153,105,170,154,153,153,153,85,149,149,165,89,89,153,85,85,
89,149,89,149,85,85,149,165,148,85,85,85,86,102,169,101,86,102,86,102,102,85,89,86,
86,101,86,85,85,102,85,101,86,82,165,81,85,85,102,85,102,154,150,149,149,89,153,153,
154,85,85,105,88,153,85,149,149,89,149,149,149,85,85,85,149,88,84,101,85,89,86,86,
105,85,22,86,86,166,85,85,85,85,101,22,149,101,85,85,85,101,21,85,85,85,169,105,
150,105,89,89,153,153,149,85,101,85,149,89,149,152,153,153,85,149,85,85,90,85,85,85,
85,85,101,85,102,86,154,85,101,102,90,22,86,102,86,102,85,85,85,101,85,85,85,101,
105,85,85,22,85,85,102,154,150,89,101,85,149,89,149,89,105,89,106,137,85,153,85,85,
89,89,133,85,85,84,149,149,68,85,102,101,101,102,146,165,86,105,101,86,86,102,85,85,
86,101,85,85,85,85,101,21,85,85,97,101,101,101,101,149,149,153,166,85,153,85,153,85,
85,85,101,85,85,149,105,165,89,85,85,85,85,85,69,153,85,153,86,101,105,154,101,90,
101,101,102,102,102,101,101,85,102,85,102,85,25,86,102,86,86,85,85,97,86,165,150,153,
89,85,149,89,85,85,89,85,149,149,149,148,85,85,89,149,85,89,85,85,153,84,85,85,
104,86,85,101,86,85,85,101,101,102,101,85,85,85,102,85,85,166,85,102,85,85,102,81,
85,85,17,85,86,149,153,85,89,153,150,85,85,89,105,86,85,85,85,149,149,89,85,149,
89,85,149,85,149,85,84,84,90,102,102,105,85,86,102,89,85,101,85,102,85,102,85,102,
85,86,85,85,102,85,85,102,81,21,86,101,85,153,85,85,154,85,85,153,85,153,85,85,
85,85,154,85,154,85,153,85,153,85,152,85,152,84,152,100,169,101,165,102,102,102,102,102,
102,102,102,102,101,101,101,101,105,85,85,85,85,85,85,86,86,101,85,153,149,85,153,153,
85,85,89,153,85,100,85,85,85,85,72,89,89,89,85,85,85,85,85,85,73,85,85,85,
86,150,102,102,101,101,101,101,102,86,82,85,85,101,90,85,101,86,101,86,101,86,85,101,
18,85,89,165,85,153,86,153,85,105,149,85,85,89,153,153,153,149,89,89,85,85,149,101,
85,85,85,85,133,85,85,101,86,85,85,101,85,101,86,150,85,85,85,85,85,86,102,85,
85,102,85,85,85,85,85,86,86,150,150,89,85,165,150,149,149,85,85,85,153,85,153,85,
85,154,149,149,148,85,89,73,85,85,85,85,149,85,101,85,101,90,21,101,101,86,86,101,
85,89,102,85,85,102,85,85,85,85,101,85,21,21,21,86,85,85,153,85,89,153,153,149,
149,89,85,85,85,85,85,149,133,85,85,85,85,85,85,89,149,149,85,89,85,85,85,85,
101,86,85,85,85,169,101,101,85,85,102,85,85,86,85,86,86,21,85,21,85,85,81,85,
85,85,85,85,85,85,85,89,85,133,85,85,85,153,85,85,89,89,85,85,149,73,85,148,
149,85,69,85,101,101,85,86,85,102,101,89,101,150,149,21,101,86,86,102,85,85,86,37,
81,21,101,85,85,82,85,149,85,85,90,89,85,150,89,85,85,85,101,153,85,89,153,153,
85,84,133,85,85,69,84,149,85,89,149,85,85,85,85,85,85,85,101,85,86,21,85,85,
85,89,150,86,89,85,105,86,86,101,85,101,149,85,85,149,86,85,101,105,85,85,86,85,
85,85,149,21,85,85,85,149,149,149,89,101,89,85,101,85,85,86,101,85,86,101,85,89,
85,89,85,85,85,85,85,85,81,85,85,86,89,85,85,149,85,90,149,86,85,89,85,85,
85,85,85,149,86,85,85,85,89,85,85,85,101,85,101,89,89,86,85,85,106,89,85,85,
149,149,85,85,85,86,85,85,149,85,85,85,85,85,85,85,149,85,85,149,85,85,85,150,
149,101,85,85,85,85,149,101,85,85,85,85,85,149,85,85,85,81,81,85,85,85,85,85,
101,85,149,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,80,85,85,105,
86,85,85,89,85,149,85,89,165,85,105,85,106,85,85,85,85,85,85,85,85,85,85,85,
85,85,85,85,85,85,149,165,86,85,85,85,101,85,149,85,85,85,85,85,85,101,85,89,
85,85,85,85,85,85,85,85,86,86,85,86,85,86,85,86,85,89,85,85,85,85,85,85,
85,85,85,85,85,85,85,81,85,85,149,89,85,85,85,101,85,85,85,85,86,85,85,85,
85,86,89,89,85,85,85,85,85,85,85,85,69,85,85,85,85,89,85,85,85,85,85,85,
85,85,86,85,86,89,85,85,85,85,85,85,85,85,85,85,85,85,85,101,86,85,85,85,
85,86,149,149,85,105,85,85,85,149,85,85,85,85,85,85,89,85,85,85,86,85,86,85,
85,101,85,150,86,85,89,85,85,85,85,85,85,85,85,89,85,85,85,85,85,85,85,85,
85,149,85,85,85,101,85,85,153,85,101,149,85,85,85,85,85,85,85,85,85,85,85,85,
84,85,85,85,85,89,85,85,85,86,86,85,101,85,86,85,85,85,85,86,85,85,149,85,
85,85,85,85,85,85,85,85,149,85,85,85,86,85,86,101,105,85,85,85,85,85,85,101,
89,85,85,85,85,85,85,85,85,85,85,85,85,149,101,86,85,85,85,85,89,101,85,85,
85,85,85,86,149,85,89,85,85,85,85,85,85,85,85,85,85,85,86,85,89,85,85,85,
85,101,85,85,85,85,85,149,85,85,85,85,85,85,85,85,85,85,85,85,85,149,89,85,
85,85,85,85,86,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,
150,85,85,85,85,85,85,85,85,85,89,85,85,89,85,85,85,85,69,85,85,85,85,85,
85,85,85,149,85,89,86,101,105,149,85,149,85,85,85,85,85,85,85,85,85,85,85,85,
85,85,85,85,85,84,101,89,149,85,86,85,149,89,85,101,101,85,89,85,85,86,85,149,
85,85,85,85,85,85,85,85,85,85,85,85,85,149,149,85,85,85,149,85,85,149,85,85,
85,85,85,85,85,85,85,89,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,
85,85,85,85,85,85,85,101,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,
85,85,85,85,149,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,
85,85,85,85,85,85,85,149,85,101,85,85,85,85,85,85,85,85,85,85,85,85,101,85,
85,85,85,85,85,85,85,85,85,85,101,85,85,85,149,85,85,85,85,85,85,85,85,85,
85,85,85,85,85,85,85,85,85,85,85,101,85,85,89,85,85,85,85,85,85,85,85,85,
85,101,85,85,85,89,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,101,85,
85,85,85,85,85,85,89,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,
85,85,85,85,85,85,85,81,85,85,85,85,101,85,85,85,85,85,85,85,85,85,85,85,
101,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,149,85,85,85,
85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,149,85,85,85,85,
85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,69,85,85,85,85,85,
85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,81,85,
85,85,149,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,
85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,
85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,
85,85,85,85,85,85,85,89,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,
85,89,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,
85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,
85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,
85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,
85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,
85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,
85,85,85,85,85,85,85,85,85,85,85,85,85,85,101,85,85,85,85,85,85,85,85,85,
85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,
85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,
85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,
85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,85,1,};
/* entries whose correction is beyond one ulp: index, exact float bits */
static const u32 SIN_EXC[][2] = {{5295, 0x3ef8e592u},{5299, 0x3ef9117eu},{5301, 0x3ef92773u},{5313, 0x3ef9ab25u},{5412, 0x3efde662u},{5416, 0x3efe1207u},{5422, 0x3efe537au},{5424, 0x3efe694au},{65536, 0}};

NO_CONTRACT static float sin_quadrant(u32 r) { /* r in [0, 16384] */
    if (r <= 8192) {
        float x = (float)r * 9.58737992428526e-05f;
        float z = x * x, v = z * x;
        float rr = 8.3333337680e-03f + z * (-1.9841270114e-04f + z * 2.7183114939e-06f);
        return x + v * (-1.6666667163e-01f + z * rr);
    } else {
        float x = (float)(16384 - r) * 9.58737992428526e-05f;
        float z = x * x;
        float rr = z * (4.1666667908e-02f + z * (-1.3888889225e-03f + z * (2.4801587642e-05f + z * -2.7557314297e-07f)));
        float hz = 0.5f * z, w = 1.0f - hz;
        return w + (((1.0f - w) - hz) + z * rr);
    }
}

static float sin_index(u32 i) {
    i &= 65535;
    if (i == 32768) return 1.2246467991473532e-16f;
    u32 q = i >> 14, r = i & 16383;
    if (q & 1) r = 16384 - r;
    float v = sin_quadrant(r);
    int d = (int)((SIN_CORR[r >> 2] >> ((r & 3) * 2)) & 3) - 1;
    if (d == 2) {
        for (int k = 0;; k++)
            if (SIN_EXC[k][0] == r || SIN_EXC[k][0] == 65536) {
                memcpy(&v, &SIN_EXC[k][1], 4);
                break;
            }
    } else if (d) {
        u32 b;
        memcpy(&b, &v, 4);
        b = (u32)((i32)b + d);
        memcpy(&v, &b, 4);
    }
    return (q & 2) ? -v : v;
}

static inline float mh_sin(float f) { return sin_index((u32)(i32)(f * 10430.378f)); }
static inline float mh_cos(float f) { return sin_index((u32)(i32)(f * 10430.378f + 16384.0f)); }

static inline int floor_f(float f) {
    int i = (int)f;
    return f < (float)i ? i - 1 : i;
}

static inline int floor_d(double d) {
    int i = (int)d;
    return d < (double)i ? i - 1 : i;
}

/* ======================================================================== */
/* Biomes                                                                    */
/* ======================================================================== */

enum {
    BI_OCEAN = 0, BI_PLAINS = 1, BI_DESERT = 2, BI_EXTREME_HILLS = 3, BI_FOREST = 4, BI_TAIGA = 5, BI_SWAMP = 6,
    BI_RIVER = 7, BI_FROZEN_OCEAN = 10, BI_FROZEN_RIVER = 11, BI_ICE_PLAINS = 12, BI_ICE_MOUNTAINS = 13,
    BI_MUSHROOM = 14, BI_MUSHROOM_SHORE = 15, BI_BEACH = 16, BI_DESERT_HILLS = 17, BI_FOREST_HILLS = 18,
    BI_TAIGA_HILLS = 19, BI_EH_EDGE = 20, BI_JUNGLE = 21, BI_JUNGLE_HILLS = 22, BI_JUNGLE_EDGE = 23,
    BI_DEEP_OCEAN = 24, BI_STONE_BEACH = 25, BI_COLD_BEACH = 26, BI_BIRCH = 27, BI_BIRCH_HILLS = 28,
    BI_ROOFED = 29, BI_COLD_TAIGA = 30, BI_COLD_TAIGA_HILLS = 31, BI_MEGA_TAIGA = 32, BI_MEGA_TAIGA_HILLS = 33,
    BI_EH_PLUS = 34, BI_SAVANNA = 35, BI_SAVANNA_PLATEAU = 36, BI_MESA = 37, BI_MESA_PLATEAU_F = 38,
    BI_MESA_PLATEAU = 39
};

/* Biome class (getClass(), with BiomeBaseSub reporting its parent's class) */
enum {
    C_OCEAN, C_PLAINS, C_DESERT, C_HILLS, C_FOREST, C_TAIGA, C_SWAMP, C_RIVER, C_ICE, C_MUSHROOM, C_BEACH,
    C_JUNGLE, C_STONEBEACH, C_SAVANNA, C_MESA
};

/* Surface builders */
enum { S_DEFAULT, S_HILLS, S_TAIGA_MEGA, S_SWAMP, S_SAVANNA_M, S_MESA };

/* Decoration (biome .a(World, Random, BlockPosition)) kinds */
enum {
    D_PLAIN,       /* decorator only */
    D_PLAINS,      /* BiomePlains */
    D_DESERT,      /* BiomeDesert (well) */
    D_HILLS,       /* BiomeBigHills (emerald, silverfish) */
    D_FOREST,      /* BiomeForest */
    D_TAIGA,       /* BiomeTaiga */
    D_ICE,         /* BiomeIcePlains */
    D_JUNGLE,      /* BiomeJungle (melon, vine draws) */
    D_SAVANNA,     /* BiomeSavanna (double grass) */
    D_ROOFED_SUB   /* Roofed Forest M: parent's BiomeForest.a with the parent biome */
};

/* Tree selector kinds (biome.a(Random)) */
enum {
    T_DEFAULT, T_HILLS, T_FOREST0, T_FOREST1, T_BIRCH, T_ROOFED, T_BIRCH_SUB, T_TAIGA0, T_TAIGA1, T_TAIGA2,
    T_SWAMP, T_ICE, T_JUNGLE, T_JUNGLE_EDGE, T_SAVANNA, T_MESA
};

/* Flower selector kinds; grass selector kinds */
enum { F_DEFAULT, F_PLAINS, F_FLOWER_FOREST, F_SWAMP };
enum { G_DEFAULT, G_TAIGA, G_JUNGLE };

typedef struct {
    uint8_t id;
    uint8_t cls, surf, deco, tree, flower, grass;
    uint8_t snowy;   /* ax: BiomeBase.c() */
    uint8_t temp_cat; /* 0 ocean 1 cold 2 medium 3 warm */
    uint8_t top, filler;
    uint8_t mode;    /* hills: aI; taiga: aI; forest: aG; mesa: bit0 bryce, bit1 plateau F; plains: sunflower; ice: spikes; jungle: edge */
    float depth, scale, temp;
    /* decorator: A trees, B flowers, C grass, D deadbush, E mushrooms, F reeds, G cactus, H gravel,
     * I sand, J clay, K big mushrooms, Z lily pads */
    int16_t dA;
    int8_t dB, dC, dD, dE, dF, dG, dH, dI, dJ, dK, dZ;
    u32 color;
} Biome;

#define DEC(A, B, C, D, E, F, G, H, I, J, K, Z) A, B, C, D, E, F, G, H, I, J, K, Z
#define DDEF DEC(0, 2, 1, 0, 0, 0, 0, 1, 3, 1, 0, 0)

static const Biome BIOMES[] = {
    {0, C_OCEAN, S_DEFAULT, D_PLAIN, T_DEFAULT, F_DEFAULT, G_DEFAULT, 0, 0, B_GRASS, B_DIRT, 0, -1.0f, 0.1f, 0.5f, DDEF, 0x000070},
    {1, C_PLAINS, S_DEFAULT, D_PLAINS, T_DEFAULT, F_PLAINS, G_DEFAULT, 0, 2, B_GRASS, B_DIRT, 0, 0.125f, 0.05f, 0.8f, DEC(-999, 4, 10, 0, 0, 0, 0, 1, 3, 1, 0, 0), 0x8DB360},
    {2, C_DESERT, S_DEFAULT, D_DESERT, T_DEFAULT, F_DEFAULT, G_DEFAULT, 0, 3, B_SAND, B_SAND, 0, 0.125f, 0.05f, 2.0f, DEC(-999, 2, 1, 2, 0, 50, 10, 1, 3, 1, 0, 0), 0xFA9418},
    {3, C_HILLS, S_HILLS, D_HILLS, T_HILLS, F_DEFAULT, G_DEFAULT, 0, 2, B_GRASS, B_DIRT, 0, 1.0f, 0.5f, 0.2f, DDEF, 0x606060},
    {4, C_FOREST, S_DEFAULT, D_FOREST, T_FOREST0, F_DEFAULT, G_DEFAULT, 0, 2, B_GRASS, B_DIRT, 0, 0.1f, 0.2f, 0.7f, DEC(10, 2, 2, 0, 0, 0, 0, 1, 3, 1, 0, 0), 0x056621},
    {5, C_TAIGA, S_DEFAULT, D_TAIGA, T_TAIGA0, F_DEFAULT, G_TAIGA, 0, 2, B_GRASS, B_DIRT, 0, 0.2f, 0.2f, 0.25f, DEC(10, 2, 1, 0, 1, 0, 0, 1, 3, 1, 0, 0), 0x0B6659},
    {6, C_SWAMP, S_SWAMP, D_PLAIN, T_SWAMP, F_SWAMP, G_DEFAULT, 0, 2, B_GRASS, B_DIRT, 0, -0.2f, 0.1f, 0.8f, DEC(2, 1, 5, 1, 8, 10, 0, 0, 0, 1, 0, 4), 0x07F9B2},
    {7, C_RIVER, S_DEFAULT, D_PLAIN, T_DEFAULT, F_DEFAULT, G_DEFAULT, 0, 2, B_GRASS, B_DIRT, 0, -0.5f, 0.0f, 0.5f, DDEF, 0x0000FF},
    {10, C_OCEAN, S_DEFAULT, D_PLAIN, T_DEFAULT, F_DEFAULT, G_DEFAULT, 1, 0, B_GRASS, B_DIRT, 0, -1.0f, 0.1f, 0.0f, DDEF, 0x9090A0},
    {11, C_RIVER, S_DEFAULT, D_PLAIN, T_DEFAULT, F_DEFAULT, G_DEFAULT, 1, 1, B_GRASS, B_DIRT, 0, -0.5f, 0.0f, 0.0f, DDEF, 0xA0A0FF},
    {12, C_ICE, S_DEFAULT, D_ICE, T_ICE, F_DEFAULT, G_DEFAULT, 1, 1, B_GRASS, B_DIRT, 0, 0.125f, 0.05f, 0.0f, DDEF, 0xFFFFFF},
    {13, C_ICE, S_DEFAULT, D_ICE, T_ICE, F_DEFAULT, G_DEFAULT, 1, 1, B_GRASS, B_DIRT, 0, 0.45f, 0.3f, 0.0f, DDEF, 0xA0A0A0},
    {14, C_MUSHROOM, S_DEFAULT, D_PLAIN, T_DEFAULT, F_DEFAULT, G_DEFAULT, 0, 2, B_MYCELIUM, B_DIRT, 0, 0.2f, 0.3f, 0.9f, DEC(-100, -100, -100, 0, 1, 0, 0, 1, 3, 1, 1, 0), 0xFF00FF},
    {15, C_MUSHROOM, S_DEFAULT, D_PLAIN, T_DEFAULT, F_DEFAULT, G_DEFAULT, 0, 2, B_MYCELIUM, B_DIRT, 0, 0.0f, 0.025f, 0.9f, DEC(-100, -100, -100, 0, 1, 0, 0, 1, 3, 1, 1, 0), 0xA000FF},
    {16, C_BEACH, S_DEFAULT, D_PLAIN, T_DEFAULT, F_DEFAULT, G_DEFAULT, 0, 2, B_SAND, B_SAND, 0, 0.0f, 0.025f, 0.8f, DEC(-999, 2, 1, 0, 0, 0, 0, 1, 3, 1, 0, 0), 0xFADE55},
    {17, C_DESERT, S_DEFAULT, D_DESERT, T_DEFAULT, F_DEFAULT, G_DEFAULT, 0, 3, B_SAND, B_SAND, 0, 0.45f, 0.3f, 2.0f, DEC(-999, 2, 1, 2, 0, 50, 10, 1, 3, 1, 0, 0), 0xD25F12},
    {18, C_FOREST, S_DEFAULT, D_FOREST, T_FOREST0, F_DEFAULT, G_DEFAULT, 0, 2, B_GRASS, B_DIRT, 0, 0.45f, 0.3f, 0.7f, DEC(10, 2, 2, 0, 0, 0, 0, 1, 3, 1, 0, 0), 0x22551C},
    {19, C_TAIGA, S_DEFAULT, D_TAIGA, T_TAIGA0, F_DEFAULT, G_TAIGA, 0, 2, B_GRASS, B_DIRT, 0, 0.45f, 0.3f, 0.25f, DEC(10, 2, 1, 0, 1, 0, 0, 1, 3, 1, 0, 0), 0x163933},
    {20, C_HILLS, S_HILLS, D_HILLS, T_HILLS, F_DEFAULT, G_DEFAULT, 0, 2, B_GRASS, B_DIRT, 1, 0.8f, 0.3f, 0.2f, DEC(3, 2, 1, 0, 0, 0, 0, 1, 3, 1, 0, 0), 0x72789A},
    {21, C_JUNGLE, S_DEFAULT, D_JUNGLE, T_JUNGLE, F_DEFAULT, G_JUNGLE, 0, 2, B_GRASS, B_DIRT, 0, 0.1f, 0.2f, 0.95f, DEC(50, 4, 25, 0, 0, 0, 0, 1, 3, 1, 0, 0), 0x537B09},
    {22, C_JUNGLE, S_DEFAULT, D_JUNGLE, T_JUNGLE, F_DEFAULT, G_JUNGLE, 0, 2, B_GRASS, B_DIRT, 0, 0.45f, 0.3f, 0.95f, DEC(50, 4, 25, 0, 0, 0, 0, 1, 3, 1, 0, 0), 0x2C4205},
    {23, C_JUNGLE, S_DEFAULT, D_JUNGLE, T_JUNGLE_EDGE, F_DEFAULT, G_JUNGLE, 0, 2, B_GRASS, B_DIRT, 1, 0.1f, 0.2f, 0.95f, DEC(2, 4, 25, 0, 0, 0, 0, 1, 3, 1, 0, 0), 0x628B17},
    {24, C_OCEAN, S_DEFAULT, D_PLAIN, T_DEFAULT, F_DEFAULT, G_DEFAULT, 0, 0, B_GRASS, B_DIRT, 0, -1.8f, 0.1f, 0.5f, DDEF, 0x000030},
    {25, C_STONEBEACH, S_DEFAULT, D_PLAIN, T_DEFAULT, F_DEFAULT, G_DEFAULT, 0, 2, B_STONE, B_STONE, 0, 0.1f, 0.8f, 0.2f, DEC(-999, 2, 1, 0, 0, 0, 0, 1, 3, 1, 0, 0), 0xA2A284},
    {26, C_BEACH, S_DEFAULT, D_PLAIN, T_DEFAULT, F_DEFAULT, G_DEFAULT, 1, 1, B_SAND, B_SAND, 0, 0.0f, 0.025f, 0.05f, DEC(-999, 2, 1, 0, 0, 0, 0, 1, 3, 1, 0, 0), 0xFAF0C0},
    {27, C_FOREST, S_DEFAULT, D_FOREST, T_BIRCH, F_DEFAULT, G_DEFAULT, 0, 2, B_GRASS, B_DIRT, 2, 0.1f, 0.2f, 0.6f, DEC(10, 2, 2, 0, 0, 0, 0, 1, 3, 1, 0, 0), 0x307444},
    {28, C_FOREST, S_DEFAULT, D_FOREST, T_BIRCH, F_DEFAULT, G_DEFAULT, 0, 2, B_GRASS, B_DIRT, 2, 0.45f, 0.3f, 0.6f, DEC(10, 2, 2, 0, 0, 0, 0, 1, 3, 1, 0, 0), 0x1F5F32},
    {29, C_FOREST, S_DEFAULT, D_FOREST, T_ROOFED, F_DEFAULT, G_DEFAULT, 0, 2, B_GRASS, B_DIRT, 3, 0.1f, 0.2f, 0.7f, DEC(-999, 2, 2, 0, 0, 0, 0, 1, 3, 1, 0, 0), 0x40511A},
    {30, C_TAIGA, S_DEFAULT, D_TAIGA, T_TAIGA0, F_DEFAULT, G_TAIGA, 1, 1, B_GRASS, B_DIRT, 0, 0.2f, 0.2f, -0.5f, DEC(10, 2, 1, 0, 1, 0, 0, 1, 3, 1, 0, 0), 0x31554A},
    {31, C_TAIGA, S_DEFAULT, D_TAIGA, T_TAIGA0, F_DEFAULT, G_TAIGA, 1, 1, B_GRASS, B_DIRT, 0, 0.45f, 0.3f, -0.5f, DEC(10, 2, 1, 0, 1, 0, 0, 1, 3, 1, 0, 0), 0x243F36},
    {32, C_TAIGA, S_TAIGA_MEGA, D_TAIGA, T_TAIGA1, F_DEFAULT, G_TAIGA, 0, 2, B_GRASS, B_DIRT, 1, 0.2f, 0.2f, 0.3f, DEC(10, 2, 7, 1, 3, 0, 0, 1, 3, 1, 0, 0), 0x596651},
    {33, C_TAIGA, S_TAIGA_MEGA, D_TAIGA, T_TAIGA1, F_DEFAULT, G_TAIGA, 0, 2, B_GRASS, B_DIRT, 1, 0.45f, 0.3f, 0.3f, DEC(10, 2, 7, 1, 3, 0, 0, 1, 3, 1, 0, 0), 0x454F3E},
    {34, C_HILLS, S_HILLS, D_HILLS, T_HILLS, F_DEFAULT, G_DEFAULT, 0, 2, B_GRASS, B_DIRT, 1, 1.0f, 0.5f, 0.2f, DEC(3, 2, 1, 0, 0, 0, 0, 1, 3, 1, 0, 0), 0x507050},
    {35, C_SAVANNA, S_DEFAULT, D_SAVANNA, T_SAVANNA, F_DEFAULT, G_DEFAULT, 0, 3, B_GRASS, B_DIRT, 0, 0.125f, 0.05f, 1.2f, DEC(1, 4, 20, 0, 0, 0, 0, 1, 3, 1, 0, 0), 0xBDB25F},
    {36, C_SAVANNA, S_DEFAULT, D_SAVANNA, T_SAVANNA, F_DEFAULT, G_DEFAULT, 0, 3, B_GRASS, B_DIRT, 0, 1.5f, 0.025f, 1.0f, DEC(1, 4, 20, 0, 0, 0, 0, 1, 3, 1, 0, 0), 0xA79D64},
    {37, C_MESA, S_MESA, D_PLAIN, T_MESA, F_DEFAULT, G_DEFAULT, 0, 3, B_RED_SAND, B_STAINED_CLAY_WHITE, 0, 0.1f, 0.2f, 2.0f, DEC(-999, 0, 1, 20, 0, 3, 5, 1, 3, 1, 0, 0), 0xD94515},
    {38, C_MESA, S_MESA, D_PLAIN, T_MESA, F_DEFAULT, G_DEFAULT, 0, 3, B_RED_SAND, B_STAINED_CLAY_WHITE, 2, 1.5f, 0.025f, 2.0f, DEC(5, 0, 1, 20, 0, 3, 5, 1, 3, 1, 0, 0), 0xB09765},
    {39, C_MESA, S_MESA, D_PLAIN, T_MESA, F_DEFAULT, G_DEFAULT, 0, 3, B_RED_SAND, B_STAINED_CLAY_WHITE, 0, 1.5f, 0.025f, 2.0f, DEC(-999, 0, 1, 20, 0, 3, 5, 1, 3, 1, 0, 0), 0xCA8C65},
    /* mutated variants */
    {129, C_PLAINS, S_DEFAULT, D_PLAINS, T_DEFAULT, F_PLAINS, G_DEFAULT, 0, 2, B_GRASS, B_DIRT, 1, 0.125f, 0.05f, 0.8f, DEC(-999, 4, 10, 0, 0, 0, 0, 1, 3, 1, 0, 0), 0xB5DB88},
    {130, C_DESERT, S_DEFAULT, D_PLAIN, T_DEFAULT, F_DEFAULT, G_DEFAULT, 0, 3, B_SAND, B_SAND, 0, 0.225f, 0.25f, 2.0f, DEC(-999, 2, 1, 2, 0, 50, 10, 1, 3, 1, 0, 0), 0xFFBC40},
    {131, C_HILLS, S_HILLS, D_HILLS, T_HILLS, F_DEFAULT, G_DEFAULT, 0, 2, B_GRASS, B_DIRT, 2, 1.0f, 0.5f, 0.2f, DDEF, 0x888888},
    {132, C_FOREST, S_DEFAULT, D_FOREST, T_FOREST1, F_FLOWER_FOREST, G_DEFAULT, 0, 2, B_GRASS, B_DIRT, 1, 0.1f, 0.4f, 0.7f, DEC(6, 100, 1, 0, 0, 0, 0, 1, 3, 1, 0, 0), 0x6A7425},
    {133, C_TAIGA, S_DEFAULT, D_PLAIN, T_TAIGA0, F_DEFAULT, G_DEFAULT, 0, 2, B_GRASS, B_DIRT, 0, 0.3f, 0.4f, 0.25f, DEC(10, 2, 1, 0, 1, 0, 0, 1, 3, 1, 0, 0), 0x338E81},
    {134, C_SWAMP, S_SWAMP, D_PLAIN, T_SWAMP, F_DEFAULT, G_DEFAULT, 0, 2, B_GRASS, B_DIRT, 0, -0.1f, 0.3f, 0.8f, DEC(2, 1, 5, 1, 8, 10, 0, 0, 0, 1, 0, 4), 0x2FFFDA},
    {140, C_ICE, S_DEFAULT, D_ICE, T_ICE, F_DEFAULT, G_DEFAULT, 1, 1, B_SNOW, B_DIRT, 1, 0.425f, 0.45f, 0.0f, DDEF, 0xB4DCDC},
    {149, C_JUNGLE, S_DEFAULT, D_PLAIN, T_JUNGLE, F_DEFAULT, G_DEFAULT, 0, 2, B_GRASS, B_DIRT, 0, 0.2f, 0.4f, 0.95f, DEC(50, 4, 25, 0, 0, 0, 0, 1, 3, 1, 0, 0), 0x7BA331},
    {151, C_JUNGLE, S_DEFAULT, D_PLAIN, T_JUNGLE_EDGE, F_DEFAULT, G_DEFAULT, 0, 2, B_GRASS, B_DIRT, 1, 0.2f, 0.4f, 0.95f, DEC(2, 4, 25, 0, 0, 0, 0, 1, 3, 1, 0, 0), 0x8AB33F},
    {155, C_FOREST, S_DEFAULT, D_PLAIN, T_BIRCH_SUB, F_DEFAULT, G_DEFAULT, 0, 2, B_GRASS, B_DIRT, 2, 0.2f, 0.4f, 0.6f, DEC(10, 2, 2, 0, 0, 0, 0, 1, 3, 1, 0, 0), 0x589C6C},
    {156, C_FOREST, S_DEFAULT, D_PLAIN, T_BIRCH_SUB, F_DEFAULT, G_DEFAULT, 0, 2, B_GRASS, B_DIRT, 2, 0.55f, 0.5f, 0.6f, DEC(10, 2, 2, 0, 0, 0, 0, 1, 3, 1, 0, 0), 0x47875A},
    {157, C_FOREST, S_DEFAULT, D_ROOFED_SUB, T_ROOFED, F_DEFAULT, G_DEFAULT, 0, 2, B_GRASS, B_DIRT, 3, 0.2f, 0.4f, 0.7f, DEC(-999, 2, 2, 0, 0, 0, 0, 1, 3, 1, 0, 0), 0x687942},
    {158, C_TAIGA, S_DEFAULT, D_PLAIN, T_TAIGA0, F_DEFAULT, G_DEFAULT, 1, 1, B_GRASS, B_DIRT, 0, 0.3f, 0.4f, -0.5f, DEC(10, 2, 1, 0, 1, 0, 0, 1, 3, 1, 0, 0), 0x597D72},
    {160, C_TAIGA, S_TAIGA_MEGA, D_TAIGA, T_TAIGA2, F_DEFAULT, G_TAIGA, 0, 2, B_GRASS, B_DIRT, 2, 0.2f, 0.2f, 0.25f, DEC(10, 2, 7, 1, 3, 0, 0, 1, 3, 1, 0, 0), 0x818E79},
    {161, C_TAIGA, S_TAIGA_MEGA, D_TAIGA, T_TAIGA2, F_DEFAULT, G_TAIGA, 0, 2, B_GRASS, B_DIRT, 2, 0.2f, 0.2f, 0.25f, DEC(10, 2, 7, 1, 3, 0, 0, 1, 3, 1, 0, 0), 0x6D7766},
    {162, C_HILLS, S_HILLS, D_HILLS, T_HILLS, F_DEFAULT, G_DEFAULT, 0, 2, B_GRASS, B_DIRT, 2, 1.0f, 0.5f, 0.2f, DDEF, 0x789878},
    {163, C_SAVANNA, S_SAVANNA_M, D_PLAIN, T_SAVANNA, F_DEFAULT, G_DEFAULT, 0, 3, B_GRASS, B_DIRT, 0, 0.3625f, 1.225f, 1.1f, DEC(2, 2, 5, 0, 0, 0, 0, 1, 3, 1, 0, 0), 0xE5DA87},
    {164, C_SAVANNA, S_SAVANNA_M, D_PLAIN, T_SAVANNA, F_DEFAULT, G_DEFAULT, 0, 2, B_GRASS, B_DIRT, 0, 1.05f, 1.2125f, 1.0f, DEC(2, 2, 5, 0, 0, 0, 0, 1, 3, 1, 0, 0), 0xCFC58C},
    {165, C_MESA, S_MESA, D_PLAIN, T_MESA, F_DEFAULT, G_DEFAULT, 0, 3, B_RED_SAND, B_STAINED_CLAY_WHITE, 1, 0.1f, 0.2f, 2.0f, DEC(-999, 0, 1, 20, 0, 3, 5, 1, 3, 1, 0, 0), 0xFF6D3D},
    {166, C_MESA, S_MESA, D_PLAIN, T_MESA, F_DEFAULT, G_DEFAULT, 0, 3, B_RED_SAND, B_STAINED_CLAY_WHITE, 2, 0.45f, 0.3f, 2.0f, DEC(5, 0, 1, 20, 0, 3, 5, 1, 3, 1, 0, 0), 0xD8BF8D},
    {167, C_MESA, S_MESA, D_PLAIN, T_MESA, F_DEFAULT, G_DEFAULT, 0, 3, B_RED_SAND, B_STAINED_CLAY_WHITE, 0, 0.45f, 0.3f, 2.0f, DEC(-999, 0, 1, 20, 0, 3, 5, 1, 3, 1, 0, 0), 0xF2B48D},
};
#define NBIOMES ((int)(sizeof(BIOMES) / sizeof(BIOMES[0])))

static uint8_t biome_index[256]; /* id -> index in BIOMES, 255 = none */

static const Biome *bio(int id) {
    if (id < 0 || id > 255 || biome_index[id] == 255) return &BIOMES[0]; /* BiomeBase.getBiome(id, OCEAN) */
    return &BIOMES[biome_index[id]];
}

static int bio_exists(int id) { return id >= 0 && id < 256 && biome_index[id] != 255; }

/* GenLayer.a(int, int): same biome "kind" */
static int bio_same(int i, int j) {
    if (i == j) return 1;
    if (i == BI_MESA_PLATEAU_F || i == BI_MESA_PLATEAU) return j == BI_MESA_PLATEAU_F || j == BI_MESA_PLATEAU;
    if (!bio_exists(i) || !bio_exists(j)) return 0;
    return bio(i)->cls == bio(j)->cls;
}

static int is_ocean_id(int i) { return i == BI_OCEAN || i == BI_DEEP_OCEAN || i == BI_FROZEN_OCEAN; }

/* ======================================================================== */
/* GenLayer stack                                                            */
/* ======================================================================== */

#define LMUL 6364136223846793005ULL
#define LADD 1442695040888963407ULL

enum {
    L_ISLAND0, L_ZOOM, L_FUZZY, L_ISLAND, L_ICEPLAINS, L_TOPSOIL, L_SPEC1, L_SPEC2, L_SPEC3, L_MUSHISLAND,
    L_DEEPOCEAN, L_CLEANER, L_BIOME, L_DESERT, L_HILLS, L_PLAINS, L_MUSHSHORE, L_RIVER, L_SMOOTH
};

typedef struct {
    uint8_t type;
    int16_t seed;
    uint8_t noworld; /* never initialised with the world seed (c stays 0) */
} LayerDef;

static const LayerDef LBASE[] = {
    {L_ISLAND0, 1, 0},    {L_FUZZY, 2000, 0},  {L_ISLAND, 1, 0},     {L_ZOOM, 2001, 0},  {L_ISLAND, 2, 0},
    {L_ISLAND, 50, 0},    {L_ISLAND, 70, 0},   {L_ICEPLAINS, 2, 0},  {L_TOPSOIL, 2, 0},  {L_ISLAND, 3, 0},
    {L_SPEC1, 2, 0},      {L_SPEC2, 2, 0},     {L_SPEC3, 3, 0},      {L_ZOOM, 2002, 0},  {L_ZOOM, 2003, 0},
    {L_ISLAND, 4, 0},     {L_MUSHISLAND, 5, 0}, {L_DEEPOCEAN, 4, 0},
};
#define NBASE 18
/* the river noise used by GenLayerRegionHills: its two zoom layers are never seeded with the world seed */
static const LayerDef LHILLSRIVER[] = {{L_CLEANER, 100, 0}, {L_ZOOM, 1000, 1}, {L_ZOOM, 1001, 1}};
static const LayerDef LRIVER[] = {{L_CLEANER, 100, 0}, {L_ZOOM, 1000, 0}, {L_ZOOM, 1001, 0}, {L_ZOOM, 1000, 0},
                                  {L_ZOOM, 1001, 0},   {L_ZOOM, 1002, 0}, {L_ZOOM, 1003, 0}, {L_RIVER, 1, 0},
                                  {L_SMOOTH, 1000, 0}};
static const LayerDef LBIOME1[] = {{L_BIOME, 200, 0}, {L_ZOOM, 1000, 0}, {L_ZOOM, 1001, 0}, {L_DESERT, 1000, 0}};
static const LayerDef LBIOME2[] = {{L_HILLS, 1000, 0}, {L_PLAINS, 1001, 0}, {L_ZOOM, 1000, 0},
                                   {L_ISLAND, 3, 0},   {L_ZOOM, 1001, 0},   {L_MUSHSHORE, 1000, 0},
                                   {L_ZOOM, 1002, 0},  {L_ZOOM, 1003, 0},   {L_SMOOTH, 1000, 0}};

static i64 g_seed;
static i64 lay_wseed[19 + 32]; /* world-seeded "c" per distinct layer seed, see layer_c() */

static i64 layer_base(i64 s) {
    u64 b = (u64)s;
    for (int i = 0; i < 3; i++) {
        b *= b * LMUL + LADD;
        b += (u64)s;
    }
    return (i64)b;
}

static i64 layer_world(i64 s) {
    u64 b = (u64)layer_base(s);
    u64 c = (u64)g_seed;
    for (int i = 0; i < 3; i++) {
        c *= c * LMUL + LADD;
        c += b;
    }
    return (i64)c;
}

/* cache of world-seeded c by layer seed (the seeds used are few) */
static const int16_t LSEEDS[] = {1, 2, 3, 4, 5, 10, 50, 70, 100, 200, 1000, 1001, 1002, 1003, 2000, 2001, 2002, 2003};
#define NLSEEDS 18

static i64 layer_c(int seed, int noworld) {
    if (noworld) return 0;
    for (int i = 0; i < NLSEEDS; i++)
        if (LSEEDS[i] == seed) return lay_wseed[i];
    return layer_world(seed);
}

/* layer RNG state */
typedef struct {
    i64 c, d;
} LRand;

static inline void lr_chunk(LRand *r, i64 x, i64 z) {
    u64 d = (u64)r->c;
    d *= d * LMUL + LADD;
    d += (u64)x;
    d *= d * LMUL + LADD;
    d += (u64)z;
    d *= d * LMUL + LADD;
    d += (u64)x;
    d *= d * LMUL + LADD;
    d += (u64)z;
    r->d = (i64)d;
}

static inline int lr_int(LRand *r, int n) {
    int j = (int)((r->d >> 24) % (i64)n);
    if (j < 0) j += n;
    u64 d = (u64)r->d;
    d *= d * LMUL + LADD;
    d += (u64)r->c;
    r->d = (i64)d;
    return j;
}

static inline int sel_mode(LRand *r, int i, int j, int k, int l) {
    if (j == k && k == l) return j;
    if (i == j && i == k) return i;
    if (i == j && i == l) return i;
    if (i == k && i == l) return i;
    if (i == j && k != l) return i;
    if (i == k && j != l) return i;
    if (i == l && j != k) return i;
    if (j == k && i != l) return j;
    if (j == l && i != k) return j;
    if (k == l && i != j) return k;
    int a[4] = {i, j, k, l};
    return a[lr_int(r, 4)];
}

/* Layer scratch: one bump-allocated int32 area */
static i32 *lay_mem; /* points into the shared scratch union */
static int lay_top, lay_peak, lay_cap;

static i32 *lay_alloc(int n) {
    i32 *p = lay_mem + lay_top;
    lay_top += n;
    if (lay_top > lay_peak) lay_peak = lay_top;
    if (lay_top > lay_cap) {
        /* should not happen with the request sizes used; clamp to avoid corruption */
        lay_top = lay_cap;
        return lay_mem + lay_cap - n;
    }
    return p;
}

/* parent window of a layer for an output window */
static void layer_parent_win(int type, int x, int z, int w, int h, int *px, int *pz, int *pw, int *ph) {
    switch (type) {
    case L_ZOOM:
    case L_FUZZY:
        *px = x >> 1;
        *pz = z >> 1;
        *pw = (w >> 1) + 2;
        *ph = (h >> 1) + 2;
        break;
    case L_SPEC3:
    case L_CLEANER:
    case L_BIOME:
        *px = x;
        *pz = z;
        *pw = w;
        *ph = h;
        break;
    default:
        *px = x - 1;
        *pz = z - 1;
        *pw = w + 2;
        *ph = h + 2;
        break;
    }
}

/* Applies one layer: in = parent window (px,pz,pw,ph), out = (x,z,w,h). in2 is the second
 * input (hills river noise, same window as in). */
static void layer_apply(const LayerDef *L, const i32 *in, const i32 *in2, i32 *out, int x, int z, int w, int h) {
    LRand r;
    r.c = layer_c(L->seed, L->noworld);
    int pw = w + 2;
    switch (L->type) {
    case L_ISLAND0:
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++) {
                lr_chunk(&r, x + i, z + j);
                out[i + j * w] = lr_int(&r, 10) == 0 ? 1 : 0;
            }
        if (x > -w && x <= 0 && z > -h && z <= 0) out[-x + -z * w] = 1;
        break;
    case L_ZOOM:
    case L_FUZZY: {
        int px = x >> 1, pz = z >> 1, ppw = (w >> 1) + 2;
        int cx0 = x >> 1, cx1 = (x + w - 1) >> 1, cz0 = z >> 1, cz1 = (z + h - 1) >> 1;
        for (int cz = cz0; cz <= cz1; cz++)
            for (int cx = cx0; cx <= cx1; cx++) {
                int ix = cx - px, iz = cz - pz;
                int a = in[ix + iz * ppw], b = in[ix + (iz + 1) * ppw];
                int c = in[ix + 1 + iz * ppw], d = in[ix + 1 + (iz + 1) * ppw];
                lr_chunk(&r, (i64)cx << 1, (i64)cz << 1);
                int v00 = a;
                int v01 = lr_int(&r, 2) ? b : a;
                int v10 = lr_int(&r, 2) ? c : a;
                int v11;
                if (L->type == L_FUZZY) {
                    int arr[4] = {a, c, b, d};
                    v11 = arr[lr_int(&r, 4)];
                } else {
                    v11 = sel_mode(&r, a, c, b, d);
                }
                int bx = (cx << 1) - x, bz = (cz << 1) - z;
                if (bx >= 0 && bz >= 0) out[bx + bz * w] = v00;
                if (bx >= 0 && bz + 1 < h) out[bx + (bz + 1) * w] = v01;
                if (bx + 1 < w && bz >= 0) out[bx + 1 + bz * w] = v10;
                if (bx + 1 < w && bz + 1 < h) out[bx + 1 + (bz + 1) * w] = v11;
            }
        break;
    }
    case L_ISLAND:
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++) {
                int a = in[i + j * pw], b = in[i + 2 + j * pw], c = in[i + (j + 2) * pw], d = in[i + 2 + (j + 2) * pw];
                int k = in[i + 1 + (j + 1) * pw];
                lr_chunk(&r, x + i, z + j);
                int v;
                if (k == 0 && (a != 0 || b != 0 || c != 0 || d != 0)) {
                    int l3 = 1, i4 = 1;
                    if (a != 0 && lr_int(&r, l3++) == 0) i4 = a;
                    if (b != 0 && lr_int(&r, l3++) == 0) i4 = b;
                    if (c != 0 && lr_int(&r, l3++) == 0) i4 = c;
                    if (d != 0 && lr_int(&r, l3++) == 0) i4 = d;
                    if (lr_int(&r, 3) == 0) v = i4;
                    else if (i4 == 4) v = 4;
                    else v = 0;
                } else if (k > 0 && (a == 0 || b == 0 || c == 0 || d == 0)) {
                    if (lr_int(&r, 5) == 0) v = k == 4 ? 4 : 0;
                    else v = k;
                } else {
                    v = k;
                }
                out[i + j * w] = v;
            }
        break;
    case L_ICEPLAINS:
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++) {
                int n = in[i + 1 + j * pw], e = in[i + 2 + (j + 1) * pw], ww = in[i + (j + 1) * pw];
                int s = in[i + 1 + (j + 2) * pw], k = in[i + 1 + (j + 1) * pw];
                out[i + j * w] = k;
                lr_chunk(&r, x + i, z + j);
                if (k == 0 && n == 0 && e == 0 && ww == 0 && s == 0 && lr_int(&r, 2) == 0) out[i + j * w] = 1;
            }
        break;
    case L_TOPSOIL:
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++) {
                int k = in[i + 1 + (j + 1) * pw];
                lr_chunk(&r, x + i, z + j);
                if (k == 0) {
                    out[i + j * w] = 0;
                } else {
                    int l = lr_int(&r, 6);
                    out[i + j * w] = l == 0 ? 4 : (l <= 1 ? 3 : 1);
                }
            }
        break;
    case L_SPEC1:
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++) {
                lr_chunk(&r, x + i, z + j);
                int k = in[i + 1 + (j + 1) * pw];
                if (k == 1) {
                    int n = in[i + 1 + j * pw], e = in[i + 2 + (j + 1) * pw], ww = in[i + (j + 1) * pw],
                        s = in[i + 1 + (j + 2) * pw];
                    if (n == 3 || e == 3 || ww == 3 || s == 3 || n == 4 || e == 4 || ww == 4 || s == 4) k = 2;
                }
                out[i + j * w] = k;
            }
        break;
    case L_SPEC2:
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++) {
                int k = in[i + 1 + (j + 1) * pw];
                if (k == 4) {
                    int n = in[i + 1 + j * pw], e = in[i + 2 + (j + 1) * pw], ww = in[i + (j + 1) * pw],
                        s = in[i + 1 + (j + 2) * pw];
                    if (n == 2 || e == 2 || ww == 2 || s == 2 || n == 1 || e == 1 || ww == 1 || s == 1) k = 3;
                }
                out[i + j * w] = k;
            }
        break;
    case L_SPEC3:
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++) {
                lr_chunk(&r, x + i, z + j);
                int k = in[i + j * w];
                if (k != 0 && lr_int(&r, 13) == 0) k |= (1 + lr_int(&r, 15)) << 8 & 3840;
                out[i + j * w] = k;
            }
        break;
    case L_MUSHISLAND:
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++) {
                int a = in[i + j * pw], b = in[i + 2 + j * pw], c = in[i + (j + 2) * pw], d = in[i + 2 + (j + 2) * pw];
                int k = in[i + 1 + (j + 1) * pw];
                lr_chunk(&r, x + i, z + j);
                if (k == 0 && a == 0 && b == 0 && c == 0 && d == 0 && lr_int(&r, 100) == 0) out[i + j * w] = BI_MUSHROOM;
                else out[i + j * w] = k;
            }
        break;
    case L_DEEPOCEAN:
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++) {
                int n = in[i + 1 + j * pw], e = in[i + 2 + (j + 1) * pw], ww = in[i + (j + 1) * pw],
                    s = in[i + 1 + (j + 2) * pw], k = in[i + 1 + (j + 1) * pw];
                int c = (n == 0) + (e == 0) + (ww == 0) + (s == 0);
                out[i + j * w] = (k == 0 && c > 3) ? BI_DEEP_OCEAN : k;
            }
        break;
    case L_CLEANER:
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++) {
                lr_chunk(&r, x + i, z + j);
                out[i + j * w] = in[i + j * w] > 0 ? lr_int(&r, 299999) + 2 : 0;
            }
        break;
    case L_BIOME: {
        static const uint8_t warm[6] = {BI_DESERT, BI_DESERT, BI_DESERT, BI_SAVANNA, BI_SAVANNA, BI_PLAINS};
        static const uint8_t medium[6] = {BI_FOREST, BI_ROOFED, BI_EXTREME_HILLS, BI_PLAINS, BI_BIRCH, BI_SWAMP};
        static const uint8_t cold[4] = {BI_FOREST, BI_EXTREME_HILLS, BI_TAIGA, BI_PLAINS};
        static const uint8_t ice[4] = {BI_ICE_PLAINS, BI_ICE_PLAINS, BI_ICE_PLAINS, BI_COLD_TAIGA};
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++) {
                lr_chunk(&r, x + i, z + j);
                int k = in[i + j * w];
                int sp = (k & 3840) >> 8;
                k &= ~3840;
                int v;
                if (is_ocean_id(k) || k == BI_MUSHROOM) v = k;
                else if (k == 1) v = sp > 0 ? (lr_int(&r, 3) == 0 ? BI_MESA_PLATEAU : BI_MESA_PLATEAU_F) : warm[lr_int(&r, 6)];
                else if (k == 2) v = sp > 0 ? BI_JUNGLE : medium[lr_int(&r, 6)];
                else if (k == 3) v = sp > 0 ? BI_MEGA_TAIGA : cold[lr_int(&r, 4)];
                else if (k == 4) v = ice[lr_int(&r, 4)];
                else v = BI_MUSHROOM;
                out[i + j * w] = v;
            }
        break;
    }
    case L_DESERT:
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++) {
                lr_chunk(&r, x + i, z + j);
                int k = in[i + 1 + (j + 1) * pw];
                int n = in[i + 1 + j * pw], e = in[i + 2 + (j + 1) * pw], ww = in[i + (j + 1) * pw],
                    s = in[i + 1 + (j + 2) * pw];
                int v;
                /* a(...): extreme hills edge */
                if (bio_same(k, BI_EXTREME_HILLS)) {
                    /* b(int,int): same kind, or compatible temperatures */
#define TEMPOK(q, t) (bio_same(q, t) || (bio_exists(q) && bio_exists(t) && (bio(q)->temp_cat == bio(t)->temp_cat || bio(q)->temp_cat == 2 || bio(t)->temp_cat == 2)))
                    v = (TEMPOK(n, BI_EXTREME_HILLS) && TEMPOK(e, BI_EXTREME_HILLS) && TEMPOK(ww, BI_EXTREME_HILLS) &&
                         TEMPOK(s, BI_EXTREME_HILLS))
                            ? k
                            : BI_EH_EDGE;
                } else if (k == BI_MESA_PLATEAU_F) {
                    v = (bio_same(n, k) && bio_same(e, k) && bio_same(ww, k) && bio_same(s, k)) ? k : BI_MESA;
                } else if (k == BI_MESA_PLATEAU) {
                    v = (bio_same(n, k) && bio_same(e, k) && bio_same(ww, k) && bio_same(s, k)) ? k : BI_MESA;
                } else if (k == BI_MEGA_TAIGA) {
                    v = (bio_same(n, k) && bio_same(e, k) && bio_same(ww, k) && bio_same(s, k)) ? k : BI_TAIGA;
                } else if (k == BI_DESERT) {
                    v = (n != BI_ICE_PLAINS && e != BI_ICE_PLAINS && ww != BI_ICE_PLAINS && s != BI_ICE_PLAINS) ? k : BI_EH_PLUS;
                } else if (k == BI_SWAMP) {
                    if (n != BI_DESERT && e != BI_DESERT && ww != BI_DESERT && s != BI_DESERT && n != BI_COLD_TAIGA &&
                        e != BI_COLD_TAIGA && ww != BI_COLD_TAIGA && s != BI_COLD_TAIGA && n != BI_ICE_PLAINS &&
                        e != BI_ICE_PLAINS && ww != BI_ICE_PLAINS && s != BI_ICE_PLAINS)
                        v = (n != BI_JUNGLE && s != BI_JUNGLE && e != BI_JUNGLE && ww != BI_JUNGLE) ? k : BI_JUNGLE_EDGE;
                    else
                        v = BI_PLAINS;
                } else {
                    v = k;
                }
                out[i + j * w] = v;
            }
        break;
    case L_HILLS:
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++) {
                lr_chunk(&r, x + i, z + j);
                int k = in[i + 1 + (j + 1) * pw];
                int l = in2[i + 1 + (j + 1) * pw];
                int flag = (l - 2) % 29 == 0;
                int v;
                if (k != 0 && l >= 2 && (l - 2) % 29 == 1 && k < 128) {
                    v = bio_exists(k + 128) ? k + 128 : k;
                } else if (lr_int(&r, 3) != 0 && !flag) {
                    v = k;
                } else {
                    int i2 = k;
                    if (k == BI_DESERT) i2 = BI_DESERT_HILLS;
                    else if (k == BI_FOREST) i2 = BI_FOREST_HILLS;
                    else if (k == BI_BIRCH) i2 = BI_BIRCH_HILLS;
                    else if (k == BI_ROOFED) i2 = BI_PLAINS;
                    else if (k == BI_TAIGA) i2 = BI_TAIGA_HILLS;
                    else if (k == BI_MEGA_TAIGA) i2 = BI_MEGA_TAIGA_HILLS;
                    else if (k == BI_COLD_TAIGA) i2 = BI_COLD_TAIGA_HILLS;
                    else if (k == BI_PLAINS) i2 = lr_int(&r, 3) == 0 ? BI_FOREST_HILLS : BI_FOREST;
                    else if (k == BI_ICE_PLAINS) i2 = BI_ICE_MOUNTAINS;
                    else if (k == BI_JUNGLE) i2 = BI_JUNGLE_HILLS;
                    else if (k == BI_OCEAN) i2 = BI_DEEP_OCEAN;
                    else if (k == BI_EXTREME_HILLS) i2 = BI_EH_PLUS;
                    else if (k == BI_SAVANNA) i2 = BI_SAVANNA_PLATEAU;
                    else if (bio_same(k, BI_MESA_PLATEAU_F)) i2 = BI_MESA;
                    else if (k == BI_DEEP_OCEAN && lr_int(&r, 3) == 0) i2 = lr_int(&r, 2) == 0 ? BI_PLAINS : BI_FOREST;
                    if (flag && i2 != k) i2 = bio_exists(i2 + 128) ? i2 + 128 : k;
                    if (i2 == k) {
                        v = k;
                    } else {
                        int n = in[i + 1 + j * pw], e = in[i + 2 + (j + 1) * pw], ww = in[i + (j + 1) * pw],
                            s = in[i + 1 + (j + 2) * pw];
                        int c = bio_same(n, k) + bio_same(e, k) + bio_same(ww, k) + bio_same(s, k);
                        v = c >= 3 ? i2 : k;
                    }
                }
                out[i + j * w] = v;
            }
        break;
    case L_PLAINS:
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++) {
                lr_chunk(&r, x + i, z + j);
                int k = in[i + 1 + (j + 1) * pw];
                out[i + j * w] = (lr_int(&r, 57) == 0 && k == BI_PLAINS) ? BI_PLAINS + 128 : k;
            }
        break;
    case L_MUSHSHORE:
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++) {
                lr_chunk(&r, x + i, z + j);
                int k = in[i + 1 + (j + 1) * pw];
                int n = in[i + 1 + j * pw], e = in[i + 2 + (j + 1) * pw], ww = in[i + (j + 1) * pw],
                    s = in[i + 1 + (j + 2) * pw];
                int v;
                const Biome *b = bio_exists(k) ? bio(k) : 0;
#define JUNGLEOK(q) ((bio_exists(q) && bio(q)->cls == C_JUNGLE) || (q) == BI_JUNGLE_EDGE || (q) == BI_JUNGLE || (q) == BI_JUNGLE_HILLS || (q) == BI_FOREST || (q) == BI_TAIGA || is_ocean_id(q))
#define ISMESA(q) (bio_exists(q) && bio(q)->cls == C_MESA)
                if (k == BI_MUSHROOM) {
                    v = (n != BI_OCEAN && e != BI_OCEAN && ww != BI_OCEAN && s != BI_OCEAN) ? k : BI_MUSHROOM_SHORE;
                } else if (b && b->cls == C_JUNGLE) {
                    if (JUNGLEOK(n) && JUNGLEOK(e) && JUNGLEOK(ww) && JUNGLEOK(s))
                        v = (!is_ocean_id(n) && !is_ocean_id(e) && !is_ocean_id(ww) && !is_ocean_id(s)) ? k : BI_BEACH;
                    else
                        v = BI_JUNGLE_EDGE;
                } else if (k != BI_EXTREME_HILLS && k != BI_EH_PLUS && k != BI_EH_EDGE) {
                    if (b && b->snowy) {
                        v = is_ocean_id(k) ? k : ((!is_ocean_id(n) && !is_ocean_id(e) && !is_ocean_id(ww) && !is_ocean_id(s)) ? k : BI_COLD_BEACH);
                    } else if (k != BI_MESA && k != BI_MESA_PLATEAU_F) {
                        if (k != BI_OCEAN && k != BI_DEEP_OCEAN && k != BI_RIVER && k != BI_SWAMP)
                            v = (!is_ocean_id(n) && !is_ocean_id(e) && !is_ocean_id(ww) && !is_ocean_id(s)) ? k : BI_BEACH;
                        else
                            v = k;
                    } else {
                        if (!is_ocean_id(n) && !is_ocean_id(e) && !is_ocean_id(ww) && !is_ocean_id(s))
                            v = (ISMESA(n) && ISMESA(e) && ISMESA(ww) && ISMESA(s)) ? k : BI_DESERT;
                        else
                            v = k;
                    }
                } else {
                    v = is_ocean_id(k) ? k : ((!is_ocean_id(n) && !is_ocean_id(e) && !is_ocean_id(ww) && !is_ocean_id(s)) ? k : BI_STONE_BEACH);
                }
                out[i + j * w] = v;
            }
        break;
    case L_RIVER:
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++) {
#define RC(q) ((q) >= 2 ? 2 + ((q) & 1) : (q))
                int a = RC(in[i + (j + 1) * pw]), b = RC(in[i + 2 + (j + 1) * pw]);
                int c = RC(in[i + 1 + j * pw]), d = RC(in[i + 1 + (j + 2) * pw]);
                int k = RC(in[i + 1 + (j + 1) * pw]);
                out[i + j * w] = (k == a && k == c && k == b && k == d) ? -1 : BI_RIVER;
            }
        break;
    case L_SMOOTH:
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++) {
                int a = in[i + (j + 1) * pw], b = in[i + 2 + (j + 1) * pw];
                int c = in[i + 1 + j * pw], d = in[i + 1 + (j + 2) * pw];
                int k = in[i + 1 + (j + 1) * pw];
                if (a == b && c == d) {
                    lr_chunk(&r, x + i, z + j);
                    k = lr_int(&r, 2) == 0 ? a : c;
                } else {
                    if (a == b) k = a;
                    if (c == d) k = c;
                }
                out[i + j * w] = k;
            }
        break;
    }
}

/* Runs a chain of layers (the last one producing window x,z,w,h), starting from the
 * output of `base` layers (computed recursively by run_base) or from `start`.
 * Returns a pointer to the result (allocated at the bump allocator). */
typedef struct {
    int x, z, w, h;
} Win;

static i32 *run_chain(const LayerDef *const *defs, int n, int x, int z, int w, int h, const i32 *second) {
    /* windows, top down */
    Win win[40];
    win[n - 1] = (Win){x, z, w, h};
    for (int i = n - 1; i > 0; i--)
        layer_parent_win(defs[i]->type, win[i].x, win[i].z, win[i].w, win[i].h, &win[i - 1].x, &win[i - 1].z,
                         &win[i - 1].w, &win[i - 1].h);
    /* bottom up with two buffers */
    int mark = lay_top;
    int maxa = 0;
    for (int i = 0; i < n; i++)
        if (win[i].w * win[i].h > maxa) maxa = win[i].w * win[i].h;
    i32 *b0 = lay_alloc(maxa), *b1 = lay_alloc(maxa);
    i32 *cur = 0;
    for (int i = 0; i < n; i++) {
        i32 *dst = (cur == b0) ? b1 : b0;
        layer_apply(defs[i], cur, (defs[i]->type == L_HILLS) ? second : 0, dst, win[i].x, win[i].z, win[i].w,
                    win[i].h);
        cur = dst;
    }
    /* move result to mark */
    i32 *res = lay_mem + mark;
    memmove(res, cur, sizeof(i32) * (size_t)(w * h));
    lay_top = mark + w * h;
    return res;
}

static const LayerDef *chain_buf[40];

static int chain_build(const LayerDef *tail, int ntail, int with_base) {
    int n = 0;
    if (with_base)
        for (int i = 0; i < NBASE; i++) chain_buf[n++] = &LBASE[i];
    for (int i = 0; i < ntail; i++) chain_buf[n++] = &tail[i];
    return n;
}

/* GenLayerRiverMix output for window (x,z,w,h) into dst (biome ids) */
static void layers_rivermix(int x, int z, int w, int h, i32 *dst) {
    int mark = lay_top;
    /* biome branch: base + biome1 to desert, then hills (needs the hills river noise), then biome2 */
    /* window of the hills layer input */
    const LayerDef *b2[9];
    for (int i = 0; i < 9; i++) b2[i] = &LBIOME2[i];
    Win wb2[9];
    wb2[8] = (Win){x - 1, z - 1, w + 2, h + 2}; /* the smooth layer input is fed by b2[7]; RiverMix needs smooth(x,z,w,h) */
    /* compute windows for b2 chain from its top (smooth outputs x,z,w,h) */
    wb2[8] = (Win){x, z, w, h};
    for (int i = 8; i > 0; i--)
        layer_parent_win(b2[i]->type, wb2[i].x, wb2[i].z, wb2[i].w, wb2[i].h, &wb2[i - 1].x, &wb2[i - 1].z,
                         &wb2[i - 1].w, &wb2[i - 1].h);
    Win hin; /* input window of the hills layer */
    layer_parent_win(L_HILLS, wb2[0].x, wb2[0].z, wb2[0].w, wb2[0].h, &hin.x, &hin.z, &hin.w, &hin.h);
    /* hills river noise */
    int n = chain_build(LHILLSRIVER, 3, 1);
    i32 *hr = run_chain(chain_buf, n, hin.x, hin.z, hin.w, hin.h, 0);
    /* biome chain up to desert */
    n = chain_build(LBIOME1, 4, 1);
    i32 *bd = run_chain(chain_buf, n, hin.x, hin.z, hin.w, hin.h, 0);
    /* hills .. smooth: run chain fed by bd */
    {
        int mark2 = lay_top;
        int maxa = 0;
        for (int i = 0; i < 9; i++)
            if (wb2[i].w * wb2[i].h > maxa) maxa = wb2[i].w * wb2[i].h;
        i32 *q0 = lay_alloc(maxa), *q1 = lay_alloc(maxa);
        const i32 *cur = bd;
        i32 *dstb = q0;
        for (int i = 0; i < 9; i++) {
            layer_apply(b2[i], cur, hr, dstb, wb2[i].x, wb2[i].z, wb2[i].w, wb2[i].h);
            cur = dstb;
            dstb = (dstb == q0) ? q1 : q0;
        }
        /* keep biome result at mark2 (overwrites q0 area, safe with memmove) */
        i32 *keep = lay_mem + mark;
        memmove(keep, cur, sizeof(i32) * (size_t)(w * h));
        lay_top = mark + w * h;
        (void)mark2;
    }
    i32 *biomes = lay_mem + mark;
    n = chain_build(LRIVER, 9, 1);
    i32 *river = run_chain(chain_buf, n, x, z, w, h, 0);
    for (int i = 0; i < w * h; i++) {
        int b = biomes[i], rv = river[i];
        int v;
        if (b != BI_OCEAN && b != BI_DEEP_OCEAN) {
            if (rv == BI_RIVER) {
                if (b == BI_ICE_PLAINS) v = BI_FROZEN_RIVER;
                else if (b != BI_MUSHROOM && b != BI_MUSHROOM_SHORE) v = rv & 255;
                else v = BI_MUSHROOM_SHORE;
            } else {
                v = b;
            }
        } else {
            v = b;
        }
        dst[i] = v;
    }
    lay_top = mark;
}

/* GenLayerZoomVoronoi for the block window (x, z, w, h); rm = rivermix window
 * ((x-2)>>2, (z-2)>>2, rw, rh) as returned by voronoi_rm_win. Output uint8 biome ids. */
static void voronoi_rm_win(int x, int z, int w, int h, int *rx, int *rz, int *rw, int *rh) {
    *rx = (x - 2) >> 2;
    *rz = (z - 2) >> 2;
    *rw = ((x - 2 + w - 1) >> 2) - *rx + 2;
    *rh = ((z - 2 + h - 1) >> 2) - *rz + 2;
}

static i64 vor_c;

/* jitter values for the 4 corner points of a cell, as integers u = a(1024) - 512 */
static void vor_cell(i64 cx4, i64 cz4, int u[8]) {
    LRand r;
    r.c = vor_c;
    lr_chunk(&r, cx4 << 2, cz4 << 2);
    u[0] = lr_int(&r, 1024) - 512; /* d1 x */
    u[1] = lr_int(&r, 1024) - 512; /* d2 z */
    lr_chunk(&r, (cx4 + 1) << 2, cz4 << 2);
    u[2] = lr_int(&r, 1024) - 512; /* d3 x (+4) */
    u[3] = lr_int(&r, 1024) - 512; /* d4 z */
    lr_chunk(&r, cx4 << 2, (cz4 + 1) << 2);
    u[4] = lr_int(&r, 1024) - 512; /* d5 x */
    u[5] = lr_int(&r, 1024) - 512; /* d6 z (+4) */
    lr_chunk(&r, (cx4 + 1) << 2, (cz4 + 1) << 2);
    u[6] = lr_int(&r, 1024) - 512; /* d7 x (+4) */
    u[7] = lr_int(&r, 1024) - 512; /* d8 z (+4) */
}

/* which corner (0: x0z0, 1: x1z0, 2: x0z1, 3: x1z1) sub-position (sx, sz) of a cell picks */
static int vor_pick(const int u[8], int sx, int sz) {
    /* (i - d) * 2560 = 2560 * (i - off) - 9u, exact */
    static const int offx[4] = {0, 4, 0, 4}, offz[4] = {0, 0, 4, 4};
    i64 dd[4];
    for (int k = 0; k < 4; k++) {
        i64 nx = 2560LL * (sx - offx[k]) - 9LL * u[2 * k];
        i64 nz = 2560LL * (sz - offz[k]) - 9LL * u[2 * k + 1];
        dd[k] = nx * nx + nz * nz;
    }
    if (dd[0] == dd[1] || dd[0] == dd[2] || dd[0] == dd[3] || dd[1] == dd[2] || dd[1] == dd[3] || dd[2] == dd[3]) {
        /* exact tie: replay Java's double arithmetic */
        double d[8];
        for (int k = 0; k < 8; k++) {
            d[k] = ((double)(u[k] + 512) / 1024.0 - 0.5) * 3.6;
            if ((k == 2) || (k == 5) || (k == 6) || (k == 7)) d[k] += 4.0;
        }
        double i4 = sz, k4 = sx;
        double d9 = (i4 - d[1]) * (i4 - d[1]) + (k4 - d[0]) * (k4 - d[0]);
        double d10 = (i4 - d[3]) * (i4 - d[3]) + (k4 - d[2]) * (k4 - d[2]);
        double d11 = (i4 - d[5]) * (i4 - d[5]) + (k4 - d[4]) * (k4 - d[4]);
        double d12 = (i4 - d[7]) * (i4 - d[7]) + (k4 - d[6]) * (k4 - d[6]);
        if (d9 < d10 && d9 < d11 && d9 < d12) return 0;
        if (d10 < d9 && d10 < d11 && d10 < d12) return 1;
        if (d11 < d9 && d11 < d10 && d11 < d12) return 2;
        return 3;
    }
    if (dd[0] < dd[1] && dd[0] < dd[2] && dd[0] < dd[3]) return 0;
    if (dd[1] < dd[0] && dd[1] < dd[2] && dd[1] < dd[3]) return 1;
    if (dd[2] < dd[0] && dd[2] < dd[1] && dd[2] < dd[3]) return 2;
    return 3;
}

static void voronoi(const i32 *rm, int rx, int rz, int rw, int x, int z, int w, int h, uint8_t *out, int ostride) {
    int u[8];
    i64 lastx = INT64_MIN, lastz = INT64_MIN;
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
            int X = x + i - 2, Z = z + j - 2;
            int cx4 = X >> 2, cz4 = Z >> 2;
            if (cx4 != lastx || cz4 != lastz) {
                vor_cell(cx4, cz4, u);
                lastx = cx4;
                lastz = cz4;
            }
            int k = vor_pick(u, X & 3, Z & 3);
            int ix = cx4 - rx + (k & 1), iz = cz4 - rz + (k >> 1);
            out[i + j * ostride] = (uint8_t)(rm[ix + iz * rw] & 255);
        }
}

/* ======================================================================== */
/* Noise                                                                     */
/* ======================================================================== */

/* A NoiseGeneratorPerlin / NoiseGenerator3Handler is fully determined by the
 * java.util.Random state at its construction: 3 nextDouble (offsets) then 256
 * swaps. We keep only that state and rebuild the permutation when needed. */
typedef struct {
    u64 st;
} Octave;

static void perm_build(u64 st, uint8_t *perm, i64 off[3]) {
    JRand r = {st};
    for (int k = 0; k < 3; k++) {
        i64 n = jr_double_bits(&r);
        if (off) off[k] = n >> 13; /* offset * 2^32 (offset = n / 2^53 * 256), Q32 */
    }
    for (int i = 0; i < 256; i++) perm[i] = (uint8_t)i;
    for (int i = 0; i < 256; i++) {
        int j = jr_int(&r, 256 - i) + i;
        uint8_t t = perm[i];
        perm[i] = perm[j];
        perm[j] = t;
    }
}

static void perm_skip(JRand *r) {
    for (int k = 0; k < 6; k++) jr_next(r, 27);
    for (int i = 0; i < 256; i++) jr_int(r, 256 - i);
}

static inline float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
static inline float lerp(float t, float a, float b) { return a + t * (b - a); }

static inline float grad3(int h, float x, float y, float z) {
    switch (h & 15) {
    case 0: return x + y;
    case 1: return -x + y;
    case 2: return x - y;
    case 3: return -x - y;
    case 4: return x + z;
    case 5: return -x + z;
    case 6: return x - z;
    case 7: return -x - z;
    case 8: return y + z;
    case 9: return -y + z;
    case 10: return y - z;
    case 11: return -y - z;
    case 12: return x + y;
    case 13: return -y + z;
    case 14: return -x + y;
    default: return -y - z;
    }
}

/* Coordinates of samples: for octave `oct` (frequency 2^-oct) of a generator whose
 * scale (Java double from a float) is S_fx = scale * 2^32, the sample index n of a
 * request starting at integer position base gives
 *   coord = (base * scale + n * scale) * 2^-oct + offset
 * We compute it modulo 2^32 * 256 in Q32 fixed point: cell = bits 32..39, frac = bits 0..31. */
static inline u64 coord_q32(i64 base, u64 S_fx, int oct, i64 n, i64 off) {
    u64 b = ((u64)base * S_fx) >> oct;
    return b + (u64)n * (S_fx >> oct) + (u64)off;
}

static inline void coord_split(u64 c, int *cell, float *frac) {
    *cell = (int)((c >> 32) & 255);
    *frac = (float)(u32)c * (1.0f / 4294967296.0f);
}

/* Fixed-point scales (value * 2^32) of the generators' sample spacings: Java passes
 * (double)float, and these floats are exactly representable in Q32. */
#define FX(f) ((u64)((double)(f) * 4294967296.0))
static u64 SC_LIMIT, SC_MAINXZ, SC_MAINY, SC_DEPTH;

/* One 3D octave over the chunk grid (5 x 33 x 5, index (x*5+z)*33+y), Java's
 * NoiseGeneratorPerlin.a with its stale-cache quirk. need: bitmask per point or NULL.
 * mul: per-point multiplier array (or NULL for 1). */
static void perlin3_octave(const uint8_t *P, float *acc, const uint8_t *need, int bit, const float *mul, const int *xc,
                           const float *xf, const int *zc, const float *zf, const int *yc, const float *yf, float amp) {
    int idx = 0;
    for (int x = 0; x < 5; x++) {
        float fx = xf[x], ux = fade(fx);
        int l1 = xc[x];
        for (int z = 0; z < 5; z++) {
            float fz = zf[z], uz = fade(fz);
            int j4 = zc[z];
            int last = -1, pend = 0, valid = 0;
            float d16 = 0, d7 = 0, d17 = 0, d8 = 0;
            for (int y = 0; y < 33; y++, idx++) {
                int i5 = yc[y];
                if (y == 0 || i5 != last) {
                    last = i5;
                    pend = y;
                    valid = 0;
                }
                if (need && !(need[idx] & bit)) continue;
                if (!valid) {
                    float fy = yf[pend];
                    int ci = yc[pend];
                    int j5 = P[l1] + ci;
                    int k5 = P[j5 & 255] + j4;
                    int l5 = P[(j5 + 1) & 255] + j4;
                    int i6 = P[(l1 + 1) & 255] + ci;
                    int i2 = P[i6 & 255] + j4;
                    int j6 = P[(i6 + 1) & 255] + j4;
                    d16 = lerp(ux, grad3(P[k5 & 255], fx, fy, fz), grad3(P[i2 & 255], fx - 1, fy, fz));
                    d7 = lerp(ux, grad3(P[l5 & 255], fx, fy - 1, fz), grad3(P[j6 & 255], fx - 1, fy - 1, fz));
                    d17 = lerp(ux, grad3(P[(k5 + 1) & 255], fx, fy, fz - 1), grad3(P[(i2 + 1) & 255], fx - 1, fy, fz - 1));
                    d8 = lerp(ux, grad3(P[(l5 + 1) & 255], fx, fy - 1, fz - 1),
                              grad3(P[(j6 + 1) & 255], fx - 1, fy - 1, fz - 1));
                    valid = 1;
                }
                float uy = fade(yf[y]);
                float v = lerp(uz, lerp(uy, d16, d7), lerp(uy, d17, d8));
                acc[idx] += v * amp * (mul ? mul[idx] : 1.0f);
            }
        }
    }
}

/* 2D octave (Java's j == 1 path) over 5 x 5 */
static void perlin2_octave(const uint8_t *P, float *acc, const int *xc, const float *xf, const int *zc,
                           const float *zf, float amp) {
    int idx = 0;
    for (int x = 0; x < 5; x++) {
        float fx = xf[x], ux = fade(fx);
        int i3 = xc[x];
        for (int z = 0; z < 5; z++, idx++) {
            float fz = zf[z], uz = fade(fz);
            int l1 = zc[z];
            int l = P[i3];
            int j3 = P[l & 255] + l1;
            int k3 = P[(i3 + 1) & 255];
            int i1 = P[k3 & 255] + l1;
            float d11 = lerp(ux, grad3(P[j3 & 255], fx, 0, fz), grad3(P[i1 & 255], fx - 1, 0, fz));
            float d12 = lerp(ux, grad3(P[(j3 + 1) & 255], fx, 0, fz - 1), grad3(P[(i1 + 1) & 255], fx - 1, 0, fz - 1));
            acc[idx] += lerp(uz, d11, d12) * amp;
        }
    }
}

/* ---- simplex (NoiseGenerator3Handler) ---- */
/* constants as Q64 fractions for exact integer setup */
#define F2D 0.36602540378443864676 /* 0.5 * (sqrt(3) - 1) */
#define G2D 0.21132486540518711775 /* (3 - sqrt(3)) / 6 */
#define H2D 0.57735026918962576451 /* 1 - 2 * G2 = sqrt(3) / 3 */
static const u64 F2_Q64 = 0x5DB3D742C265539DULL; /* F2 * 2^64 */

/* (k * frac) in Q32 (k signed, frac = q/2^64), exact to ~2^-32 */
static inline i64 mul_q64(i64 k, u64 q) {
    u64 hi = q >> 32, lo = q & 0xFFFFFFFFULL;
    i64 a = k * (i64)hi;               /* Q32 */
    i64 b = (k * (i64)lo) >> 32;       /* Q32 (|k| < 2^31) */
    return a + b;
}

static const int8_t SGRAD[12][2] = {{1, 1}, {-1, 1}, {1, -1}, {-1, -1}, {1, 0}, {-1, 0},
                                    {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {0, 1}, {0, -1}};

/* simplex value at (xi + xf, zi + zf); xf, zf small (|.| < ~4) */
static float simplex2(const uint8_t *P, i32 xi, float xf, i32 zi, float zf) {
    i32 K = xi + zi;
    i64 sq = mul_q64(K, F2_Q64); /* K*F2, Q32 */
    i32 si = (i32)(sq >> 32);
    float sf = (float)(u32)sq * (1.0f / 4294967296.0f);
    float t = (xf + zf) * (float)F2D;
    float sx = xf + sf + t, sz = zf + sf + t;
    int fsx = floor_f(sx), fsz = floor_f(sz);
    i32 ci = xi + si + fsx, cj = zi + si + fsz;
    /* unskew: x0 = xf - fsx + (fsx + fsz) * G2 + (K * G2 - si * (1 - 2 G2)), and the last
     * term equals sf * (1 - 2 G2) because F2 * (1 - 2 G2) = G2 */
    float u = (float)(fsx + fsz) * (float)G2D + sf * (float)H2D;
    float x0 = (xf - (float)fsx) + u, z0 = (zf - (float)fsz) + u;
    int b0, b1;
    if (x0 > z0) {
        b0 = 1;
        b1 = 0;
    } else {
        b0 = 0;
        b1 = 1;
    }
    float x1 = x0 - (float)b0 + (float)G2D, z1 = z0 - (float)b1 + (float)G2D;
    float x2 = x0 - 1.0f + 2.0f * (float)G2D, z2 = z0 - 1.0f + 2.0f * (float)G2D;
    int k = ci & 255, l = cj & 255;
    int g0 = P[(k + P[l]) & 255] % 12;
    int g1 = P[(k + b0 + P[(l + b1) & 255]) & 255] % 12;
    int g2 = P[(k + 1 + P[(l + 1) & 255]) & 255] % 12;
    float n = 0;
    float t0 = 0.5f - x0 * x0 - z0 * z0;
    if (t0 >= 0) {
        t0 *= t0;
        n += t0 * t0 * (SGRAD[g0][0] * x0 + SGRAD[g0][1] * z0);
    }
    float t1 = 0.5f - x1 * x1 - z1 * z1;
    if (t1 >= 0) {
        t1 *= t1;
        n += t1 * t1 * (SGRAD[g1][0] * x1 + SGRAD[g1][1] * z1);
    }
    float t2 = 0.5f - x2 * x2 - z2 * z2;
    if (t2 >= 0) {
        t2 *= t2;
        n += t2 * t2 * (SGRAD[g2][0] * x2 + SGRAD[g2][1] * z2);
    }
    return 70.0f * n;
}

/* Java's single-point NoiseGenerator3Handler.a(double, double) in double: used only to
 * settle values the float version puts within 1e-5 of a threshold that matters. */
static int jfloor_s(double d) { return d > 0.0 ? (int)d : (int)d - 1; }

static double simplex2_d(const uint8_t *P, double d0, double d1) {
    const double F = 0.5 * (1.7320508075688772 - 1.0), G = (3.0 - 1.7320508075688772) / 6.0;
    double d3 = (d0 + d1) * F;
    int i = jfloor_s(d0 + d3), j = jfloor_s(d1 + d3);
    double d5 = (double)(i + j) * G;
    double d8 = d0 - ((double)i - d5), d9 = d1 - ((double)j - d5);
    int b0 = d8 > d9, b1 = !b0;
    double d10 = d8 - b0 + G, d11 = d9 - b1 + G, d12 = d8 - 1.0 + 2.0 * G, d13 = d9 - 1.0 + 2.0 * G;
    int k = i & 255, l = j & 255;
    int g0 = P[(k + P[l]) & 255] % 12, g1 = P[(k + b0 + P[(l + b1) & 255]) & 255] % 12,
        g2 = P[(k + 1 + P[(l + 1) & 255]) & 255] % 12;
    double n = 0, t;
    t = 0.5 - d8 * d8 - d9 * d9;
    if (t >= 0) {
        t *= t;
        n += t * t * (SGRAD[g0][0] * d8 + SGRAD[g0][1] * d9);
    }
    t = 0.5 - d10 * d10 - d11 * d11;
    if (t >= 0) {
        t *= t;
        n += t * t * (SGRAD[g1][0] * d10 + SGRAD[g1][1] * d11);
    }
    t = 0.5 - d12 * d12 - d13 * d13;
    if (t >= 0) {
        t *= t;
        n += t * t * (SGRAD[g2][0] * d12 + SGRAD[g2][1] * d13);
    }
    return 70.0 * n;
}

/* ======================================================================== */
/* World state                                                               */
/* ======================================================================== */

static Octave oc_min[16], oc_max[16], oc_main[8], oc_surf[4], oc_depth[16];
static uint8_t perm_temp[256], perm_grass[256]; /* BiomeBase.ae (1234), .af (2345) */
static Octave oc_mesa[6];                          /* aH, aF[0..3], aG */
static uint8_t mesa_bands[64];                     /* B_ ids */
static i64 pop_mul_x, pop_mul_z;                   /* population seed multipliers */
static i64 cave_mul_x, cave_mul_z;                 /* MapGenBase seed multipliers */

/* temperature noise (BiomeBase.ae) at block x, z: ae.a(x/8, z/8) */
static float temp_noise(int x, int z) {
    return simplex2(perm_temp, x >> 3, (float)(x & 7) * 0.125f, z >> 3, (float)(z & 7) * 0.125f);
}

/* BiomeBase.a(BlockPosition): temperature at height */
static float biome_temp_at(const Biome *b, int x, int y, int z) {
    if (y > 64) {
        float f = temp_noise(x, z) * 4.0f;
        return b->temp - (f + (float)y - 64.0f) * 0.05f / 30.0f;
    }
    return b->temp;
}

/* af.a(x / d, z / d) for an integer divisor d (exact split) */
static float grass_noise_div(int x, int z, int d) {
    int xi = x >= 0 ? x / d : -((-x + d - 1) / d), zi = z >= 0 ? z / d : -((-z + d - 1) / d);
    return simplex2(perm_grass, xi, (float)(x - xi * d) / (float)d, zi, (float)(z - zi * d) / (float)d);
}

/* ======================================================================== */
/* Scratch memory (phases never overlap)                                     */
/* ======================================================================== */

#define LAY_INTS 1200

typedef struct {
    float grid[825];      /* main noise, then density */
    float acc[825];       /* limit noise accumulator */
    float surf[256];      /* surface depth noise, index z*16+x */
    uint8_t cls[825];     /* clamped-lerp class per grid point */
    uint8_t perm[256];
    uint8_t mperm[6][256]; /* mesa noises (built when a mesa column shows up) */
    uint8_t col[256];
    uint8_t mesa_ready;
} TerrainScratch;

typedef struct {
    int x, y, z; /* lake box origin (world) */
    uint8_t liquid;
    uint8_t shape[256]; /* 16x16x8 bits: index (x*16+z)*8+y */
} LakeRec;

typedef struct {
    int px, pz, ox, oz;            /* populated chunk, frame origin (frame = 32x32 columns) */
    uint8_t top[1024], tb[1024];   /* highest non-plant block of each frame column, and the block */
    uint8_t pl[1024];              /* plant-like block right above top (0 = none) */
    int nlake;
    LakeRec lake[2];
    /* cave query mask active for reads */
    int qon, qx0, qy0, qz0, qx1, qy1, qz1;
} View;

typedef struct {
    View v;
    uint8_t qmask[2048];
    i32 lay[640];
} PopScratch;

static union {
    TerrainScratch t;
    i32 lay[LAY_INTS];
    PopScratch p;
} U;

/* ======================================================================== */
/* Terrain                                                                   */
/* ======================================================================== */

static float bweights[25]; /* ChunkProviderGenerate.q */

/* biomes of chunk (cx, cz): 1:4 grid 10x10 at (cx*4-2, cz*4-2) and 16x16 blocks */
static uint8_t cb4[100], cb16[256];

static void chunk_biomes(int cx, int cz) {
    i32 *rm = U.lay;
    lay_mem = U.lay + 100;
    lay_cap = LAY_INTS - 100;
    lay_top = 0;
    layers_rivermix(cx * 4 - 2, cz * 4 - 2, 10, 10, rm);
    for (int i = 0; i < 100; i++) cb4[i] = (uint8_t)rm[i];
    /* voronoi window for blocks (cx*16, cz*16, 16, 16) is rivermix (cx*4-1, cz*4-1, 6, 6),
     * a sub-window of the 10x10 one at offset (1, 1) */
    voronoi(rm + 1 + 10, cx * 4 - 1, cz * 4 - 1, 10, cx * 16, cz * 16, 16, 16, cb16, 16);
}

static void octave_setup(const Octave *o, i64 off[3]) { perm_build(o->st, U.t.perm, off); }

/* noise grid for chunk (cx, cz) -> U.t.grid holds the 5x5x33 densities */
static void chunk_density(int cx, int cz) {
    TerrainScratch *T = &U.t;
    int xc[5], zc[5], yc[33];
    float xf[5], zf[5], yf[33];
    i64 off[3];
    i64 bx = (i64)cx * 4, bz = (i64)cz * 4;
    /* main noise (8 octaves), all points */
    memset(T->grid, 0, sizeof T->grid);
    for (int o = 0; o < 8; o++) {
        octave_setup(&oc_main[o], off);
        for (int n = 0; n < 5; n++) {
            coord_split(coord_q32(bx, SC_MAINXZ, o, n, off[0]), &xc[n], &xf[n]);
            coord_split(coord_q32(bz, SC_MAINXZ, o, n, off[2]), &zc[n], &zf[n]);
        }
        for (int n = 0; n < 33; n++) coord_split(coord_q32(0, SC_MAINY, o, n, off[1]), &yc[n], &yf[n]);
        perlin3_octave(T->perm, T->grid, 0, 0, 0, xc, xf, zc, zf, yc, yf, (float)(1 << o));
    }
    /* classes: 1 min only, 2 max only, 3 both */
    for (int i = 0; i < 825; i++) {
        float d7 = (T->grid[i] / 10.0f + 1.0f) / 2.0f;
        T->grid[i] = d7;
        T->cls[i] = d7 < 0.0f ? 1 : (d7 > 1.0f ? 2 : 3);
    }
    memset(T->acc, 0, sizeof T->acc);
    for (int o = 0; o < 16; o++) {
        octave_setup(&oc_min[o], off);
        for (int n = 0; n < 5; n++) {
            coord_split(coord_q32(bx, SC_LIMIT, o, n, off[0]), &xc[n], &xf[n]);
            coord_split(coord_q32(bz, SC_LIMIT, o, n, off[2]), &zc[n], &zf[n]);
        }
        for (int n = 0; n < 33; n++) coord_split(coord_q32(0, SC_LIMIT, o, n, off[1]), &yc[n], &yf[n]);
        perlin3_octave(T->perm, T->acc, T->cls, 1, 0, xc, xf, zc, zf, yc, yf, (float)(1 << o));
    }
    for (int i = 0; i < 825; i++) {
        if (T->cls[i] == 3) T->acc[i] *= 1.0f - T->grid[i];
        if (T->cls[i] == 2) T->grid[i] = 1.0f;
    }
    for (int o = 0; o < 16; o++) {
        octave_setup(&oc_max[o], off);
        for (int n = 0; n < 5; n++) {
            coord_split(coord_q32(bx, SC_LIMIT, o, n, off[0]), &xc[n], &xf[n]);
            coord_split(coord_q32(bz, SC_LIMIT, o, n, off[2]), &zc[n], &zf[n]);
        }
        for (int n = 0; n < 33; n++) coord_split(coord_q32(0, SC_LIMIT, o, n, off[1]), &yc[n], &yf[n]);
        perlin3_octave(T->perm, T->acc, T->cls, 2, T->grid, xc, xf, zc, zf, yc, yf, (float)(1 << o));
    }
    /* depth noise (2D, 16 octaves) into grid[0..24] after we are done with it: keep separate */
    float depth[25];
    memset(depth, 0, sizeof depth);
    for (int o = 0; o < 16; o++) {
        octave_setup(&oc_depth[o], off);
        for (int n = 0; n < 5; n++) {
            coord_split(coord_q32(bx, SC_DEPTH, o, n, off[0]), &xc[n], &xf[n]);
            coord_split(coord_q32(bz, SC_DEPTH, o, n, off[2]), &zc[n], &zf[n]);
        }
        perlin2_octave(T->perm, depth, xc, xf, zc, zf, (float)(1 << o));
    }
    /* density */
    int l = 0, i1 = 0;
    for (int j1 = 0; j1 < 5; j1++)
        for (int k1 = 0; k1 < 5; k1++) {
            float f2 = 0, f3 = 0, f4 = 0;
            const Biome *bc = bio(cb4[j1 + 2 + (k1 + 2) * 10]);
            for (int l1 = -2; l1 <= 2; l1++)
                for (int i2 = -2; i2 <= 2; i2++) {
                    const Biome *b1 = bio(cb4[j1 + l1 + 2 + (k1 + i2 + 2) * 10]);
                    float f5 = 0.0f + b1->depth * 1.0f;
                    float f6 = 0.0f + b1->scale * 1.0f;
                    float f7 = bweights[l1 + 2 + (i2 + 2) * 5] / (f5 + 2.0f);
                    if (b1->depth > bc->depth) f7 /= 2.0f;
                    f2 += f6 * f7;
                    f3 += f5 * f7;
                    f4 += f7;
                }
            f2 /= f4;
            f3 /= f4;
            f2 = f2 * 0.9f + 0.1f;
            f3 = (f3 * 4.0f - 1.0f) / 8.0f;
            float d0 = depth[i1] / 8000.0f;
            if (d0 < 0.0f) d0 = -d0 * 0.3f;
            d0 = d0 * 3.0f - 2.0f;
            if (d0 < 0.0f) {
                d0 /= 2.0f;
                if (d0 < -1.0f) d0 = -1.0f;
                d0 /= 1.4f;
                d0 /= 2.0f;
            } else {
                if (d0 > 1.0f) d0 = 1.0f;
                d0 /= 8.0f;
            }
            ++i1;
            float d1 = f3, d2 = f2;
            d1 += d0 * 0.2f;
            d1 = d1 * 8.5f / 8.0f;
            float d3 = 8.5f + d1 * 4.0f;
            for (int j2 = 0; j2 < 33; j2++, l++) {
                float d4 = ((float)j2 - d3) * 12.0f * 128.0f / 256.0f / d2;
                if (d4 < 0.0f) d4 *= 4.0f;
                float d8 = T->acc[l] / 512.0f - d4;
                if (j2 > 29) {
                    float d9 = (float)(j2 - 29) / 3.0f;
                    d8 = d8 * (1.0f - d9) + -10.0f * d9;
                }
                T->grid[l] = d8;
            }
        }
}

/* surface depth noise (NoiseGenerator3, 4 octaves) for chunk -> U.t.surf[z*16+x] */
static void chunk_surface_noise(int cx, int cz) {
    TerrainScratch *T = &U.t;
    i64 off[3];
    memset(T->surf, 0, sizeof T->surf);
    for (int o = 0; o < 4; o++) {
        perm_build(oc_surf[o].st, T->perm, off);
        /* coordinate = (X + i) * 0.0625 * 2^-o + offset */
        int sh = 4 + o;
        float amp = 0.55f * (float)(1 << o);
        /* offsets b (x) = off[0], c (z) = off[1] in Q32 */
        i32 bxi = (i32)(off[0] >> 32), bzi = (i32)(off[1] >> 32);
        float bxf = (float)(u32)off[0] * (1.0f / 4294967296.0f), bzf = (float)(u32)off[1] * (1.0f / 4294967296.0f);
        float scale = 1.0f / (float)(1 << sh);
        for (int j = 0; j < 16; j++) {
            int Z = cz * 16 + j;
            i32 zi = (Z >> sh) + bzi;
            float zf = (float)(Z & ((1 << sh) - 1)) * scale + bzf;
            for (int i = 0; i < 16; i++) {
                int X = cx * 16 + i;
                i32 xi = (X >> sh) + bxi;
                float xf = (float)(X & ((1 << sh) - 1)) * scale + bxf;
                T->surf[j * 16 + i] += simplex2(T->perm, xi, xf, zi, zf) * amp;
            }
        }
    }
}

/* ---- surface builders (BiomeBase.b and the overrides) ---- */

typedef struct {
    uint8_t top, blk, aux; /* see summary below */
} ColSum;

/* one step of BiomeBase.b's loop at height y (no bedrock); returns 1 if it needs a sandstone
 * draw (then the caller draws and calls sand_after) */
typedef struct {
    uint8_t top0, fill0, top, fill;
    int l, i1;
    float temp;
} SurfState;

static inline int surf_step(SurfState *s, uint8_t *col, int y) {
    uint8_t blk = col[y];
    if (blk == B_AIR) {
        s->l = -1;
        return 0;
    }
    if (blk != B_STONE) return 0;
    if (s->l == -1) {
        if (s->i1 <= 0) {
            s->top = B_AIR;
            s->fill = B_STONE;
        } else if (y >= SEA - 4 && y <= SEA + 1) {
            s->top = s->top0;
            s->fill = s->fill0;
        }
        if (y < SEA && s->top == B_AIR) s->top = s->temp < 0.15f ? B_ICE : B_WATER;
        s->l = s->i1;
        if (y >= SEA - 1) {
            col[y] = s->top;
        } else if (y < SEA - 7 - s->i1) {
            s->top = B_AIR;
            s->fill = B_STONE;
            col[y] = B_GRAVEL;
        } else {
            col[y] = s->fill;
        }
    } else if (s->l > 0) {
        --s->l;
        col[y] = s->fill;
        if (s->l == 0 && (s->fill == B_SAND || s->fill == B_RED_SAND)) return 1;
    }
    return 0;
}

static void build_default(uint8_t *col, const Biome *b, JRand *r, float noise, uint8_t top0, uint8_t fill0) {
    SurfState s;
    s.top0 = s.top = top0;
    s.fill0 = s.fill = fill0;
    s.l = -1;
    s.temp = b->temp;
    s.i1 = (int)(noise / 3.0f + 3.0f + jr_doublef(r) * 0.25f);
    unsigned skip = 0;
    for (int y = 255; y >= 0; y--) {
        if (y <= 4) {
            jr_skip(r, skip);
            skip = 0;
            if (y <= jr_int(r, 5)) {
                col[y] = B_BEDROCK;
                continue;
            }
        } else {
            skip++;
        }
        if (surf_step(&s, col, y)) {
            jr_skip(r, skip);
            skip = 0;
            s.l = jr_int(r, 4) + (y - SEA > 0 ? y - SEA : 0);
            s.fill = s.fill == B_RED_SAND ? B_RED_SANDSTONE : B_SANDSTONE;
        }
    }
}

/* BiomeMesa band at (x, y): aD[(y + round(aH(x/512, x/512) * 2) + 64) % 64] */
static uint8_t mesa_band(int x, int y) {
    const uint8_t *P = U.t.mperm[0];
    float xf = (float)(x & 511) / 512.0f;
    float v = simplex2(P, x >> 9, xf, x >> 9, xf) * 2.0f;
    int l = (int)floor_f(v + 0.5f);
    return mesa_bands[(y + l + 64) % 64];
}

static void mesa_prepare(void) {
    if (U.t.mesa_ready) return;
    for (int i = 0; i < 6; i++) perm_build(oc_mesa[i].st, U.t.mperm[i], 0);
    U.t.mesa_ready = 1;
}

/* NoiseGenerator3.a(x, z) (single point, no offsets) over n octaves with perms P[o] */
static float simplex_oct_point(int n, uint8_t (*P)[256], i32 xi, float xf, i32 zi, float zf, int shift0) {
    /* coordinates (xi + xf) * 2^-shift0 * 2^-o */
    float sum = 0;
    for (int o = 0; o < n; o++) {
        int sh = shift0 + o;
        /* split (xi + xf) / 2^sh into integer + fraction exactly enough */
        i32 xa = xi >> sh, za = zi >> sh;
        float xr = ((float)(xi & ((1 << sh) - 1)) + xf) / (float)(1 << sh);
        float zr = ((float)(zi & ((1 << sh) - 1)) + zf) / (float)(1 << sh);
        sum += simplex2(P[o], xa, xr, za, zr) * (float)(1 << o);
    }
    return sum;
}

static void build_mesa(uint8_t *col, const Biome *b, JRand *r, int bx, int bz, float noise) {
    mesa_prepare();
    float d1 = 0.0f;
    if (b->mode & 1) { /* bryce */
        int k = (bx & -16) + (bz & 15), l = (bz & -16) + (bx & 15);
        /* aF: 4 octaves at (k*0.25, l*0.25): perms mperm[0..3] (aF[0] == aH) */
        uint8_t(*P)[256] = U.t.mperm;
        float d2 = simplex_oct_point(4, P, k, 0.0f, l, 0.0f, 2);
        float an = noise < 0 ? -noise : noise;
        if (an < d2) d2 = an;
        if (d2 > 0.0f) {
            float d4 = simplex_oct_point(1, &U.t.mperm[4], k, 0.0f, l, 0.0f, 9);
            if (d4 < 0) d4 = -d4;
            d1 = d2 * d2 * 2.5f;
            float c = d4 * 50.0f;
            int ci = (int)c;
            float d5 = (float)(c > (float)ci ? ci + 1 : ci) + 14.0f;
            if (d1 > d5) d1 = d5;
            d1 += 64.0f;
        }
    }
    int d1i = (int)d1;
    uint8_t top = B_STAINED_CLAY_WHITE, fill = b->filler;
    int j1 = (int)(noise / 3.0f + 3.0f + jr_doublef(r) * 0.25f);
    /* Math.cos(d0 / 3.0 * pi) > 0 */
    float ph = noise / 3.0f;
    float m = ph - 2.0f * (float)floor_f(ph * 0.5f); /* in [0, 2) */
    int flag = (m < 0.5f || m > 1.5f);
    int k1 = -1, flag1 = 0;
    unsigned skip = 0;
    for (int y = 255; y >= 0; y--) {
        if (col[y] == B_AIR && y < d1i) col[y] = B_STONE;
        if (y <= 4) {
            jr_skip(r, skip);
            skip = 0;
            if (y <= jr_int(r, 5)) {
                col[y] = B_BEDROCK;
                continue;
            }
        } else {
            skip++;
        }
        uint8_t blk = col[y];
        if (blk == B_AIR) {
            k1 = -1;
        } else if (blk == B_STONE) {
            if (k1 == -1) {
                flag1 = 0;
                if (j1 <= 0) {
                    top = B_AIR;
                    fill = B_STONE;
                } else if (y >= SEA - 4 && y <= SEA + 1) {
                    top = B_STAINED_CLAY_WHITE;
                    fill = b->filler;
                }
                if (y < SEA && top == B_AIR) top = B_WATER;
                k1 = j1 + (y - SEA > 0 ? y - SEA : 0);
                if (y >= SEA - 1) {
                    if ((b->mode & 2) && y > 86 + j1 * 2) {
                        col[y] = flag ? B_COARSE_DIRT : B_GRASS;
                    } else if (y > SEA + 3 + j1) {
                        col[y] = (y >= 64 && y <= 127) ? (flag ? B_HARDENED_CLAY : mesa_band(bx, y)) : B_STAINED_CLAY_ORANGE;
                    } else {
                        col[y] = b->top;
                        flag1 = 1;
                    }
                } else {
                    col[y] = fill == B_STAINED_CLAY_WHITE ? B_STAINED_CLAY_ORANGE : fill;
                }
            } else if (k1 > 0) {
                --k1;
                col[y] = flag1 ? B_STAINED_CLAY_ORANGE : mesa_band(bx, y);
            }
        }
    }
    (void)top;
}

static void build_surface(uint8_t *col, const Biome *b, JRand *r, int bx, int bz, float noise) {
    uint8_t top = b->top, fill = b->filler;
    switch (b->surf) {
    case S_HILLS:
        top = B_GRASS;
        fill = B_DIRT;
        if ((noise < -1.0f || noise > 2.0f) && b->mode == 2) top = fill = B_GRAVEL;
        else if (noise > 1.0f && b->mode != 1) top = fill = B_STONE;
        break;
    case S_TAIGA_MEGA:
        top = B_GRASS;
        fill = B_DIRT;
        if (noise > 1.75f) top = B_COARSE_DIRT;
        else if (noise > -0.95f) top = B_PODZOL;
        break;
    case S_SAVANNA_M:
        top = B_GRASS;
        fill = B_DIRT;
        if (noise > 1.75f) top = fill = B_STONE;
        else if (noise > -0.5f) top = B_COARSE_DIRT;
        break;
    case S_SWAMP: {
        float f1 = simplex2(perm_grass, bx >> 2, (float)(bx & 3) * 0.25f, bz >> 2, (float)(bz & 3) * 0.25f);
        double d1 = f1;
        if ((f1 > -1e-5f && f1 < 1e-5f) || (f1 > 0.12f - 1e-5f && f1 < 0.12f + 1e-5f))
            d1 = simplex2_d(perm_grass, (double)bx * 0.25, (double)bz * 0.25);
        if (d1 > 0.0) {
            for (int y = 255; y >= 0; y--)
                if (col[y] != B_AIR) {
                    if (y == 62 && col[y] != B_WATER) {
                        col[y] = B_WATER;
                        if (d1 < 0.12) col[y + 1] = B_LILY_PAD;
                    }
                    break;
                }
        }
        break;
    }
    case S_MESA:
        build_mesa(col, b, r, bx, bz, noise);
        return;
    }
    build_default(col, b, r, noise, top, fill);
}

/* ---- per-chunk terrain pipeline ---- */

static inline int is_liquid_top(int blk) { return blk == B_WATER || blk == B_ICE || blk == B_LILY_PAD || blk == B_LAVA; }

/* column summary of the undecorated, cave-less terrain:
 *   top: y of the highest non-air block (lily pads excluded)
 *   blk: that block; B_LILY_PAD means water at top with a lily pad above
 *   aux: water columns (blk water/ice/lily): y of the highest solid block below;
 *        others: number of non-stone blocks (filler) right below the top */
static void column_summary(const uint8_t *col, ColSum *s) {
    int y = 255;
    while (y > 0 && col[y] == B_AIR) y--;
    int lily = 0;
    if (col[y] == B_LILY_PAD) {
        lily = 1;
        y--;
    }
    s->top = (uint8_t)y;
    s->blk = lily ? B_LILY_PAD : col[y];
    if (is_liquid_top(s->blk)) {
        int f = y;
        while (f > 0 && (col[f] == B_WATER || col[f] == B_ICE || col[f] == B_LAVA || col[f] == B_AIR)) f--;
        s->aux = (uint8_t)f;
    } else {
        int d = 0;
        for (int k = y - 1; k > 0 && d < 15; k--, d++) {
            uint8_t b = col[k];
            if (b == B_STONE || b == B_AIR || b == B_BEDROCK || b == B_WATER) break;
        }
        s->aux = (uint8_t)d;
    }
}

static void column_density(int x, int z, uint8_t *col) {
    const float *g = U.t.grid;
    int gx = x >> 2, gz = z >> 2;
    float fx = (float)(x & 3) * 0.25f, fz = (float)(z & 3) * 0.25f;
    const float *c00 = g + (gx * 5 + gz) * 33, *c01 = g + (gx * 5 + gz + 1) * 33;
    const float *c10 = g + ((gx + 1) * 5 + gz) * 33, *c11 = g + ((gx + 1) * 5 + gz + 1) * 33;
    float v[33];
    for (int k = 0; k < 33; k++) {
        float a = c00[k] + (c10[k] - c00[k]) * fx;
        float b = c01[k] + (c11[k] - c01[k]) * fx;
        v[k] = a + (b - a) * fz;
    }
    for (int k2 = 0; k2 < 32; k2++) {
        float base = v[k2], step = (v[k2 + 1] - v[k2]) * 0.125f;
        int y = k2 * 8;
        if (base <= 0.0f && v[k2 + 1] <= 0.0f && y >= SEA) {
            memset(col + y, B_AIR, 8);
            continue;
        }
        for (int l2 = 0; l2 < 8; l2++, y++) {
            float val = base + (float)l2 * step;
            col[y] = val > 0.0f ? B_STONE : (y < SEA ? B_WATER : B_AIR);
        }
    }
}

/* Terrain and surface of chunk (cx, cz) before caves. Rows y0..y0+h-1 go to out (if out),
 * the column summaries to sum (if sum). Also leaves the biomes in cb4/cb16. */
static void chunk_terrain(int cx, int cz, uint8_t *out, int y0, int h, ColSum *sum) {
    chunk_biomes(cx, cz);
    U.t.mesa_ready = 0;
    chunk_density(cx, cz);
    chunk_surface_noise(cx, cz);
    JRand r;
    jr_seed(&r, (i64)((u64)(i64)cx * 341873128712ULL + (u64)(i64)cz * 132897987541ULL));
    uint8_t *col = U.t.col;
    for (int z = 0; z < 16; z++)
        for (int x = 0; x < 16; x++) {
            column_density(x, z, col);
            const Biome *b = bio(cb16[x + z * 16]);
            build_surface(col, b, &r, cx * 16 + z, cz * 16 + x, U.t.surf[z * 16 + x]);
            if (sum) column_summary(col, &sum[x + z * 16]);
            if (out)
                for (int y = 0; y < h; y++) out[y * 256 + z * 16 + x] = col[y0 + y];
        }
}

/* ======================================================================== */
/* Block properties                                                          */
/* ======================================================================== */

/* flags per block id */
enum {
    BF_OPAQUE = 1,    /* light opacity != 0: counts in the height map */
    BF_SOLID = 2,     /* Material.isSolid() */
    BF_BUILD = 4,     /* Material.isBuildable() */
    BF_LEAVES = 8,
    BF_LOG = 16,
    BF_PLANT = 32,    /* REPLACEABLE_PLANT or PLANT material (flowers, grass, saplings...) */
    BF_CUBE = 64,     /* Block.o(): opaque full cube (leaves included on the server) */
    BF_LIQUID = 128,
    BF_RPLANT = 256   /* Material.REPLACEABLE_PLANT (tall grass, ferns, double plants, vines, dead bush) */
};
static uint16_t bflags[256];

static void bflags_init(void) {
    for (int i = 0; i < 256; i++) bflags[i] = BF_OPAQUE | BF_SOLID | BF_BUILD | BF_CUBE;
    bflags[B_AIR] = 0;
    bflags[B_WATER] = bflags[B_FLOWING_WATER] = BF_OPAQUE | BF_LIQUID;
    bflags[B_LAVA] = bflags[B_FLOWING_LAVA] = BF_LIQUID;
    static const uint8_t leaves[] = {B_LEAVES_OAK, B_LEAVES_SPRUCE, B_LEAVES_BIRCH, B_LEAVES_JUNGLE, B_LEAVES_ACACIA,
                                     B_LEAVES_DARK_OAK};
    for (unsigned i = 0; i < sizeof leaves; i++) bflags[leaves[i]] = BF_OPAQUE | BF_SOLID | BF_BUILD | BF_LEAVES | BF_CUBE;
    static const uint8_t logs[] = {B_LOG_OAK, B_LOG_OAK_X, B_LOG_OAK_Z, B_LOG_OAK_BARK, B_LOG_SPRUCE, B_LOG_SPRUCE_X,
                                   B_LOG_SPRUCE_Z, B_LOG_SPRUCE_BARK, B_LOG_BIRCH, B_LOG_BIRCH_X, B_LOG_BIRCH_Z,
                                   B_LOG_BIRCH_BARK, B_LOG_JUNGLE, B_LOG_JUNGLE_X, B_LOG_JUNGLE_Z, B_LOG_JUNGLE_BARK,
                                   B_LOG_ACACIA, B_LOG_ACACIA_X, B_LOG_ACACIA_Z, B_LOG_ACACIA_BARK, B_LOG_DARK_OAK,
                                   B_LOG_DARK_OAK_X, B_LOG_DARK_OAK_Z, B_LOG_DARK_OAK_BARK};
    for (unsigned i = 0; i < sizeof logs; i++) bflags[logs[i]] |= BF_LOG;
    static const uint8_t plants[] = {B_SAPLING_OAK, B_SAPLING_SPRUCE, B_SAPLING_BIRCH, B_SAPLING_JUNGLE,
                                     B_SAPLING_ACACIA, B_SAPLING_DARK_OAK, B_DEAD_SHRUB, B_TALL_GRASS, B_FERN,
                                     B_DEAD_BUSH, B_DANDELION, B_POPPY, B_BLUE_ORCHID, B_ALLIUM, B_AZURE_BLUET,
                                     B_RED_TULIP, B_ORANGE_TULIP, B_WHITE_TULIP, B_PINK_TULIP, B_OXEYE_DAISY,
                                     B_BROWN_MUSHROOM, B_RED_MUSHROOM, B_SUGAR_CANE, B_VINE, B_LILY_PAD,
                                     B_SUNFLOWER_LOWER, B_SUNFLOWER_UPPER, B_LILAC_LOWER, B_LILAC_UPPER,
                                     B_DOUBLE_GRASS_LOWER, B_DOUBLE_GRASS_UPPER, B_LARGE_FERN_LOWER,
                                     B_LARGE_FERN_UPPER, B_ROSE_BUSH_LOWER, B_ROSE_BUSH_UPPER, B_PEONY_LOWER,
                                     B_PEONY_UPPER};
    for (unsigned i = 0; i < sizeof plants; i++) bflags[plants[i]] = BF_PLANT;
    static const uint8_t rplants[] = {B_DEAD_SHRUB, B_TALL_GRASS, B_FERN, B_DEAD_BUSH, B_VINE, B_SUNFLOWER_LOWER,
                                      B_SUNFLOWER_UPPER, B_LILAC_LOWER, B_LILAC_UPPER, B_DOUBLE_GRASS_LOWER,
                                      B_DOUBLE_GRASS_UPPER, B_LARGE_FERN_LOWER, B_LARGE_FERN_UPPER, B_ROSE_BUSH_LOWER,
                                      B_ROSE_BUSH_UPPER, B_PEONY_LOWER, B_PEONY_UPPER};
    for (unsigned i = 0; i < sizeof rplants; i++) bflags[rplants[i]] |= BF_RPLANT;
    bflags[B_SNOW_LAYER] = 0; /* not solid, opacity 0 */
    bflags[B_ICE] = BF_OPAQUE | BF_SOLID | BF_BUILD;
    bflags[B_PACKED_ICE] = BF_OPAQUE | BF_SOLID | BF_BUILD | BF_CUBE;
    bflags[B_CACTUS] = BF_SOLID | BF_BUILD;
    bflags[B_COBWEB] = BF_OPAQUE;
    bflags[B_MOB_SPAWNER] = BF_SOLID | BF_BUILD;
    bflags[B_CHEST] = BF_SOLID | BF_BUILD;
    bflags[B_GLASS] = BF_SOLID | BF_BUILD;
    bflags[B_PUMPKIN] = BF_OPAQUE | BF_SOLID | BF_BUILD | BF_CUBE;
    bflags[B_SANDSTONE_SLAB] = BF_SOLID | BF_BUILD;
    bflags[B_STONE_SLAB] = BF_SOLID | BF_BUILD;
}

#define IS_AIR(b) ((b) == B_AIR)
#define IS_LEAVES(b) (bflags[b] & BF_LEAVES)
#define IS_LOG(b) (bflags[b] & BF_LOG)
#define IS_SOLID(b) (bflags[b] & BF_SOLID)
#define IS_BUILD(b) (bflags[b] & BF_BUILD)
#define IS_OPAQUE(b) (bflags[b] & BF_OPAQUE)
#define IS_CUBE(b) (bflags[b] & BF_CUBE)
#define IS_PLANT(b) (bflags[b] & BF_PLANT)
#define IS_RPLANT(b) (bflags[b] & BF_RPLANT)
#define IS_WATER(b) ((b) == B_WATER || (b) == B_FLOWING_WATER)
#define IS_DIRTISH(b) ((b) == B_DIRT || (b) == B_COARSE_DIRT || (b) == B_PODZOL)
#define IS_CLAY(b) ((b) == B_HARDENED_CLAY || ((b) >= B_STAINED_CLAY_WHITE && (b) <= B_STAINED_CLAY_BLACK))

/* filler below a top block (for summary-based reads) */
static uint8_t filler_of(uint8_t top) {
    switch (top) {
    case B_SAND: return B_SAND;
    case B_RED_SAND: return B_STAINED_CLAY_ORANGE;
    case B_GRAVEL: return B_GRAVEL;
    case B_STONE: return B_STONE;
    default:
        if (IS_CLAY(top)) return B_STAINED_CLAY_ORANGE;
        return B_DIRT;
    }
}

/* block of a column from its summary (no caves, no decoration) */
static uint8_t sum_block(const ColSum *s, int y) {
    if (y < 0) return B_BEDROCK;
    if (y > s->top) {
        if (y == s->top + 1 && s->blk == B_LILY_PAD) return B_LILY_PAD;
        return B_AIR;
    }
    if (y == 0) return B_BEDROCK;
    if (is_liquid_top(s->blk)) {
        if (y == s->top) return s->blk == B_LILY_PAD ? B_WATER : s->blk;
        if (y > s->aux) return s->blk == B_LAVA ? B_LAVA : B_WATER;
        if (y == s->aux) return B_SAND; /* floor (sand, gravel, dirt or clay: all buildable) */
        return B_STONE;
    }
    if (y == s->top) return s->blk;
    if (y >= s->top - s->aux) return filler_of(s->blk);
    return B_STONE;
}

/* ======================================================================== */
/* Caves and ravines (WorldGenCaves, WorldGenCanyon)                         */
/* ======================================================================== */

/* A carve target: one chunk (tcx, tcz) and, inside it, the region we care about.
 * mode CAVE_OUT: the chunk being generated (blocks in cout / summary csum).
 * mode CAVE_MASK: a query box (world coords) whose carved blocks are marked in a bitmap;
 *   blocks come from the summary of the target chunk. */
enum { CAVE_OUT, CAVE_MASK };

typedef struct {
    int mode;
    int tcx, tcz;
    const ColSum *sum;  /* summary of the target chunk */
    /* CAVE_OUT */
    uint8_t *out;
    int y0, h;
    /* CAVE_MASK: box [bx0,bx1) x [by0,by1) x [bz0,bz1) in world coords, bitmap of carved cells */
    int bx0, bx1, by0, by1, bz0, bz1;
    uint8_t *mask;
    /* region of the chunk that matters (chunk-local, inclusive-exclusive), for early skips */
    int rx0, rx1, rz0, rz1, ry0, ry1;
} CaveCtx;

static CaveCtx *cv;
static float canyon_w[256]; /* WorldGenCanyon.d */
static uint8_t c_b16[256];  /* biomes of the chunk being generated */

static inline int mask_index(int X, int Y, int Z) {
    return ((Y - cv->by0) * (cv->bz1 - cv->bz0) + (Z - cv->bz0)) * (cv->bx1 - cv->bx0) + (X - cv->bx0);
}

/* block at chunk-local (x, y, z) of the target, as the carver sees it */
static uint8_t cv_get(int x, int y, int z) {
    if (y < 0 || y > 255) return B_AIR;
    if (cv->mode == CAVE_OUT) {
        if (y >= cv->y0 && y < cv->y0 + cv->h) return cv->out[(y - cv->y0) * 256 + z * 16 + x];
        return sum_block(&cv->sum[x + z * 16], y);
    }
    int X = cv->tcx * 16 + x, Z = cv->tcz * 16 + z;
    if (X >= cv->bx0 && X < cv->bx1 && y >= cv->by0 && y < cv->by1 && Z >= cv->bz0 && Z < cv->bz1) {
        uint8_t m = cv->mask[mask_index(X, y, Z)];
        if (m) return (uint8_t)(m - 1); /* mask stores block id + 1, 0 = untouched */
    }
    return sum_block(&cv->sum[x + z * 16], y);
}

static void cv_set(int x, int y, int z, uint8_t b) {
    if (y < 0 || y > 255) return;
    if (cv->mode == CAVE_OUT) {
        if (y >= cv->y0 && y < cv->y0 + cv->h) cv->out[(y - cv->y0) * 256 + z * 16 + x] = b;
        return;
    }
    int X = cv->tcx * 16 + x, Z = cv->tcz * 16 + z;
    if (X >= cv->bx0 && X < cv->bx1 && y >= cv->by0 && y < cv->by1 && Z >= cv->bz0 && Z < cv->bz1)
        cv->mask[mask_index(X, y, Z)] = (uint8_t)(b + 1);
}

/* is (x, y, z) water, for the carver's abort test: Java reads the chunk being carved,
 * where water only comes from the terrain (summary-exact) */
static inline int cv_water(int x, int y, int z) {
    uint8_t b = cv_get(x, y, z);
    return b == B_WATER || b == B_FLOWING_WATER;
}

/* position in 32.32 fixed point (world coordinates) */
typedef i64 Q32;
#define Q32_ONE (1LL << 32)
static inline Q32 q32_of_float(float v) { return (Q32)(v * 4294967296.0f); }
static inline float q32_to_float(Q32 v) { return (float)v * (1.0f / 4294967296.0f); }
static inline int q32_floor(Q32 v) { return (int)(v >> 32); }

/* One carve step of a tunnel (cave or canyon) into the target chunk.
 * px,py,pz: centre; r6, r7: horizontal and vertical radii (Q32). */
static int cave_carve(Q32 px, Q32 py, Q32 pz, Q32 r6, Q32 r7, int canyon) {
    int j = cv->tcx, k = cv->tcz;
    int l1 = q32_floor(px - r6) - j * 16 - 1, i2 = q32_floor(px + r6) - j * 16 + 1;
    int j2 = q32_floor(py - r7) - 1, k2 = q32_floor(py + r7) + 1;
    int l2 = q32_floor(pz - r6) - k * 16 - 1, i3 = q32_floor(pz + r6) - k * 16 + 1;
    if (l1 < 0) l1 = 0;
    if (i2 > 16) i2 = 16;
    if (j2 < 1) j2 = 1;
    if (k2 > 248) k2 = 248;
    if (l2 < 0) l2 = 0;
    if (i3 > 16) i3 = 16;
    /* water check (shell of the box plus top and bottom layers): water aborts this step */
    for (int x = l1; x < i2; x++)
        for (int z = l2; z < i3; z++)
            for (int y = k2 + 1; y >= j2 - 1; --y) {
                if (y >= 0 && y < 256) {
                    if (cv_water(x, y, z)) return 0;
                    if (y != j2 - 1 && x != l1 && x != i2 - 1 && z != l2 && z != i3 - 1) y = j2;
                }
            }
    /* skip carving where it cannot touch the region we care about (no other effect) */
    if (l1 >= cv->rx1 || i2 <= cv->rx0 || l2 >= cv->rz1 || i3 <= cv->rz0 || j2 >= cv->ry1 || k2 + 1 < cv->ry0) return 1;
    float d6 = q32_to_float(r6), d7 = q32_to_float(r7);
    for (int x = l1; x < i2; x++) {
        float d12 = q32_to_float(((Q32)(j * 16 + x) << 32) + (Q32_ONE >> 1) - px) / d6;
        for (int z = l2; z < i3; z++) {
            float d13 = q32_to_float(((Q32)(k * 16 + z) << 32) + (Q32_ONE >> 1) - pz) / d6;
            float h2 = d12 * d12 + d13 * d13;
            if (h2 >= 1.0f) continue;
            int flag3 = 0;
            for (int y = k2; y > j2; --y) {
                float d14 = q32_to_float(((Q32)(y - 1) << 32) + (Q32_ONE >> 1) - py) / d7;
                if (canyon) {
                    if (!(h2 * canyon_w[y - 1] + d14 * d14 / 6.0f < 1.0f)) continue;
                    uint8_t b = cv_get(x, y, z);
                    if (b == B_GRASS) flag3 = 1;
                    if (b == B_STONE || IS_DIRTISH(b) || b == B_GRASS) {
                        if (y - 1 < 10) {
                            cv_set(x, y, z, B_LAVA);
                        } else {
                            cv_set(x, y, z, B_AIR);
                            if (flag3 && IS_DIRTISH(cv_get(x, y - 1, z)))
                                cv_set(x, y - 1, z, bio(cv->mode == CAVE_OUT ? c_b16[x + z * 16] : BI_PLAINS)->top);
                        }
                    }
                } else {
                    if (!(d14 > -0.7f && h2 + d14 * d14 < 1.0f)) continue;
                    uint8_t b = cv_get(x, y, z), up = cv_get(x, y + 1, z);
                    if (b == B_GRASS || b == B_MYCELIUM) flag3 = 1;
                    int carvable = b == B_STONE || IS_DIRTISH(b) || b == B_GRASS || IS_CLAY(b) || b == B_SANDSTONE ||
                                   b == B_RED_SANDSTONE || b == B_MYCELIUM || b == B_SNOW_LAYER ||
                                   ((b == B_SAND || b == B_RED_SAND || b == B_GRAVEL) && !IS_WATER(up));
                    if (!carvable) continue;
                    if (y - 1 < 10) {
                        cv_set(x, y, z, B_LAVA);
                    } else {
                        cv_set(x, y, z, B_AIR);
                        if (up == B_SAND) cv_set(x, y + 1, z, B_SANDSTONE);
                        else if (up == B_RED_SAND) cv_set(x, y + 1, z, B_RED_SANDSTONE);
                        if (flag3 && IS_DIRTISH(cv_get(x, y - 1, z))) {
                            uint8_t t = bio(cv->mode == CAVE_OUT ? c_b16[x + z * 16] : BI_PLAINS)->top;
                            if (t == B_PODZOL || t == B_COARSE_DIRT) t = B_DIRT; /* ak.getBlock().getBlockData() */
                            cv_set(x, y - 1, z, t);
                        }
                    }
                }
            }
        }
    }
    return 1;
}


/* MapGenCaves' "cannot reach the chunk any more" test, d8^2 + d9^2 - d10^2 > d11^2, in float,
 * settled in double when the float result is too close to call (the positions are exact) */
static int cave_too_far(Q32 dx, Q32 dz, int d10i, float d11) {
    float d8 = q32_to_float(dx), d9 = q32_to_float(dz), d10 = (float)d10i;
    float lhs = d8 * d8 + d9 * d9 - d10 * d10, rhs = d11 * d11;
    float diff = lhs - rhs;
    if (diff > 0.05f) return 1;
    if (diff < -0.05f) return 0;
    double e8 = (double)dx * (1.0 / 4294967296.0), e9 = (double)dz * (1.0 / 4294967296.0), e10 = d10i, e11 = d11;
    return e8 * e8 + e9 * e9 - e10 * e10 > e11 * e11;
}

/* tunnel stack (branches) */
typedef struct {
    i64 seed;
    Q32 x, y, z;
    float f, f1, f2;
    int16_t l, i1;
    uint8_t room;
} Tunnel;
#define TSTACK 24
static Tunnel tstack[TSTACK];

/* Walks one cave tunnel (WorldGenCaves.a, 13-argument version) and its branches. */
static void cave_tunnel(i64 seed0, Q32 x0, Q32 y0, Q32 z0, float f0, float f10, float f20, int l0, int i10, int room0) {
    int sp = 0;
    tstack[sp++] = (Tunnel){seed0, x0, y0, z0, f0, f10, f20, (int16_t)l0, (int16_t)i10, (uint8_t)room0};
    Q32 cxq = ((Q32)(cv->tcx * 16 + 8)) << 32, czq = ((Q32)(cv->tcz * 16 + 8)) << 32;
    while (sp > 0) {
        Tunnel t = tstack[--sp];
        float f3 = 0, f4 = 0;
        JRand r;
        jr_seed(&r, t.seed);
        int i1 = t.i1, l = t.l;
        if (i1 <= 0) {
            int j1 = 8 * 16 - 16;
            i1 = j1 - jr_int(&r, j1 / 4);
        }
        int flag = 0;
        if (t.room) {
            l = i1 / 2;
            flag = 1;
        }
        int k1 = jr_int(&r, i1 / 2) + i1 / 4;
        int flag1 = jr_int(&r, 6) == 0;
        float f = t.f, f1 = t.f1, f2 = t.f2;
        Q32 d0 = t.x, d1 = t.y, d2 = t.z;
        for (; l < i1; ++l) {
            float sv = mh_sin((float)l * 3.1415927f / (float)i1) * f * 1.0f;
            Q32 r6 = (Q32)(3LL << 31) + q32_of_float(sv); /* 1.5 + ... */
            Q32 r7 = t.room ? r6 / 2 : r6;
            float f5 = mh_cos(f2), f6 = mh_sin(f2);
            d0 += q32_of_float(mh_cos(f1) * f5);
            d1 += q32_of_float(f6);
            d2 += q32_of_float(mh_sin(f1) * f5);
            f2 *= flag1 ? 0.92f : 0.7f;
            f2 += f4 * 0.1f;
            f1 += f3 * 0.1f;
            f4 *= 0.9f;
            f3 *= 0.75f;
            {
                float a = jr_float(&r), b = jr_float(&r), c = jr_float(&r);
                f4 += (a - b) * c * 2.0f;
            }
            {
                float a = jr_float(&r), b = jr_float(&r), c = jr_float(&r);
                f3 += (a - b) * c * 4.0f;
            }
            if (!flag && l == k1 && f > 1.0f && i1 > 0) {
                if (sp + 2 <= TSTACK) {
                    i64 s1 = jr_long(&r);
                    float w1 = jr_float(&r) * 0.5f + 0.5f;
                    i64 s2 = jr_long(&r);
                    float w2 = jr_float(&r) * 0.5f + 0.5f;
                    /* second branch below the first: the first runs (with its sub-branches) first */
                    tstack[sp++] = (Tunnel){s2, d0, d1, d2, w2, f1 + 1.5707964f, f2 / 3.0f, (int16_t)l, (int16_t)i1, 0};
                    tstack[sp++] = (Tunnel){s1, d0, d1, d2, w1, f1 - 1.5707964f, f2 / 3.0f, (int16_t)l, (int16_t)i1, 0};
                }
                break;
            }
            if (flag || jr_int(&r, 4) != 0) {
                float d8 = q32_to_float(d0 - cxq), d9 = q32_to_float(d2 - czq);
                if (cave_too_far(d0 - cxq, d2 - czq, i1 - l, f + 2.0f + 16.0f)) break;
                float lim = 16.0f + q32_to_float(r6) * 2.0f;
                if (d8 >= -lim && d9 >= -lim && d8 <= lim && d9 <= lim) {
                    if (cave_carve(d0, d1, d2, r6, r7, 0) && flag) break;
                }
            }
        }
    }
}

static void canyon_tunnel(i64 seed, Q32 d0, Q32 d1, Q32 d2, float f, float f1, float f2) {
    JRand r;
    jr_seed(&r, seed);
    Q32 cxq = ((Q32)(cv->tcx * 16 + 8)) << 32, czq = ((Q32)(cv->tcz * 16 + 8)) << 32;
    float f3 = 0, f4 = 0;
    int j1 = 8 * 16 - 16;
    int i1 = j1 - jr_int(&r, j1 / 4);
    int l = 0;
    float f5 = 1.0f;
    for (int k1 = 0; k1 < 256; ++k1) {
        if (k1 == 0 || jr_int(&r, 3) == 0) {
            float a = jr_float(&r), b = jr_float(&r);
            f5 = 1.0f + a * b * 1.0f;
        }
        canyon_w[k1] = f5 * f5;
    }
    for (; l < i1; ++l) {
        float sv = mh_sin((float)l * 3.1415927f / (float)i1) * f * 1.0f;
        Q32 r6 = (Q32)(3LL << 31) + q32_of_float(sv);
        Q32 r7 = r6 * 3; /* d3 = 3.0 */
        /* d6 *= nextFloat * 0.25 + 0.75: nextFloat = n / 2^24, factor = (n + 3 * 2^24) / 2^26 */
        i64 n6 = jr_next(&r, 24), n7 = jr_next(&r, 24);
        r6 = (Q32)(((r6 >> 6) * (n6 + 3 * 16777216LL)) >> 20);
        r7 = (Q32)(((r7 >> 6) * (n7 + 3 * 16777216LL)) >> 20);
        float f6 = mh_cos(f2), f7 = mh_sin(f2);
        d0 += q32_of_float(mh_cos(f1) * f6);
        d1 += q32_of_float(f7);
        d2 += q32_of_float(mh_sin(f1) * f6);
        f2 *= 0.7f;
        f2 += f4 * 0.05f;
        f1 += f3 * 0.05f;
        f4 *= 0.8f;
        f3 *= 0.5f;
        {
            float a = jr_float(&r), b = jr_float(&r), c = jr_float(&r);
            f4 += (a - b) * c * 2.0f;
        }
        {
            float a = jr_float(&r), b = jr_float(&r), c = jr_float(&r);
            f3 += (a - b) * c * 4.0f;
        }
        if (jr_int(&r, 4) != 0) {
            float d8 = q32_to_float(d0 - cxq), d9 = q32_to_float(d2 - czq);
            if (cave_too_far(d0 - cxq, d2 - czq, i1 - l, f + 2.0f + 16.0f)) return;
            float lim = 16.0f + q32_to_float(r6) * 2.0f;
            if (d8 >= -lim && d9 >= -lim && d8 <= lim && d9 <= lim) cave_carve(d0, d1, d2, r6, r7, 1);
        }
    }
}

/* MapGenBase.a for both caves and canyons, carving target cv */
static void caves_run(void) {
    int i = cv->tcx, j = cv->tcz;
    JRand r;
    for (int pass = 0; pass < 2; pass++) {
        for (int j1 = i - 8; j1 <= i + 8; ++j1)
            for (int k1 = j - 8; k1 <= j + 8; ++k1) {
                jr_seed(&r, (i64)((u64)(i64)j1 * (u64)cave_mul_x) ^ (i64)((u64)(i64)k1 * (u64)cave_mul_z) ^ g_seed);
                if (pass == 0) {
                    int n = jr_int(&r, jr_int(&r, jr_int(&r, 15) + 1) + 1);
                    if (jr_int(&r, 7) != 0) n = 0;
                    for (int c = 0; c < n; ++c) {
                        int xx = j1 * 16 + jr_int(&r, 16);
                        int yy = jr_int(&r, jr_int(&r, 120) + 8);
                        int zz = k1 * 16 + jr_int(&r, 16);
                        Q32 d0 = (Q32)xx << 32, d1 = (Q32)yy << 32, d2 = (Q32)zz << 32;
                        int k = 1;
                        if (jr_int(&r, 4) == 0) {
                            i64 s = jr_long(&r);
                            float w = 1.0f + jr_float(&r) * 6.0f;
                            cave_tunnel(s, d0, d1, d2, w, 0.0f, 0.0f, -1, -1, 1);
                            k += jr_int(&r, 4);
                        }
                        for (int l1 = 0; l1 < k; ++l1) {
                            float f = jr_float(&r) * 3.1415927f * 2.0f;
                            float f1 = (jr_float(&r) - 0.5f) * 2.0f / 8.0f;
                            float f2 = jr_float(&r) * 2.0f + jr_float(&r);
                            if (jr_int(&r, 10) == 0) {
                                float a = jr_float(&r), b = jr_float(&r);
                                f2 *= a * b * 3.0f + 1.0f;
                            }
                            cave_tunnel(jr_long(&r), d0, d1, d2, f2, f, f1, 0, 0, 0);
                        }
                    }
                } else if (jr_int(&r, 50) == 0) {
                    int xx = j1 * 16 + jr_int(&r, 16);
                    int yy = jr_int(&r, jr_int(&r, 40) + 8) + 20;
                    int zz = k1 * 16 + jr_int(&r, 16);
                    float f = jr_float(&r) * 3.1415927f * 2.0f;
                    float f1 = (jr_float(&r) - 0.5f) * 2.0f / 8.0f;
                    float f2 = (jr_float(&r) * 2.0f + jr_float(&r)) * 2.0f;
                    canyon_tunnel(jr_long(&r), (Q32)xx << 32, (Q32)yy << 32, (Q32)zz << 32, f2, f, f1);
                }
            }
    }
}

/* ======================================================================== */
/* Summary cache                                                             */
/* ======================================================================== */

#define NSUM 12
typedef struct {
    int cx, cz;
    uint32_t age;
    uint8_t valid;
    ColSum s[256];
} SumEntry;
static SumEntry sumc[NSUM];
static uint32_t sum_clock;

static SumEntry *sum_find(int cx, int cz) {
    for (int i = 0; i < NSUM; i++)
        if (sumc[i].valid && sumc[i].cx == cx && sumc[i].cz == cz) {
            sumc[i].age = ++sum_clock;
            return &sumc[i];
        }
    return 0;
}

/* entry to (re)fill: the least recently used one not pinned */
static uint8_t sum_pin[NSUM];
static SumEntry *sum_slot(int cx, int cz) {
    int best = -1;
    for (int i = 0; i < NSUM; i++) {
        if (sum_pin[i]) continue;
        if (!sumc[i].valid) {
            best = i;
            break;
        }
        if (best < 0 || sumc[i].age < sumc[best].age) best = i;
    }
    SumEntry *e = &sumc[best];
    e->cx = cx;
    e->cz = cz;
    e->valid = 1;
    e->age = ++sum_clock;
    return e;
}

static SumEntry *sum_get(int cx, int cz) {
    SumEntry *e = sum_find(cx, cz);
    if (e) return e;
    e = sum_slot(cx, cz);
    chunk_terrain(cx, cz, 0, 0, 0, e->s);
    return e;
}

/* the 3x3 summaries around the chunk being generated, pinned during gen_slab */
static SumEntry *nb[3][3]; /* [dz+1][dx+1] */
static int nb_cx, nb_cz;

static const ColSum *col_sum(int X, int Z) {
    int cx = X >> 4, cz = Z >> 4;
    int dx = cx - nb_cx + 1, dz = cz - nb_cz + 1;
    if (dx < 0 || dx > 2 || dz < 0 || dz > 2) return 0;
    return &nb[dz][dx]->s[(X & 15) + (Z & 15) * 16];
}

/* ======================================================================== */
/* Population: the chunk being generated (C), the view of a population (P)   */
/* ======================================================================== */

/* chunk C: blocks for y in [c_y0, c_y0 + c_h) in c_out; c_top: highest non-air y of each column
 * (whole height), c_sl: highest solid-or-liquid y (precipitation height - 1) */
static uint8_t *c_out;
static int c_y0, c_h, c_cx, c_cz;
static uint8_t c_top[256], c_sl[256];

#define PV (U.p.v)

static inline int lake_bit(const LakeRec *L, int x, int y, int z) {
    int i = (x * 16 + z) * 8 + y;
    return (L->shape[i >> 3] >> (i & 7)) & 1;
}

/* view block at world (X, Y, Z) */
static uint8_t vget(int X, int Y, int Z) {
    if (Y < 0) return B_BEDROCK;
    if (Y > 255) return B_AIR;
    View *v = &PV;
    int lx = X - v->ox, lz = Z - v->oz;
    if (lx < 0) lx = 0;
    if (lx > 31) lx = 31;
    if (lz < 0) lz = 0;
    if (lz > 31) lz = 31;
    X = v->ox + lx;
    Z = v->oz + lz;
    int i = lz * 32 + lx;
    if (Y > v->top[i]) return (Y == v->top[i] + 1 && v->pl[i]) ? v->pl[i] : B_AIR;
    if (Y == v->top[i]) return v->tb[i];
    for (int k = 0; k < v->nlake; k++) {
        const LakeRec *L = &v->lake[k];
        int ax = X - L->x, ay = Y - L->y, az = Z - L->z;
        if (ax >= 0 && ax < 16 && az >= 0 && az < 16 && ay >= 0 && ay < 8 && lake_bit(L, ax, ay, az))
            return ay >= 4 ? B_AIR : L->liquid;
    }
    const ColSum *s = col_sum(X, Z);
    if (!s) return Y < SEA ? B_STONE : B_AIR;
    if (Y > s->top) return B_AIR;
    if (v->qon && X >= v->qx0 && X < v->qx1 && Y >= v->qy0 && Y < v->qy1 && Z >= v->qz0 && Z < v->qz1) {
        uint8_t m = U.p.qmask[((Y - v->qy0) * (v->qz1 - v->qz0) + (Z - v->qz0)) * (v->qx1 - v->qx0) + (X - v->qx0)];
        if (m) return (uint8_t)(m - 1);
    }
    return sum_block(s, Y);
}

static inline int v_empty(int X, int Y, int Z) { return vget(X, Y, Z) == B_AIR; }

/* World.getHighestBlockYAt(...).getY(): first y above the highest opaque block */
static int hm(int X, int Z) {
    View *v = &PV;
    int lx = X - v->ox, lz = Z - v->oz;
    if (lx < 0) lx = 0;
    if (lx > 31) lx = 31;
    if (lz < 0) lz = 0;
    if (lz > 31) lz = 31;
    return v->top[lz * 32 + lx] + 1;
}

/* World.r(pos) (top solid-or-liquid... actually top solid non-leaves block) + 1 */
static int top_solid(int X, int Z) {
    int y = hm(X, Z) + 1;
    for (; y >= 0; y--) {
        uint8_t b = vget(X, y, Z);
        if (IS_SOLID(b) && !IS_LEAVES(b)) break;
    }
    return y + 1;
}

/* write rules: checked against C's real blocks before a write lands in C */
enum {
    R_ALWAYS, R_TREE,     /* air, leaves, replaceable plant */
    R_AIRLEAF, R_NOTCUBE, R_TREEOK, R_AIR, R_STONE, R_DIRTGRASS, R_DIRTCLAY, R_ICE, R_ICE2, R_BUILD,
    R_NOTCHEST, R_WALL, R_SAND_SANDY
};

static int rule_ok(int rule, uint8_t b) {
    switch (rule) {
    case R_ALWAYS: return 1;
    case R_TREE: return b == B_AIR || IS_LEAVES(b) || IS_RPLANT(b);
    case R_AIRLEAF: return b == B_AIR || IS_LEAVES(b);
    case R_NOTCUBE: return !IS_CUBE(b);
    case R_TREEOK:
        return b == B_AIR || IS_LEAVES(b) || b == B_GRASS || IS_DIRTISH(b) || IS_LOG(b) || b == B_VINE ||
               (b >= B_SAPLING_OAK && b <= B_SAPLING_DARK_OAK);
    case R_AIR: return b == B_AIR;
    case R_STONE: return b == B_STONE;
    case R_DIRTGRASS: return IS_DIRTISH(b) || b == B_GRASS;
    case R_DIRTCLAY: return IS_DIRTISH(b) || b == B_CLAY;
    case R_ICE: return b == B_AIR || IS_DIRTISH(b) || b == B_SNOW || b == B_ICE;
    case R_ICE2: return b == B_AIR || IS_DIRTISH(b) || b == B_SNOW || b == B_ICE || b == B_PACKED_ICE;
    case R_BUILD: return IS_BUILD(b);
    case R_NOTCHEST: return b != B_CHEST;
    case R_WALL: return IS_BUILD(b) && b != B_CHEST;
    }
    return 1;
}

static inline int is_plantish(uint8_t b) {
    return IS_PLANT(b) || IS_RPLANT(b) || b == B_CACTUS || b == B_SNOW_LAYER || b == B_SUGAR_CANE;
}

/* top of column (x, z) of C after removing the block at y: next non-air below */
static void c_lower_top(int i, int y) {
    int x = i & 15, z = i >> 4;
    const ColSum *s = &nb[1][1]->s[i];
    while (y > 0) {
        y--;
        uint8_t b = (y >= c_y0 && y < c_y0 + c_h) ? c_out[(y - c_y0) * 256 + z * 16 + x] : sum_block(s, y);
        if (b != B_AIR) break;
    }
    c_top[i] = (uint8_t)y;
}

/* chunk C bookkeeping for a block that was (or, out of the y range, presumably was) written */
static void c_track(int i, int Y, uint8_t b) {
    if (b == B_AIR) {
        if (Y == c_top[i]) c_lower_top(i, Y);
        if (Y == c_sl[i]) c_sl[i] = c_top[i] < c_sl[i] ? c_top[i] : (uint8_t)(Y - 1);
    } else {
        if (Y > c_top[i]) c_top[i] = (uint8_t)Y;
        if ((IS_SOLID(b) || (bflags[b] & BF_LIQUID)) && Y > c_sl[i]) c_sl[i] = (uint8_t)Y;
    }
}

/* writes block b at world (X, Y, Z): into the view, and into C if the rule holds there */
static void put(int X, int Y, int Z, uint8_t b, int rule) {
    if (Y < 0 || Y > 255) return;
    View *v = &PV;
    int lx = X - v->ox, lz = Z - v->oz;
    if (lx >= 0 && lx < 32 && lz >= 0 && lz < 32) {
        int i = lz * 32 + lx;
        if (b == B_AIR) {
            if (Y == v->top[i] + 1) v->pl[i] = 0;
            if (Y == v->top[i]) {
                v->pl[i] = 0;
                int y = Y;
                v->top[i] = (uint8_t)(Y - 1); /* provisional so vget sees below */
                while (y > 0) {
                    y--;
                    v->top[i] = (uint8_t)y;
                    v->tb[i] = B_AIR;
                    /* read what lies below with the column top above it */
                    v->top[i] = 255;
                    uint8_t u = vget(X, y, Z);
                    v->top[i] = (uint8_t)y;
                    if (u != B_AIR) {
                        v->tb[i] = u;
                        break;
                    }
                }
            }
        } else if (is_plantish(b)) {
            if (Y == v->top[i] + 1) v->pl[i] = b;
        } else {
            if (Y > v->top[i]) {
                v->top[i] = (uint8_t)Y;
                v->tb[i] = b;
                v->pl[i] = 0;
            } else if (Y == v->top[i]) {
                v->tb[i] = b;
            }
        }
    }
    int cx = X - c_cx * 16, cz = Z - c_cz * 16;
    if (cx >= 0 && cx < 16 && cz >= 0 && cz < 16) {
        int i = cz * 16 + cx;
        if (Y >= c_y0 && Y < c_y0 + c_h) {
            uint8_t *o = &c_out[(Y - c_y0) * 256 + i];
            if (!rule_ok(rule, *o)) return;
            *o = b;
        }
        c_track(i, Y, b);
    }
}

/* only into C (features whose result never feeds a later decision: ores...) */
static void put_c(int X, int Y, int Z, uint8_t b, int rule) {
    int cx = X - c_cx * 16, cz = Z - c_cz * 16;
    if (cx < 0 || cx > 15 || cz < 0 || cz > 15 || Y < c_y0 || Y >= c_y0 + c_h) return;
    uint8_t *o = &c_out[(Y - c_y0) * 256 + cz * 16 + cx];
    if (rule_ok(rule, *o)) {
        *o = b;
        c_track(cz * 16 + cx, Y, b);
    }
}

/* C's real block if (X, Y, Z) is in C and in range, else the view's */
static uint8_t cget(int X, int Y, int Z) {
    int cx = X - c_cx * 16, cz = Z - c_cz * 16;
    if (cx >= 0 && cx < 16 && cz >= 0 && cz < 16 && Y >= c_y0 && Y < c_y0 + c_h)
        return c_out[(Y - c_y0) * 256 + cz * 16 + cx];
    return vget(X, Y, Z);
}

static inline int in_c_range(int X, int Y, int Z) {
    int cx = X - c_cx * 16, cz = Z - c_cz * 16;
    return cx >= 0 && cx < 16 && cz >= 0 && cz < 16 && Y >= c_y0 && Y < c_y0 + c_h;
}

static inline int in_c_cols(int X, int Z) {
    int cx = X - c_cx * 16, cz = Z - c_cz * 16;
    return cx >= 0 && cx < 16 && cz >= 0 && cz < 16;
}
