// doom.c
#include "doom.h"

#include <math.h>
#include <string.h>

#define PI_F 3.14159265358979323846f

static inline int iclamp(int x, int lo, int hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}
static inline float fmax2(float a, float b) { return a > b ? a : b; }
static inline float fmin2(float a, float b) { return a < b ? a : b; }

// xorshift32
static inline uint32_t xs32(uint32_t* s) {
    uint32_t x = (*s == 0) ? 1u : *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}
static inline float rng_f01(uint32_t* s) {
    return (xs32(s) >> 8) * (1.0f / 16777216.0f);
}
static inline int rng_int(uint32_t* s, int n) {
    return (int)(xs32(s) % (uint32_t)n);
}

void doom_fire_without_dsps_init(doom_fire_t* f, uint32_t seed) {
    memset(f, 0, sizeof(*f));
    f->decay = 3.0f;
    f->intensity = 36.0f;
    f->rng = (seed == 0) ? 1u : seed;
}

void doom_fire_without_dsps_reset(doom_fire_t* f) {
    memset(f->heat, 0, sizeof(f->heat));
    memset(f->next, 0, sizeof(f->next));
}

const float* doom_fire_without_dsps_heat(const doom_fire_t* f) { return f->heat; }

static void propagate(doom_fire_t* f, float gravity_x) {
    const float decay = f->decay;
    const float gx = gravity_x * 0.5f;

    const float center_x = (DOOM_W - 1) * 0.5f;
    const float gravity_strength = fabsf(gx);
    const float horizontal_drift = -(gx * 3.0f);

    const float w_up = 3.8f;
    const float w_below = 0.5f;
    const float w_left = fmax2(0.0f, 1.0f + horizontal_drift);
    const float w_right = fmax2(0.0f, 1.0f - horizontal_drift);
    const float total = fmax2(1.0f, w_up + w_below + w_left + w_right);
    const float inv_total = 1.0f / total;
    const float taper_relax = fmax2(0.2f, 1.0f - gravity_strength * 0.3f);
    const int drift_shift = (horizontal_drift == 0.0f)
                                ? 0
                                : ((horizontal_drift > 0.0f) ? 1 : -1) *
                                      (int)ceilf(fabsf(horizontal_drift) *
                                                 0.8f);

    // Rows [0..DOOM_H-2] are overwritten every frame; only clear bottom row.
    memset(&f->next[(DOOM_H - 1) * DOOM_W], 0, sizeof(float) * DOOM_W);

    for (int y = 0; y < DOOM_H - 1; y++) {
        const float rows_from_bottom = (float)((DOOM_H - 1) - y);
        const int src_row = (y + 1) * DOOM_W;
        const int below_row = (y + 2 < DOOM_H) ? ((y + 2) * DOOM_W) : src_row;
        const float lean_shift = horizontal_drift * rows_from_bottom * 0.35f;
        const float effective_center = center_x + lean_shift;
        const float taper_k = (y < DOOM_H - 6)
                                  ? (((float)((DOOM_H - 6) - y)) * 0.045f *
                                     taper_relax)
                                  : 0.0f;

        for (int x = 0; x < DOOM_W; x++) {
            const int left_x = (x > 0) ? (x - 1) : x;
            const int right_x = (x + 1 < DOOM_W) ? (x + 1) : x;

            const float val_up = f->heat[src_row + x];
            const float val_left = f->heat[src_row + left_x];
            const float val_right = f->heat[src_row + right_x];
            const float val_below = f->heat[below_row + x];

            const float avg = (val_up * w_up + val_left * w_left +
                               val_right * w_right + val_below * w_below) *
                              inv_total;

            float cooling = decay * (rng_f01(&f->rng) * 0.4f + 0.8f);
            if (taper_k > 0.0f) {
                const float dist = fabsf((float)x - effective_center);
                cooling += (dist * dist) * taper_k;
            }

            const float new_heat = fmax2(0.0f, avg - cooling);
            f->next[y * DOOM_W + x] = new_heat;

            if (new_heat > 12.0f && rng_f01(&f->rng) < 0.006f) {
                const int jump = 2 + rng_int(&f->rng, 4);
                const int ty = y - jump;

                int tx = x + (rng_int(&f->rng, 3) - 1) + drift_shift;
                tx = iclamp(tx, 0, DOOM_W - 1);

                if (ty >= 0) {
                    const int tidx = ty * DOOM_W + tx;
                    const float spark =
                        fmin2(new_heat * 2.2f + 10.0f, (float)DOOM_HEAT_MAX);
                    if (spark > f->next[tidx]) {
                        f->next[tidx] = spark;
                    }
                }
            }
        }
    }
}

static void ignite(doom_fire_t* f, uint32_t t_ms) {
    const float t = (float)t_ms;

    const float sway = sinf(t / 800.0f) * 1.2f + sinf(t / 350.0f) * 0.5f;
    const float cx = (DOOM_W - 1) * 0.5f + sway;
    const float cy = (float)DOOM_H - 5.5f;
    const float radius = 4.8f;

    const float breathing = sinf(t / 200.0f) * 4.0f;
    const float flicker = rng_f01(&f->rng) * 2.0f;
    const float base_intensity = fmax2(0.0f, f->intensity + breathing - flicker);

    for (int y = DOOM_H - 12; y < DOOM_H; y++) {
        for (int x = 0; x < DOOM_W; x++) {
            const float dx = (float)x - cx;
            const float dy = (float)y - cy;
            const float dist = sqrtf(dx * dx + dy * dy);

            if (dist <= radius) {
                const float falloff = cosf((dist / radius) * (PI_F * 0.5f));
                const float heat = base_intensity * falloff;
                const int idx = y * DOOM_W + x;
                if (heat > f->next[idx]) {
                    f->next[idx] = heat;
                }
            }
        }
    }
}

void doom_fire_without_dsps_step(doom_fire_t* f, float gravity_x,
                                 uint32_t t_ms) {
    propagate(f, gravity_x);
    ignite(f, t_ms);
    memcpy(f->heat, f->next, sizeof(f->heat));
}
