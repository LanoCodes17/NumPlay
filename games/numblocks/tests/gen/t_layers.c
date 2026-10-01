#include "../../src/gen.c"
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv) {
    i64 seed = argc > 1 ? strtoll(argv[1], 0, 10) : 12345;
    int R = argc > 2 ? atoi(argv[2]) : 512; /* half size in blocks */
    jr_jump_init();
    g_seed = seed;
    layers_init_test();
    int W = 2 * R;
    static uint8_t img[2048 * 2048 * 3];
    static i32 rm[64 * 64];
    static uint8_t bb[64 * 64];
    for (int tz = -R; tz < R; tz += 64)
        for (int tx = -R; tx < R; tx += 64) {
            int rx, rz, rw, rh;
            voronoi_rm_win(tx, tz, 64, 64, &rx, &rz, &rw, &rh);
            layers_rivermix(rx, rz, rw, rh, rm);
            voronoi(rm, rx, rz, rw, tx, tz, 64, 64, bb, 64);
            for (int j = 0; j < 64; j++)
                for (int i = 0; i < 64; i++) {
                    u32 c = bio(bb[i + j * 64])->color;
                    int px = tx + R + i, pz = tz + R + j;
                    uint8_t *p = img + 3 * (pz * W + px);
                    p[0] = c >> 16; p[1] = c >> 8; p[2] = c;
                }
        }
    printf("P6 %d %d peak lay ints %d\n", W, W, lay_peak);
    FILE *f = fopen(argc > 3 ? argv[3] : "/tmp/claude-0/-home-user-NumPlay/3af2eb1c-20d9-554a-acfb-eb19ce4fa977/scratchpad/gen/biomes.ppm", "wb");
    fprintf(f, "P6 %d %d 255\n", W, W);
    fwrite(img, 1, (size_t)W * W * 3, f);
    fclose(f);
    return 0;
}
