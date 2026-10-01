/* NumBlocks world generator: a port of Minecraft 1.8.8's default overworld
 * generator (ChunkProviderGenerate and everything it calls), sized for the
 * NumWorks calculator (all state static, no malloc, about 28 KB of RAM).
 * See the comment at the top of gen.c for what differs from Minecraft. */
#ifndef NB_GEN_H
#define NB_GEN_H
#include <stdint.h>

/* Starts a world: seed as a Java long (what /seed prints). Call before anything else. */
void gen_init(int64_t seed);

/* Blocks of chunk (cx, cz) for y in [y0, y0 + h), after terrain, caves, ravines
 * and the decoration (population) that Minecraft would put in this chunk:
 * out[(y - y0) * 256 + z * 16 + x], x and z in 0..15, values from blocks.h (B_*).
 * Only y < 128 is ever written (y0 + h must be <= 128). */
void gen_slab(int cx, int cz, int y0, int h, uint8_t *out);

/* Minecraft biome id at block (x, z) (0 ocean, 1 plains, ... 129+ mutated). */
int gen_biome(int x, int z);

/* y (0..127) of the highest non-air block of column (x, z) after generation
 * (including trees and other decoration), or -1 if the column is empty. */
int gen_top(int x, int z);

/* The world spawn point, as Minecraft chooses it (WorldChunkManager.findBiomePosition
 * then the random walk until the top block is grass); y is the first air block above it. */
void gen_spawn(int *x, int *y, int *z);

/* Statistics for tests: bytes of static RAM the generator uses. */
unsigned gen_ram_bytes(void);

#endif
