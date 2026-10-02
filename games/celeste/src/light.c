/* Lights and bloom: VertexLight and BloomPoint components register here
 * while their entities render; the lighting layer darkens the view outside
 * them (Celeste's LightingRenderer). */
#include "level.h"

#define MAX_LIGHTS 24
typedef struct { float x, y, alpha, start, end; uint32_t color; } Light;
static Light lights[MAX_LIGHTS];
static int nlights;

void light_add(float x, float y, uint32_t color, float alpha, float start_fade, float end_fade) {
  if (nlights >= MAX_LIGHTS || alpha <= 0) return;
  lights[nlights++] = (Light){x, y, alpha, start_fade, end_fade, color};
}
void bloom_add(float x, float y, float alpha, float radius) { (void)x, (void)y, (void)alpha, (void)radius; }
void lights_begin(void) { nlights = 0; }
int lights_count(void) { return nlights; }
