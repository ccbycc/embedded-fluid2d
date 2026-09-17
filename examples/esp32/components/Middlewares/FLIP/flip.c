/*
Portions of this file are adapted/ported from work by:
  Copyright 2022 Matthias Müller - Ten Minute Physics
  www.youtube.com/c/TenMinutePhysics
  www.matthiasMueller.info/tenMinutePhysics

Licensed under the MIT License. See examples/esp32/LICENSE for the full text.

Modifications/port to C:
  Copyright 2026 cccAboy
*/
#include "flip.h"

#include "dsps_mem.h"
#include "dsps_math.h"
#include "esp_heap_caps.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#define LED_VAL_MAX_F 20.0f
#define DENSITY_CLAMP_F 1.2f
#define GAMMA_F 0.6f

// gamma LUT
static bool s_gamma_inited = false;
static uint8_t s_gamma_lut[256];

static void gamma_init_once(void) {
    if (s_gamma_inited)
        return;
    for (int i = 0; i < 256; i++) {
        float x = (float)i / 255.0f;
        s_gamma_lut[i] = (uint8_t)lrintf(powf(x, GAMMA_F) * 255.0f);
    }
    s_gamma_inited = true;
}

static inline int clamp_i(int x, int lo, int hi) {
    if (x < lo)
        return lo;
    if (x > hi)
        return hi;
    return x;
}
static inline float clamp_f(float x, float lo, float hi) {
    if (x < lo)
        return lo;
    if (x > hi)
        return hi;
    return x;
}

struct FlipFluid {
    float density;
    int f_num_x, f_num_y;
    float h;
    float f_inv_spacing;
    int f_num_cells;

    float *u, *v, *du, *dv, *prev_u, *prev_v, *p, *s;
    int32_t* cell_type;

    int max_particles;
    int num_particles;
    float* particle_pos;
    float* particle_vel;
    float* particle_density;
    float particle_rest_density;

    float particle_radius;
    float p_inv_spacing;
    int p_num_x, p_num_y, p_num_cells;
    int32_t* num_cell_particles;
    int32_t* first_cell_particle;
    int32_t* cell_particle_ids;

    int AIR_CELL, FLUID_CELL, SOLID_CELL;

    float gravity_scale;
    int push_iters;
    int pressure_iters;
    float flip_ratio;
};

static void integrate_particles(int n, float* pos, float* vel, float dt,
                                float gx, float gy) {
    const float dgx = gx * dt;
    const float dgy = gy * dt;
    dsps_addc_f32(&vel[0], &vel[0], n, dgx, 2, 2);
    dsps_addc_f32(&vel[1], &vel[1], n, dgy, 2, 2);
    for (int i = 0; i < n; i++) {
        pos[2 * i + 0] += vel[2 * i + 0] * dt;
        pos[2 * i + 1] += vel[2 * i + 1] * dt;
    }
}

static void push_particles_apart(int num_particles, float* pos,
                                 float particle_radius, float p_inv_spacing,
                                 int p_num_x, int p_num_y,
                                 int32_t* num_cell_particles,
                                 int32_t* first_cell_particle,
                                 int32_t* cell_particle_ids, int num_iters) {
    const float min_dist = 2.0f * particle_radius;
    const float min_dist2 = min_dist * min_dist;
    const int p_num_cells = p_num_x * p_num_y;

    // (a) 统计每个网格里有几个粒子
    dsps_memset(num_cell_particles, 0, sizeof(int32_t) * p_num_cells);

    for (int i = 0; i < num_particles; i++) {
        float x = pos[2 * i + 0];
        float y = pos[2 * i + 1];
        int xi = clamp_i((int)(x * p_inv_spacing), 0, p_num_x - 1);
        int yi = clamp_i((int)(y * p_inv_spacing), 0, p_num_y - 1);
        int cell = xi * p_num_y + yi;
        num_cell_particles[cell]++;
    }

    // (b) 前缀和：得到每个 cell 的粒子区间
    int first = 0;
    for (int i = 0; i < p_num_cells; i++) {
        first += num_cell_particles[i];
        first_cell_particle[i] = first;
    }
    first_cell_particle[p_num_cells] = first;

    // (c) 把粒子 id 塞到对应 cell 的区间里（倒填）
    for (int i = 0; i < num_particles; i++) {
        float x = pos[2 * i + 0];
        float y = pos[2 * i + 1];
        int xi = clamp_i((int)(x * p_inv_spacing), 0, p_num_x - 1);
        int yi = clamp_i((int)(y * p_inv_spacing), 0, p_num_y - 1);
        int cell = xi * p_num_y + yi;
        first_cell_particle[cell]--;
        cell_particle_ids[first_cell_particle[cell]] = i;
    }

    // (d) 把粒子推开：避免粒子叠在一起
    for (int it = 0; it < num_iters; it++) {
        for (int i = 0; i < num_particles; i++) {
            float px = pos[2 * i + 0];
            float py = pos[2 * i + 1];

            int pxi = (int)(px * p_inv_spacing);
            int pyi = (int)(py * p_inv_spacing);

            int x0 = MAX(pxi - 1, 0);
            int y0 = MAX(pyi - 1, 0);
            int x1 = MIN(pxi + 1, p_num_x - 1);
            int y1 = MIN(pyi + 1, p_num_y - 1);

            for (int xi = x0; xi <= x1; xi++) {
                for (int yi = y0; yi <= y1; yi++) {
                    int cell = xi * p_num_y + yi;
                    int a = first_cell_particle[cell];
                    int b = first_cell_particle[cell + 1];

                    for (int k = a; k < b; k++) {
                        int id = cell_particle_ids[k];
                        if (id == i)
                            continue;

                        float qx = pos[2 * id + 0];
                        float qy = pos[2 * id + 1];

                        float dx = qx - px;
                        float dy = qy - py;
                        float d2 = dx * dx + dy * dy;

                        if (d2 > min_dist2 || d2 == 0.0f)
                            continue;

                        float d = 0.0f;
                        dsps_sqrt_f32(&d2, &d, 1);
                        float s = (0.5f * (min_dist - d)) / d;
                        dx *= s;
                        dy *= s;

                        pos[2 * i + 0] -= dx;
                        pos[2 * i + 1] -= dy;
                        pos[2 * id + 0] += dx;
                        pos[2 * id + 1] += dy;
                    }
                }
            }
        }
    }
}

static void handle_particle_collisions(int n, float* pos, float* vel,
                                       float f_inv_spacing, int f_num_x,
                                       int f_num_y, float particle_radius) {
    float h = 1.0f / f_inv_spacing;
    float r = particle_radius;

    float min_x = h + r;
    float max_x = (f_num_x - 1) * h - r;
    float min_y = h + r;
    float max_y = (f_num_y - 1) * h - r;

    for (int i = 0; i < n; i++) {
        float x = pos[2 * i + 0];
        float y = pos[2 * i + 1];

        if (x < min_x) {
            x = min_x;
            vel[2 * i + 0] = 0.0f;
        }
        if (x > max_x) {
            x = max_x;
            vel[2 * i + 0] = 0.0f;
        }
        if (y < min_y) {
            y = min_y;
            vel[2 * i + 1] = 0.0f;
        }
        if (y > max_y) {
            y = max_y;
            vel[2 * i + 1] = 0.0f;
        }

        pos[2 * i + 0] = x;
        pos[2 * i + 1] = y;
    }
}

static void update_particle_density(int num_particles, float* pos,
                                    float* particle_density, int f_num_x,
                                    int f_num_y, float h, float f_inv_spacing) {
    const int n = f_num_y;
    const float h2 = 0.5f * h;
    dsps_memset(particle_density, 0, sizeof(float) * (f_num_x * f_num_y));

    for (int i = 0; i < num_particles; i++) {
        float x = pos[2 * i + 0];
        float y = pos[2 * i + 1];

        x = clamp_f(x, h, (f_num_x - 1) * h);
        y = clamp_f(y, h, (f_num_y - 1) * h);

        int x0 = (int)((x - h2) * f_inv_spacing);
        float tx = (x - h2 - x0 * h) * f_inv_spacing;
        int x1 = MIN(x0 + 1, f_num_x - 2);

        int y0 = (int)((y - h2) * f_inv_spacing);
        float ty = (y - h2 - y0 * h) * f_inv_spacing;
        int y1 = MIN(y0 + 1, f_num_y - 2);

        float sx = 1.0f - tx;
        float sy = 1.0f - ty;

        // 双线性加权累加密度
        particle_density[x0 * n + y0] += sx * sy;
        particle_density[x1 * n + y0] += tx * sy;
        particle_density[x1 * n + y1] += tx * ty;
        particle_density[x0 * n + y1] += sx * ty;
    }
}

static float calculate_rest_density(int f_num_cells, const int32_t* cell_type,
                                    const float* particle_density,
                                    int FLUID_CELL) {
    float sum = 0.0f;
    for (int i = 0; i < f_num_cells; i++) {
        sum += particle_density[i];
    }
    int cnt = 0;
    for (int i = 0; i < f_num_cells; i++) {
        if (cell_type[i] == FLUID_CELL) {
            cnt++;
        }
    }
    return (cnt > 0) ? (sum / (float)cnt) : 0.0f;
}

static void transfer_velocities(int to_grid, float flip_ratio,
                                int num_particles, float* pos, float* vel,
                                float* u, float* v, float* du, float* dv,
                                float* prev_u, float* prev_v,
                                int32_t* cell_type, const float* s, int f_num_x,
                                int f_num_y, float h, float f_inv_spacing,
                                int AIR_CELL, int FLUID_CELL, int SOLID_CELL) {
    const int n = f_num_y;
    const float h2 = 0.5f * h;

    if (to_grid) {
        size_t bytes = sizeof(float) * (size_t)f_num_x * (size_t)f_num_y;
        dsps_memcpy(prev_u, u, bytes);
        dsps_memcpy(prev_v, v, bytes);
        dsps_memset(du, 0, bytes);
        dsps_memset(dv, 0, bytes);
        dsps_memset(u, 0, bytes);
        dsps_memset(v, 0, bytes);

        // 根据固体 s[] 设置 cell_type（边界 SOLID，其它 AIR）
        for (int i = 0; i < f_num_x * f_num_y; i++) {
            cell_type[i] = (s[i] == 0.0f) ? SOLID_CELL : AIR_CELL;
        }
        // 粒子所在 cell -> FLUID
        for (int i = 0; i < num_particles; i++) {
            float x = pos[2 * i + 0];
            float y = pos[2 * i + 1];
            int xi = clamp_i((int)(x * f_inv_spacing), 0, f_num_x - 1);
            int yi = clamp_i((int)(y * f_inv_spacing), 0, f_num_y - 1);
            int cell = xi * n + yi;
            if (cell_type[cell] == AIR_CELL)
                cell_type[cell] = FLUID_CELL;
        }
    }

    // 分两次：component=0 处理 u，component=1 处理 v
    for (int component = 0; component < 2; component++) {
        float dx = (component == 0) ? 0.0f : h2;
        float dy = (component == 0) ? h2 : 0.0f;

        float* f = (component == 0) ? u : v;
        float* prev_f = (component == 0) ? prev_u : prev_v;
        float* d = (component == 0) ? du : dv;

        for (int i = 0; i < num_particles; i++) {
            float x = pos[2 * i + 0];
            float y = pos[2 * i + 1];

            x = clamp_f(x, h, (f_num_x - 1) * h);
            y = clamp_f(y, h, (f_num_y - 1) * h);

            int x0 = MIN((int)((x - dx) * f_inv_spacing), f_num_x - 2);
            float tx = (x - dx - x0 * h) * f_inv_spacing;
            int x1 = MIN(x0 + 1, f_num_x - 2);

            int y0 = MIN((int)((y - dy) * f_inv_spacing), f_num_y - 2);
            float ty = (y - dy - y0 * h) * f_inv_spacing;
            int y1 = MIN(y0 + 1, f_num_y - 2);

            float sx = 1.0f - tx;
            float sy = 1.0f - ty;

            float w0 = sx * sy;
            float w1 = tx * sy;
            float w2 = tx * ty;
            float w3 = sx * ty;

            int nr0 = x0 * n + y0;
            int nr1 = x1 * n + y0;
            int nr2 = x1 * n + y1;
            int nr3 = x0 * n + y1;

            if (to_grid) {
                float pv = vel[2 * i + component];
                f[nr0] += pv * w0;
                d[nr0] += w0;
                f[nr1] += pv * w1;
                d[nr1] += w1;
                f[nr2] += pv * w2;
                d[nr2] += w2;
                f[nr3] += pv * w3;
                d[nr3] += w3;
            } else {
                int offset = (component == 0) ? n : 1;
                float valid0 = (cell_type[nr0] != AIR_CELL ||
                                cell_type[nr0 - offset] != AIR_CELL)
                                   ? 1.0f
                                   : 0.0f;
                float valid1 = (cell_type[nr1] != AIR_CELL ||
                                cell_type[nr1 - offset] != AIR_CELL)
                                   ? 1.0f
                                   : 0.0f;
                float valid2 = (cell_type[nr2] != AIR_CELL ||
                                cell_type[nr2 - offset] != AIR_CELL)
                                   ? 1.0f
                                   : 0.0f;
                float valid3 = (cell_type[nr3] != AIR_CELL ||
                                cell_type[nr3 - offset] != AIR_CELL)
                                   ? 1.0f
                                   : 0.0f;

                float v_curr = vel[2 * i + component];
                float d_sum =
                    valid0 * w0 + valid1 * w1 + valid2 * w2 + valid3 * w3;

                if (d_sum > 0.0f) {
                    float pic_v =
                        (valid0 * w0 * f[nr0] + valid1 * w1 * f[nr1] +
                         valid2 * w2 * f[nr2] + valid3 * w3 * f[nr3]) /
                        d_sum;

                    float corr = (valid0 * w0 * (f[nr0] - prev_f[nr0]) +
                                  valid1 * w1 * (f[nr1] - prev_f[nr1]) +
                                  valid2 * w2 * (f[nr2] - prev_f[nr2]) +
                                  valid3 * w3 * (f[nr3] - prev_f[nr3])) /
                                 d_sum;

                    float flip_v = v_curr + corr;
                    vel[2 * i + component] =
                        (1.0f - flip_ratio) * pic_v + flip_ratio * flip_v;
                }
            }
        }

        if (to_grid) {
            // 归一化：
            for (int i = 0; i < f_num_x * f_num_y; i++) {
                if (d[i] > 0.0f)
                    f[i] /= d[i];
            }

            // 固体边界修正
            for (int i = 0; i < f_num_x; i++) {
                for (int j = 0; j < f_num_y; j++) {
                    int idx = i * n + j;
                    int is_solid = (cell_type[idx] == SOLID_CELL);
                    if (is_solid ||
                        (i > 0 && cell_type[(i - 1) * n + j] == SOLID_CELL)) {
                        u[idx] = prev_u[idx];
                    }
                    if (is_solid ||
                        (j > 0 && cell_type[i * n + (j - 1)] == SOLID_CELL)) {
                        v[idx] = prev_v[idx];
                    }
                }
            }
        }
    }
}

static void solve_incompressibility(
    int num_iters, float dt, float over_relaxation, int compensate_drift,
    float* p, float* u, float* v, float* prev_u, float* prev_v, const float* s,
    const int32_t* cell_type, const float* particle_density,
    float particle_rest_density, int f_num_x, int f_num_y, float h,
    float density, int FLUID_CELL) {
    const int n = f_num_y;
    size_t bytes = sizeof(float) * (size_t)f_num_x * (size_t)f_num_y;
    dsps_memset(p, 0, bytes);
    dsps_memcpy(prev_u, u, bytes);
    dsps_memcpy(prev_v, v, bytes);

    float cp = (density * h) / dt;

    for (int it = 0; it < num_iters; it++) {
        for (int i = 1; i < f_num_x - 1; i++) {
            for (int j = 1; j < f_num_y - 1; j++) {
                int center = i * n + j;
                if (cell_type[center] != FLUID_CELL)
                    continue;

                int left = (i - 1) * n + j;
                int right = (i + 1) * n + j;
                int bottom = i * n + (j - 1);
                int top = i * n + (j + 1);

                float sx0 = s[left], sx1 = s[right];
                float sy0 = s[bottom], sy1 = s[top];
                float s_sum = sx0 + sx1 + sy0 + sy1;
                if (s_sum == 0.0f)
                    continue;

                float div = (u[right] - u[center]) + (v[top] - v[center]);

                if (particle_rest_density > 0.0f && compensate_drift) {
                    float compression =
                        particle_density[center] - particle_rest_density;
                    if (compression > 0.0f) {
                        div -= 1.0f * compression;
                    }
                }

                float p_val = -div / s_sum;
                p_val *= over_relaxation;
                p[center] += cp * p_val;

                u[center] -= sx0 * p_val;
                u[right] += sx1 * p_val;
                v[center] -= sy0 * p_val;
                v[top] += sy1 * p_val;
            }
        }
    }
}

static void get_led_grid(const FlipFluid* f, float* out_grid, int visible_x,
                         int visible_y) {
    const int padding = 1;
    const int n = f->f_num_y;

    for (int i = 0; i < visible_x; i++) {
        for (int j = 0; j < visible_y; j++) {
            int sim_i = i + padding;
            int sim_j = j + padding;
            int idx = sim_i * n + sim_j;

            float d = f->particle_density[idx];
            if (f->particle_rest_density > 0.0f) {
                d /= f->particle_rest_density;
            }

            // 归一化
            float b = d / DENSITY_CLAMP_F;
            if (b < 0.0f)
                b = 0.0f;
            if (b > 1.0f)
                b = 1.0f;

            uint8_t bi = (uint8_t)lrintf(b * 255.0f);
            uint8_t bg = s_gamma_lut[bi];
            out_grid[i * visible_y + j] = (float)bg * (LED_VAL_MAX_F / 255.0f);
        }
    }
}

// 对外 API
void flip_set_gravity_scale(FlipFluid* f, float gravity_scale) {
    if (!f)
        return;
    f->gravity_scale = gravity_scale;
}

void flip_set_solver_quality(FlipFluid* f, int push_iters, int pressure_iters,
                             float flip_ratio) {
    if (!f)
        return;

    if (push_iters < 1)
        push_iters = 1;
    if (push_iters > 4)
        push_iters = 4;

    if (pressure_iters < 4)
        pressure_iters = 4;
    if (pressure_iters > 40)
        pressure_iters = 40;

    if (flip_ratio < 0.0f)
        flip_ratio = 0.0f;
    if (flip_ratio > 1.0f)
        flip_ratio = 1.0f;

    f->push_iters = push_iters;
    f->pressure_iters = pressure_iters;
    f->flip_ratio = flip_ratio;
}

static int alloc_floats(float** p, int count) {
    size_t bytes = (size_t)count * sizeof(float);
    *p = (float*)heap_caps_aligned_alloc(
        16, bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (*p) {
        dsps_memset(*p, 0, bytes);
        return 1;
    }
    return 0;
}
static int alloc_i32(int32_t** p, int count) {
    *p = (int32_t*)calloc((size_t)count, sizeof(int32_t));
    return (*p != NULL);
}

FlipFluid* flip_create(float sim_w, float sim_h, int visible_res,
                       float fill_ratio) {
    int sim_res = visible_res + 2;
    float tank_w = sim_w;
    float tank_h = sim_h;

    float h = tank_h / (float)(sim_res - 1);
    float density = 1000.0f;

    float rel_water_h = fill_ratio;
    float rel_water_w = 0.8f;

    float r = 0.35f * h;
    float dx = 2.0f * r;
    float dy = (sqrtf(3.0f) / 2.0f) * dx;

    int num_x = (int)floorf((rel_water_w * tank_w - 2.0f * h - 2.0f * r) / dx);
    int num_y = (int)floorf((rel_water_h * tank_h - 2.0f * h - 2.0f * r) / dy);
    int base_particles = MAX(num_x * num_y, 1);
    int max_particles = MAX(base_particles + 256, base_particles * 2);

    FlipFluid* f = (FlipFluid*)calloc(1, sizeof(FlipFluid));
    if (!f)
        return NULL;

    f->density = density;
    f->f_num_x = (int)floorf(tank_w / h) + 1;
    f->f_num_y = (int)floorf(tank_h / h) + 1;
    f->h = MAX(tank_w / f->f_num_x, tank_h / f->f_num_y);
    f->f_inv_spacing = 1.0f / f->h;
    f->f_num_cells = f->f_num_x * f->f_num_y;

    f->max_particles = max_particles;
    f->num_particles = 0;

    f->particle_radius = r;
    f->p_inv_spacing = 1.0f / (2.2f * r);
    f->p_num_x = (int)floorf(tank_w * f->p_inv_spacing) + 1;
    f->p_num_y = (int)floorf(tank_h * f->p_inv_spacing) + 1;
    f->p_num_cells = f->p_num_x * f->p_num_y;

    f->AIR_CELL = 0;
    f->FLUID_CELL = 1;
    f->SOLID_CELL = 2;

    f->gravity_scale = 9.81f;
    f->push_iters = 1;
    f->pressure_iters = 12;
    f->flip_ratio = 0.9f;

    // 分配内存
    if (!alloc_floats(&f->u, f->f_num_cells) ||
        !alloc_floats(&f->v, f->f_num_cells) ||
        !alloc_floats(&f->du, f->f_num_cells) ||
        !alloc_floats(&f->dv, f->f_num_cells) ||
        !alloc_floats(&f->prev_u, f->f_num_cells) ||
        !alloc_floats(&f->prev_v, f->f_num_cells) ||
        !alloc_floats(&f->p, f->f_num_cells) ||
        !alloc_floats(&f->s, f->f_num_cells) ||
        !alloc_i32(&f->cell_type, f->f_num_cells) ||
        !alloc_floats(&f->particle_pos, 2 * f->max_particles) ||
        !alloc_floats(&f->particle_vel, 2 * f->max_particles) ||
        !alloc_floats(&f->particle_density, f->f_num_cells) ||
        !alloc_i32(&f->num_cell_particles, f->p_num_cells) ||
        !alloc_i32(&f->first_cell_particle, f->p_num_cells + 1) ||
        !alloc_i32(&f->cell_particle_ids, f->max_particles)) {
        flip_destroy(f);
        return NULL;
    }
    // 初始化
    f->num_particles = num_x * num_y;
    int p_idx = 0;
    for (int i = 0; i < num_x; i++) {
        for (int j = 0; j < num_y; j++) {
            f->particle_pos[p_idx + 0] =
                h + r + dx * i + ((j % 2 == 0) ? 0.0f : r);
            f->particle_pos[p_idx + 1] = h + r + dy * j;
            p_idx += 2;
        }
    }

    int n = f->f_num_y;
    for (int i = 0; i < f->f_num_x; i++) {
        for (int j = 0; j < f->f_num_y; j++) {
            float ss = 1.0f;
            if (i == 0 || i == f->f_num_x - 1 || j == 0 ||
                j == f->f_num_y - 1) {
                ss = 0.0f;
            }
            f->s[i * n + j] = ss;
        }
    }

    f->particle_rest_density = 0.0f;

    gamma_init_once();

    return f;
}

void flip_destroy(FlipFluid* f) {
    if (!f)
        return;
    free(f->u);
    free(f->v);
    free(f->du);
    free(f->dv);
    free(f->prev_u);
    free(f->prev_v);
    free(f->p);
    free(f->s);
    free(f->cell_type);
    free(f->particle_pos);
    free(f->particle_vel);
    free(f->particle_density);
    free(f->num_cell_particles);
    free(f->first_cell_particle);
    free(f->cell_particle_ids);
    free(f);
}

void flip_step(FlipFluid* f, float dt, float gx, float gy) {
    if (!f)
        return;

    float Gx = gx * f->gravity_scale;
    float Gy = gy * f->gravity_scale;

    integrate_particles(f->num_particles, f->particle_pos, f->particle_vel, dt,
                        Gx, Gy);

    push_particles_apart(f->num_particles, f->particle_pos, f->particle_radius,
                         f->p_inv_spacing, f->p_num_x, f->p_num_y,
                         f->num_cell_particles, f->first_cell_particle,
                         f->cell_particle_ids, f->push_iters);

    handle_particle_collisions(f->num_particles, f->particle_pos,
                               f->particle_vel, f->f_inv_spacing, f->f_num_x,
                               f->f_num_y, f->particle_radius);

    transfer_velocities(1, f->flip_ratio, f->num_particles,
                        f->particle_pos, f->particle_vel, f->u, f->v, f->du,
                        f->dv, f->prev_u, f->prev_v, f->cell_type, f->s,
                        f->f_num_x, f->f_num_y, f->h, f->f_inv_spacing,
                        f->AIR_CELL, f->FLUID_CELL, f->SOLID_CELL);

    update_particle_density(f->num_particles, f->particle_pos,
                            f->particle_density, f->f_num_x, f->f_num_y, f->h,
                            f->f_inv_spacing);

    if (f->particle_rest_density == 0.0f) {
        f->particle_rest_density = calculate_rest_density(
            f->f_num_cells, f->cell_type, f->particle_density, f->FLUID_CELL);
    }

    solve_incompressibility(
        f->pressure_iters, dt, /*over_relaxation=*/1.9f, /*compensate_drift=*/1,
        f->p, f->u, f->v, f->prev_u, f->prev_v, f->s, f->cell_type,
        f->particle_density, f->particle_rest_density, f->f_num_x, f->f_num_y,
        f->h, f->density, f->FLUID_CELL);

    transfer_velocities(0, f->flip_ratio, f->num_particles,
                        f->particle_pos, f->particle_vel, f->u, f->v, f->du,
                        f->dv, f->prev_u, f->prev_v, f->cell_type, f->s,
                        f->f_num_x, f->f_num_y, f->h, f->f_inv_spacing,
                        f->AIR_CELL, f->FLUID_CELL, f->SOLID_CELL);
}

void flip_get_led_grid(const FlipFluid* f, float* out_grid) {
    if (!f || !out_grid)
        return;

    int visible_x = f->f_num_x - 2;
    int visible_y = f->f_num_y - 2;
    get_led_grid(f, out_grid, visible_x, visible_y);
}
