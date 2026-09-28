/*
Portions of this module are adapted/ported from work by:
  Copyright 2022 Matthias Mueller - Ten Minute Physics
  www.youtube.com/c/TenMinutePhysics
  www.matthiasMueller.info/tenMinutePhysics

Licensed under the MIT License. See examples/esp32/LICENSE for the full text.
*/
#include <fluid2d/fluid_solver.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>

namespace fluid2d {
namespace {

enum CellType {
    AirCell = 0,
    FluidCell = 1,
    SolidCell = 2
};

struct DerivedConfig {
    int grid_x;
    int grid_y;
    int grid_cells;
    float nominal_spacing;
    float spacing;
    float inverse_spacing;
    float particle_radius;
    float particle_inverse_spacing;
    int particle_grid_x;
    int particle_grid_y;
    int particle_grid_cells;
    int initial_particles_x;
    int initial_particles_y;
    int initial_particle_count;
};

struct SolverState {
    Config config;
    DerivedConfig derived;

    float gravity_x;
    float gravity_y;
    int particle_count;
    float particle_rest_density;

    float* u;
    float* v;
    float* du;
    float* dv;
    float* previous_u;
    float* previous_v;
    float* pressure;
    float* solid_mask;
    std::int32_t* cell_type;

    float* particle_position;
    float* particle_velocity;
    float* particle_density;

    std::int32_t* particle_cell_count;
    std::int32_t* first_particle_in_cell;
    std::int32_t* particle_ids;
};

const std::size_t kWorkspaceAlignment = alignof(SolverState);
const float kGridSpacingTolerance = 1.0e-4f;

int clampInt(int value, int low, int high) {
    if (value < low) {
        return low;
    }
    if (value > high) {
        return high;
    }
    return value;
}

float clampFloat(float value, float low, float high) {
    if (value < low) {
        return low;
    }
    if (value > high) {
        return high;
    }
    return value;
}

bool finitePositive(float value) {
    return std::isfinite(value) && value > 0.0f;
}

bool checkedMultiply(std::size_t left, std::size_t right,
                     std::size_t* result) {
    if (left != 0U && right > std::numeric_limits<std::size_t>::max() / left) {
        return false;
    }
    *result = left * right;
    return true;
}

bool checkedAdd(std::size_t left, std::size_t right, std::size_t* result) {
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        return false;
    }
    *result = left + right;
    return true;
}

std::size_t alignUp(std::size_t value, std::size_t alignment) {
    const std::size_t mask = alignment - 1U;
    return (value + mask) & ~mask;
}

bool addBlock(std::size_t count, std::size_t element_size,
              std::size_t alignment, std::size_t* offset) {
    if (*offset > std::numeric_limits<std::size_t>::max() - (alignment - 1U)) {
        return false;
    }
    const std::size_t aligned = alignUp(*offset, alignment);
    std::size_t bytes = 0U;
    if (!checkedMultiply(count, element_size, &bytes)) {
        return false;
    }
    return checkedAdd(aligned, bytes, offset);
}

bool computeDerived(const Config& config, DerivedConfig* derived) {
    if (!finitePositive(config.simulation_width) ||
        !finitePositive(config.simulation_height) || config.grid_width == 0U ||
        config.grid_height == 0U || config.max_particles == 0U ||
        config.max_particles >
            static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
        !finitePositive(config.fluid_density) ||
        !finitePositive(config.particle_radius_factor) ||
        !finitePositive(config.neighbor_spacing_factor) ||
        !std::isfinite(config.gravity_scale) ||
        !std::isfinite(config.flip_ratio) || config.flip_ratio < 0.0f ||
        config.flip_ratio > 1.0f ||
        !finitePositive(config.pressure_over_relaxation) ||
        config.particle_push_iterations == 0U ||
        config.pressure_iterations == 0U ||
        !finitePositive(config.initial_fill_width) ||
        config.initial_fill_width > 1.0f ||
        !finitePositive(config.initial_fill_height) ||
        config.initial_fill_height > 1.0f) {
        return false;
    }

    const std::uint32_t grid_x =
        static_cast<std::uint32_t>(config.grid_width) + 2U;
    const std::uint32_t grid_y =
        static_cast<std::uint32_t>(config.grid_height) + 2U;
    const std::uint64_t grid_cells =
        static_cast<std::uint64_t>(grid_x) * grid_y;
    if (grid_cells > static_cast<std::uint64_t>(
                         std::numeric_limits<int>::max())) {
        return false;
    }

    const float nominal_x =
        config.simulation_width / static_cast<float>(config.grid_width + 1U);
    const float nominal_y =
        config.simulation_height / static_cast<float>(config.grid_height + 1U);
    const float nominal_spacing =
        nominal_x > nominal_y ? nominal_x : nominal_y;
    const float spacing_x = config.simulation_width / static_cast<float>(grid_x);
    const float spacing_y = config.simulation_height / static_cast<float>(grid_y);
    const float spacing = spacing_x > spacing_y ? spacing_x : spacing_y;
    if (std::fabs(spacing_x - spacing_y) >
        spacing * kGridSpacingTolerance) {
        return false;
    }
    const float radius = config.particle_radius_factor * nominal_spacing;
    const float neighbor_spacing = config.neighbor_spacing_factor * radius;
    if (!finitePositive(nominal_spacing) || !finitePositive(spacing) ||
        !finitePositive(radius) || !finitePositive(neighbor_spacing)) {
        return false;
    }
    const float interior_width =
        static_cast<float>(grid_x - 2U) * spacing;
    const float interior_height =
        static_cast<float>(grid_y - 2U) * spacing;
    if (interior_width <= 2.0f * radius ||
        interior_height <= 2.0f * radius) {
        return false;
    }

    const float inverse_neighbor_spacing = 1.0f / neighbor_spacing;
    if (!finitePositive(inverse_neighbor_spacing)) {
        return false;
    }
    const double particle_grid_x_value =
        std::floor(static_cast<double>(config.simulation_width) *
                   inverse_neighbor_spacing) +
        1.0;
    const double particle_grid_y_value =
        std::floor(static_cast<double>(config.simulation_height) *
                   inverse_neighbor_spacing) +
        1.0;
    if (particle_grid_x_value < 1.0 || particle_grid_y_value < 1.0 ||
        particle_grid_x_value > std::numeric_limits<int>::max() ||
        particle_grid_y_value > std::numeric_limits<int>::max()) {
        return false;
    }

    const int particle_grid_x = static_cast<int>(particle_grid_x_value);
    const int particle_grid_y = static_cast<int>(particle_grid_y_value);
    const std::int64_t particle_grid_cells =
        static_cast<std::int64_t>(particle_grid_x) * particle_grid_y;
    if (particle_grid_cells > std::numeric_limits<int>::max()) {
        return false;
    }

    const float particle_dx = 2.0f * radius;
    const float particle_dy = 0.5f * std::sqrt(3.0f) * particle_dx;
    const double initial_x_value = std::floor(
        (static_cast<double>(config.initial_fill_width) *
             config.simulation_width -
         2.0 * nominal_spacing - 2.0 * radius) /
        particle_dx);
    const double initial_y_value = std::floor(
        (static_cast<double>(config.initial_fill_height) *
             config.simulation_height -
         2.0 * nominal_spacing - 2.0 * radius) /
        particle_dy);
    if (!std::isfinite(initial_x_value) || !std::isfinite(initial_y_value) ||
        initial_x_value <= 0.0 || initial_y_value <= 0.0 ||
        initial_x_value > std::numeric_limits<int>::max() ||
        initial_y_value > std::numeric_limits<int>::max()) {
        return false;
    }
    const int initial_x = static_cast<int>(initial_x_value);
    const int initial_y = static_cast<int>(initial_y_value);
    const std::int64_t initial_count =
        static_cast<std::int64_t>(initial_x) * initial_y;
    if (initial_count > config.max_particles ||
        initial_count > std::numeric_limits<int>::max()) {
        return false;
    }

    derived->grid_x = static_cast<int>(grid_x);
    derived->grid_y = static_cast<int>(grid_y);
    derived->grid_cells = static_cast<int>(grid_cells);
    derived->nominal_spacing = nominal_spacing;
    derived->spacing = spacing;
    derived->inverse_spacing = 1.0f / spacing;
    derived->particle_radius = radius;
    derived->particle_inverse_spacing = inverse_neighbor_spacing;
    derived->particle_grid_x = particle_grid_x;
    derived->particle_grid_y = particle_grid_y;
    derived->particle_grid_cells = static_cast<int>(particle_grid_cells);
    derived->initial_particles_x = initial_x;
    derived->initial_particles_y = initial_y;
    derived->initial_particle_count = static_cast<int>(initial_count);
    return true;
}

bool layoutSize(const Config& config, const DerivedConfig& derived,
                std::size_t* size) {
    std::size_t offset = 0U;
    if (!addBlock(1U, sizeof(SolverState), alignof(SolverState), &offset)) {
        return false;
    }

    const std::size_t grid_cells =
        static_cast<std::size_t>(derived.grid_cells);
    const std::size_t particle_grid_cells =
        static_cast<std::size_t>(derived.particle_grid_cells);
    const std::size_t max_particles = config.max_particles;

    for (int array = 0; array < 9; ++array) {
        if (!addBlock(grid_cells, sizeof(float), alignof(float), &offset)) {
            return false;
        }
    }
    if (!addBlock(grid_cells, sizeof(std::int32_t), alignof(std::int32_t),
                  &offset) ||
        !addBlock(max_particles * 2U, sizeof(float), alignof(float), &offset) ||
        !addBlock(max_particles * 2U, sizeof(float), alignof(float), &offset) ||
        !addBlock(particle_grid_cells, sizeof(std::int32_t),
                  alignof(std::int32_t), &offset) ||
        !addBlock(particle_grid_cells + 1U, sizeof(std::int32_t),
                  alignof(std::int32_t), &offset) ||
        !addBlock(max_particles, sizeof(std::int32_t), alignof(std::int32_t),
                  &offset)) {
        return false;
    }
    *size = offset;
    return true;
}

class WorkspaceCursor {
public:
    WorkspaceCursor(unsigned char* data, std::size_t size)
        : data_(data), size_(size), offset_(0U) {}

    template <typename T>
    T* take(std::size_t count) {
        const std::size_t aligned = alignUp(offset_, alignof(T));
        std::size_t bytes = 0U;
        std::size_t end = 0U;
        if (!checkedMultiply(count, sizeof(T), &bytes) ||
            !checkedAdd(aligned, bytes, &end) || end > size_) {
            return NULL;
        }
        T* result = reinterpret_cast<T*>(data_ + aligned);
        offset_ = end;
        return result;
    }

private:
    unsigned char* data_;
    std::size_t size_;
    std::size_t offset_;
};

SolverState* state(void* value) {
    return static_cast<SolverState*>(value);
}

void zeroFloatArray(float* values, std::size_t count) {
    std::memset(values, 0, count * sizeof(float));
}

void zeroIntArray(std::int32_t* values, std::size_t count) {
    std::memset(values, 0, count * sizeof(std::int32_t));
}

void initializeState(SolverState* solver) {
    const DerivedConfig& d = solver->derived;
    const Config& config = solver->config;
    const std::size_t grid_cells = static_cast<std::size_t>(d.grid_cells);
    const std::size_t max_particles = config.max_particles;
    const std::size_t particle_grid_cells =
        static_cast<std::size_t>(d.particle_grid_cells);

    zeroFloatArray(solver->u, grid_cells);
    zeroFloatArray(solver->v, grid_cells);
    zeroFloatArray(solver->du, grid_cells);
    zeroFloatArray(solver->dv, grid_cells);
    zeroFloatArray(solver->previous_u, grid_cells);
    zeroFloatArray(solver->previous_v, grid_cells);
    zeroFloatArray(solver->pressure, grid_cells);
    zeroFloatArray(solver->solid_mask, grid_cells);
    zeroIntArray(solver->cell_type, grid_cells);
    zeroFloatArray(solver->particle_position, max_particles * 2U);
    zeroFloatArray(solver->particle_velocity, max_particles * 2U);
    zeroFloatArray(solver->particle_density, grid_cells);
    zeroIntArray(solver->particle_cell_count, particle_grid_cells);
    zeroIntArray(solver->first_particle_in_cell, particle_grid_cells + 1U);
    zeroIntArray(solver->particle_ids, max_particles);

    solver->gravity_x = 0.0f;
    solver->gravity_y = 0.0f;
    solver->particle_count = d.initial_particle_count;
    solver->particle_rest_density = 0.0f;

    const float radius = d.particle_radius;
    const float particle_dx = 2.0f * radius;
    const float particle_dy = 0.5f * std::sqrt(3.0f) * particle_dx;
    int position_index = 0;
    for (int x = 0; x < d.initial_particles_x; ++x) {
        for (int y = 0; y < d.initial_particles_y; ++y) {
            solver->particle_position[position_index] =
                d.nominal_spacing + radius + particle_dx * x +
                ((y % 2 == 0) ? 0.0f : radius);
            solver->particle_position[position_index + 1] =
                d.nominal_spacing + radius + particle_dy * y;
            position_index += 2;
        }
    }

    for (int x = 0; x < d.grid_x; ++x) {
        for (int y = 0; y < d.grid_y; ++y) {
            const bool boundary = x == 0 || x == d.grid_x - 1 || y == 0 ||
                                  y == d.grid_y - 1;
            solver->solid_mask[x * d.grid_y + y] = boundary ? 0.0f : 1.0f;
        }
    }
}

void integrateParticles(SolverState* solver, float dt) {
    const float gx = solver->gravity_x * solver->config.gravity_scale * dt;
    const float gy = solver->gravity_y * solver->config.gravity_scale * dt;
    for (int i = 0; i < solver->particle_count; ++i) {
        const int index = 2 * i;
        solver->particle_velocity[index] += gx;
        solver->particle_velocity[index + 1] += gy;
        solver->particle_position[index] += solver->particle_velocity[index] * dt;
        solver->particle_position[index + 1] +=
            solver->particle_velocity[index + 1] * dt;
    }
}

void pushParticlesApart(SolverState* solver) {
    const DerivedConfig& d = solver->derived;
    const float minimum_distance = 2.0f * d.particle_radius;
    const float minimum_distance_squared = minimum_distance * minimum_distance;

    zeroIntArray(solver->particle_cell_count,
                 static_cast<std::size_t>(d.particle_grid_cells));
    for (int i = 0; i < solver->particle_count; ++i) {
        const float x = solver->particle_position[2 * i];
        const float y = solver->particle_position[2 * i + 1];
        const int cell_x = clampInt(
            static_cast<int>(x * d.particle_inverse_spacing), 0,
            d.particle_grid_x - 1);
        const int cell_y = clampInt(
            static_cast<int>(y * d.particle_inverse_spacing), 0,
            d.particle_grid_y - 1);
        ++solver->particle_cell_count[cell_x * d.particle_grid_y + cell_y];
    }

    int first = 0;
    for (int i = 0; i < d.particle_grid_cells; ++i) {
        first += solver->particle_cell_count[i];
        solver->first_particle_in_cell[i] = first;
    }
    solver->first_particle_in_cell[d.particle_grid_cells] = first;

    for (int i = 0; i < solver->particle_count; ++i) {
        const float x = solver->particle_position[2 * i];
        const float y = solver->particle_position[2 * i + 1];
        const int cell_x = clampInt(
            static_cast<int>(x * d.particle_inverse_spacing), 0,
            d.particle_grid_x - 1);
        const int cell_y = clampInt(
            static_cast<int>(y * d.particle_inverse_spacing), 0,
            d.particle_grid_y - 1);
        const int cell = cell_x * d.particle_grid_y + cell_y;
        --solver->first_particle_in_cell[cell];
        solver->particle_ids[solver->first_particle_in_cell[cell]] = i;
    }

    for (std::uint16_t iteration = 0;
         iteration < solver->config.particle_push_iterations; ++iteration) {
        for (int i = 0; i < solver->particle_count; ++i) {
            const float px = solver->particle_position[2 * i];
            const float py = solver->particle_position[2 * i + 1];
            const int particle_x =
                static_cast<int>(px * d.particle_inverse_spacing);
            const int particle_y =
                static_cast<int>(py * d.particle_inverse_spacing);
            const int x0 = particle_x > 0 ? particle_x - 1 : 0;
            const int y0 = particle_y > 0 ? particle_y - 1 : 0;
            const int x1 = particle_x + 1 < d.particle_grid_x
                               ? particle_x + 1
                               : d.particle_grid_x - 1;
            const int y1 = particle_y + 1 < d.particle_grid_y
                               ? particle_y + 1
                               : d.particle_grid_y - 1;

            for (int cell_x = x0; cell_x <= x1; ++cell_x) {
                for (int cell_y = y0; cell_y <= y1; ++cell_y) {
                    const int cell = cell_x * d.particle_grid_y + cell_y;
                    const int begin = solver->first_particle_in_cell[cell];
                    const int end = solver->first_particle_in_cell[cell + 1];
                    for (int index = begin; index < end; ++index) {
                        const int other = solver->particle_ids[index];
                        if (other == i) {
                            continue;
                        }
                        float dx = solver->particle_position[2 * other] - px;
                        float dy =
                            solver->particle_position[2 * other + 1] - py;
                        const float distance_squared = dx * dx + dy * dy;
                        if (distance_squared > minimum_distance_squared ||
                            distance_squared == 0.0f) {
                            continue;
                        }
                        const float distance = std::sqrt(distance_squared);
                        const float scale =
                            0.5f * (minimum_distance - distance) / distance;
                        dx *= scale;
                        dy *= scale;
                        solver->particle_position[2 * i] -= dx;
                        solver->particle_position[2 * i + 1] -= dy;
                        solver->particle_position[2 * other] += dx;
                        solver->particle_position[2 * other + 1] += dy;
                    }
                }
            }
        }
    }
}

void handleParticleCollisions(SolverState* solver) {
    const DerivedConfig& d = solver->derived;
    const float minimum_x = d.spacing + d.particle_radius;
    const float maximum_x =
        (d.grid_x - 1) * d.spacing - d.particle_radius;
    const float minimum_y = d.spacing + d.particle_radius;
    const float maximum_y =
        (d.grid_y - 1) * d.spacing - d.particle_radius;

    for (int i = 0; i < solver->particle_count; ++i) {
        const int index = 2 * i;
        float x = solver->particle_position[index];
        float y = solver->particle_position[index + 1];
        if (x < minimum_x) {
            x = minimum_x;
            solver->particle_velocity[index] = 0.0f;
        }
        if (x > maximum_x) {
            x = maximum_x;
            solver->particle_velocity[index] = 0.0f;
        }
        if (y < minimum_y) {
            y = minimum_y;
            solver->particle_velocity[index + 1] = 0.0f;
        }
        if (y > maximum_y) {
            y = maximum_y;
            solver->particle_velocity[index + 1] = 0.0f;
        }
        solver->particle_position[index] = x;
        solver->particle_position[index + 1] = y;
    }
}

void updateParticleDensity(SolverState* solver) {
    const DerivedConfig& d = solver->derived;
    const int stride = d.grid_y;
    const float half_spacing = 0.5f * d.spacing;
    zeroFloatArray(solver->particle_density,
                   static_cast<std::size_t>(d.grid_cells));

    for (int i = 0; i < solver->particle_count; ++i) {
        float x = clampFloat(solver->particle_position[2 * i], d.spacing,
                             (d.grid_x - 1) * d.spacing);
        float y = clampFloat(solver->particle_position[2 * i + 1], d.spacing,
                             (d.grid_y - 1) * d.spacing);
        const int x0 =
            static_cast<int>((x - half_spacing) * d.inverse_spacing);
        const float tx =
            (x - half_spacing - x0 * d.spacing) * d.inverse_spacing;
        const int x1 = x0 + 1 < d.grid_x - 1 ? x0 + 1 : d.grid_x - 2;
        const int y0 =
            static_cast<int>((y - half_spacing) * d.inverse_spacing);
        const float ty =
            (y - half_spacing - y0 * d.spacing) * d.inverse_spacing;
        const int y1 = y0 + 1 < d.grid_y - 1 ? y0 + 1 : d.grid_y - 2;
        const float sx = 1.0f - tx;
        const float sy = 1.0f - ty;

        solver->particle_density[x0 * stride + y0] += sx * sy;
        solver->particle_density[x1 * stride + y0] += tx * sy;
        solver->particle_density[x1 * stride + y1] += tx * ty;
        solver->particle_density[x0 * stride + y1] += sx * ty;
    }
}

float calculateRestDensity(const SolverState* solver) {
    float sum = 0.0f;
    int fluid_cells = 0;
    for (int i = 0; i < solver->derived.grid_cells; ++i) {
        sum += solver->particle_density[i];
        if (solver->cell_type[i] == FluidCell) {
            ++fluid_cells;
        }
    }
    return fluid_cells > 0 ? sum / static_cast<float>(fluid_cells) : 0.0f;
}

void transferVelocities(SolverState* solver, bool to_grid) {
    const DerivedConfig& d = solver->derived;
    const int stride = d.grid_y;
    const float half_spacing = 0.5f * d.spacing;
    const std::size_t bytes =
        static_cast<std::size_t>(d.grid_cells) * sizeof(float);

    if (to_grid) {
        std::memcpy(solver->previous_u, solver->u, bytes);
        std::memcpy(solver->previous_v, solver->v, bytes);
        std::memset(solver->du, 0, bytes);
        std::memset(solver->dv, 0, bytes);
        std::memset(solver->u, 0, bytes);
        std::memset(solver->v, 0, bytes);
        for (int i = 0; i < d.grid_cells; ++i) {
            solver->cell_type[i] =
                solver->solid_mask[i] == 0.0f ? SolidCell : AirCell;
        }
        for (int i = 0; i < solver->particle_count; ++i) {
            const int x = clampInt(
                static_cast<int>(solver->particle_position[2 * i] *
                                 d.inverse_spacing),
                0, d.grid_x - 1);
            const int y = clampInt(
                static_cast<int>(solver->particle_position[2 * i + 1] *
                                 d.inverse_spacing),
                0, d.grid_y - 1);
            const int cell = x * stride + y;
            if (solver->cell_type[cell] == AirCell) {
                solver->cell_type[cell] = FluidCell;
            }
        }
    }

    for (int component = 0; component < 2; ++component) {
        const float offset_x = component == 0 ? 0.0f : half_spacing;
        const float offset_y = component == 0 ? half_spacing : 0.0f;
        float* velocity_grid = component == 0 ? solver->u : solver->v;
        float* previous_grid =
            component == 0 ? solver->previous_u : solver->previous_v;
        float* weight_grid = component == 0 ? solver->du : solver->dv;

        for (int i = 0; i < solver->particle_count; ++i) {
            float x = clampFloat(solver->particle_position[2 * i], d.spacing,
                                 (d.grid_x - 1) * d.spacing);
            float y = clampFloat(solver->particle_position[2 * i + 1], d.spacing,
                                 (d.grid_y - 1) * d.spacing);
            const int x0_candidate = static_cast<int>(
                (x - offset_x) * d.inverse_spacing);
            const int x0 = x0_candidate < d.grid_x - 2
                               ? x0_candidate
                               : d.grid_x - 2;
            const float tx =
                (x - offset_x - x0 * d.spacing) * d.inverse_spacing;
            const int x1 = x0 + 1 < d.grid_x - 1 ? x0 + 1 : d.grid_x - 2;
            const int y0_candidate = static_cast<int>(
                (y - offset_y) * d.inverse_spacing);
            const int y0 = y0_candidate < d.grid_y - 2
                               ? y0_candidate
                               : d.grid_y - 2;
            const float ty =
                (y - offset_y - y0 * d.spacing) * d.inverse_spacing;
            const int y1 = y0 + 1 < d.grid_y - 1 ? y0 + 1 : d.grid_y - 2;
            const float sx = 1.0f - tx;
            const float sy = 1.0f - ty;
            const float weights[4] = {sx * sy, tx * sy, tx * ty, sx * ty};
            const int cells[4] = {x0 * stride + y0, x1 * stride + y0,
                                  x1 * stride + y1, x0 * stride + y1};

            if (to_grid) {
                const float particle_velocity =
                    solver->particle_velocity[2 * i + component];
                for (int corner = 0; corner < 4; ++corner) {
                    velocity_grid[cells[corner]] +=
                        particle_velocity * weights[corner];
                    weight_grid[cells[corner]] += weights[corner];
                }
            } else {
                const int cell_offset = component == 0 ? stride : 1;
                float valid[4];
                float weight_sum = 0.0f;
                for (int corner = 0; corner < 4; ++corner) {
                    valid[corner] =
                        solver->cell_type[cells[corner]] != AirCell ||
                                solver->cell_type[cells[corner] - cell_offset] !=
                                    AirCell
                            ? 1.0f
                            : 0.0f;
                    weight_sum += valid[corner] * weights[corner];
                }
                if (weight_sum > 0.0f) {
                    float pic_velocity = 0.0f;
                    float correction = 0.0f;
                    for (int corner = 0; corner < 4; ++corner) {
                        const float weight = valid[corner] * weights[corner];
                        pic_velocity += weight * velocity_grid[cells[corner]];
                        correction +=
                            weight * (velocity_grid[cells[corner]] -
                                      previous_grid[cells[corner]]);
                    }
                    pic_velocity /= weight_sum;
                    correction /= weight_sum;
                    const float flip_velocity =
                        solver->particle_velocity[2 * i + component] + correction;
                    solver->particle_velocity[2 * i + component] =
                        (1.0f - solver->config.flip_ratio) * pic_velocity +
                        solver->config.flip_ratio * flip_velocity;
                }
            }
        }

        if (to_grid) {
            for (int i = 0; i < d.grid_cells; ++i) {
                if (weight_grid[i] > 0.0f) {
                    velocity_grid[i] /= weight_grid[i];
                }
            }
            for (int x = 0; x < d.grid_x; ++x) {
                for (int y = 0; y < d.grid_y; ++y) {
                    const int index = x * stride + y;
                    const bool solid = solver->cell_type[index] == SolidCell;
                    if (solid ||
                        (x > 0 && solver->cell_type[(x - 1) * stride + y] ==
                                      SolidCell)) {
                        solver->u[index] = solver->previous_u[index];
                    }
                    if (solid ||
                        (y > 0 && solver->cell_type[x * stride + y - 1] ==
                                      SolidCell)) {
                        solver->v[index] = solver->previous_v[index];
                    }
                }
            }
        }
    }
}

void solveIncompressibility(SolverState* solver, float dt) {
    const DerivedConfig& d = solver->derived;
    const int stride = d.grid_y;
    const std::size_t bytes =
        static_cast<std::size_t>(d.grid_cells) * sizeof(float);
    std::memset(solver->pressure, 0, bytes);
    std::memcpy(solver->previous_u, solver->u, bytes);
    std::memcpy(solver->previous_v, solver->v, bytes);
    const float pressure_scale =
        solver->config.fluid_density * d.spacing / dt;

    for (std::uint16_t iteration = 0;
         iteration < solver->config.pressure_iterations; ++iteration) {
        for (int x = 1; x < d.grid_x - 1; ++x) {
            for (int y = 1; y < d.grid_y - 1; ++y) {
                const int center = x * stride + y;
                if (solver->cell_type[center] != FluidCell) {
                    continue;
                }
                const int left = (x - 1) * stride + y;
                const int right = (x + 1) * stride + y;
                const int bottom = x * stride + y - 1;
                const int top = x * stride + y + 1;
                const float solid_left = solver->solid_mask[left];
                const float solid_right = solver->solid_mask[right];
                const float solid_bottom = solver->solid_mask[bottom];
                const float solid_top = solver->solid_mask[top];
                const float solid_sum = solid_left + solid_right + solid_bottom +
                                        solid_top;
                if (solid_sum == 0.0f) {
                    continue;
                }

                float divergence = (solver->u[right] - solver->u[center]) +
                                   (solver->v[top] - solver->v[center]);
                if (solver->config.compensate_drift &&
                    solver->particle_rest_density > 0.0f) {
                    const float compression =
                        solver->particle_density[center] -
                        solver->particle_rest_density;
                    if (compression > 0.0f) {
                        divergence -= compression;
                    }
                }

                float pressure = -divergence / solid_sum;
                pressure *= solver->config.pressure_over_relaxation;
                solver->pressure[center] += pressure_scale * pressure;
                solver->u[center] -= solid_left * pressure;
                solver->u[right] += solid_right * pressure;
                solver->v[center] -= solid_bottom * pressure;
                solver->v[top] += solid_top * pressure;
            }
        }
    }
}

}  // namespace

Config::Config()
    : simulation_width(1.0f),
      simulation_height(1.0f),
      grid_width(16U),
      grid_height(16U),
      max_particles(436U),
      initial_fill_width(0.8f),
      initial_fill_height(0.6f),
      fluid_density(1000.0f),
      particle_radius_factor(0.35f),
      neighbor_spacing_factor(2.2f),
      gravity_scale(9.81f),
      flip_ratio(0.9f),
      pressure_over_relaxation(1.9f),
      particle_push_iterations(1U),
      pressure_iterations(12U),
      compensate_drift(true) {}

FluidSolver::FluidSolver() : state_(NULL) {}

std::size_t FluidSolver::requiredMemory(const Config& config) {
    DerivedConfig derived;
    if (!computeDerived(config, &derived)) {
        return 0U;
    }
    std::size_t layout_size = 0U;
    if (!layoutSize(config, derived, &layout_size)) {
        return 0U;
    }
    std::size_t total = 0U;
    if (!checkedAdd(layout_size, kWorkspaceAlignment - 1U, &total)) {
        return 0U;
    }
    return total;
}

Status FluidSolver::initialize(const Config& config, void* workspace,
                               std::size_t workspace_size) {
    state_ = NULL;
    DerivedConfig derived;
    if (!computeDerived(config, &derived)) {
        return Status::InvalidConfig;
    }
    const std::size_t required = requiredMemory(config);
    if (workspace == NULL || workspace_size < required) {
        return Status::InsufficientMemory;
    }

    const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(workspace);
    const std::size_t prefix =
        static_cast<std::size_t>((kWorkspaceAlignment -
                                  (address & (kWorkspaceAlignment - 1U))) &
                                 (kWorkspaceAlignment - 1U));
    unsigned char* aligned = static_cast<unsigned char*>(workspace) + prefix;
    WorkspaceCursor cursor(aligned, workspace_size - prefix);
    SolverState* solver = cursor.take<SolverState>(1U);
    if (solver == NULL) {
        return Status::InsufficientMemory;
    }
    new (solver) SolverState();
    solver->config = config;
    solver->derived = derived;

    const std::size_t grid_cells = static_cast<std::size_t>(derived.grid_cells);
    const std::size_t max_particles = config.max_particles;
    const std::size_t particle_grid_cells =
        static_cast<std::size_t>(derived.particle_grid_cells);
    solver->u = cursor.take<float>(grid_cells);
    solver->v = cursor.take<float>(grid_cells);
    solver->du = cursor.take<float>(grid_cells);
    solver->dv = cursor.take<float>(grid_cells);
    solver->previous_u = cursor.take<float>(grid_cells);
    solver->previous_v = cursor.take<float>(grid_cells);
    solver->pressure = cursor.take<float>(grid_cells);
    solver->solid_mask = cursor.take<float>(grid_cells);
    solver->particle_density = cursor.take<float>(grid_cells);
    solver->cell_type = cursor.take<std::int32_t>(grid_cells);
    solver->particle_position = cursor.take<float>(max_particles * 2U);
    solver->particle_velocity = cursor.take<float>(max_particles * 2U);
    solver->particle_cell_count =
        cursor.take<std::int32_t>(particle_grid_cells);
    solver->first_particle_in_cell =
        cursor.take<std::int32_t>(particle_grid_cells + 1U);
    solver->particle_ids = cursor.take<std::int32_t>(max_particles);

    if (solver->u == NULL || solver->v == NULL || solver->du == NULL ||
        solver->dv == NULL || solver->previous_u == NULL ||
        solver->previous_v == NULL || solver->pressure == NULL ||
        solver->solid_mask == NULL || solver->particle_density == NULL ||
        solver->cell_type == NULL || solver->particle_position == NULL ||
        solver->particle_velocity == NULL ||
        solver->particle_cell_count == NULL ||
        solver->first_particle_in_cell == NULL || solver->particle_ids == NULL) {
        return Status::InsufficientMemory;
    }

    state_ = solver;
    initializeState(solver);
    return Status::Ok;
}

Status FluidSolver::reset() {
    if (state_ == NULL) {
        return Status::NotInitialized;
    }
    initializeState(state(state_));
    return Status::Ok;
}

void FluidSolver::setGravity(float gx, float gy) {
    if (state_ == NULL) {
        return;
    }
    SolverState* solver = state(state_);
    solver->gravity_x = std::isfinite(gx) ? gx : 0.0f;
    solver->gravity_y = std::isfinite(gy) ? gy : 0.0f;
}

Status FluidSolver::setSolverQuality(
    std::uint16_t particle_push_iterations,
    std::uint16_t pressure_iterations, float flip_ratio) {
    if (state_ == NULL) {
        return Status::NotInitialized;
    }
    if (particle_push_iterations == 0U || pressure_iterations == 0U ||
        !std::isfinite(flip_ratio) || flip_ratio < 0.0f || flip_ratio > 1.0f) {
        return Status::InvalidConfig;
    }
    SolverState* solver = state(state_);
    solver->config.particle_push_iterations = particle_push_iterations;
    solver->config.pressure_iterations = pressure_iterations;
    solver->config.flip_ratio = flip_ratio;
    return Status::Ok;
}

Status FluidSolver::step(float dt) {
    if (state_ == NULL) {
        return Status::NotInitialized;
    }
    if (!finitePositive(dt)) {
        return Status::InvalidTimeStep;
    }
    SolverState* solver = state(state_);
    integrateParticles(solver, dt);
    pushParticlesApart(solver);
    handleParticleCollisions(solver);
    transferVelocities(solver, true);
    updateParticleDensity(solver);
    if (solver->particle_rest_density == 0.0f) {
        solver->particle_rest_density = calculateRestDensity(solver);
    }
    solveIncompressibility(solver, dt);
    transferVelocities(solver, false);
    return Status::Ok;
}

bool FluidSolver::initialized() const {
    return state_ != NULL;
}

std::size_t FluidSolver::particleCapacity() const {
    return state_ == NULL ? 0U : state(state_)->config.max_particles;
}

ParticleView FluidSolver::particles() const {
    ParticleView view = {NULL, NULL, 0U};
    if (state_ != NULL) {
        const SolverState* solver = state(state_);
        view.positions_xy = solver->particle_position;
        view.velocities_xy = solver->particle_velocity;
        view.count = static_cast<std::size_t>(solver->particle_count);
    }
    return view;
}

GridView FluidSolver::densityGrid() const {
    GridView view = {NULL, 0U, 0U, 0U, 0.0f};
    if (state_ != NULL) {
        const SolverState* solver = state(state_);
        view.values = solver->particle_density + solver->derived.grid_y + 1;
        view.width = solver->config.grid_width;
        view.height = solver->config.grid_height;
        view.stride = static_cast<std::size_t>(solver->derived.grid_y);
        view.rest_density = solver->particle_rest_density;
    }
    return view;
}

}  // namespace fluid2d
