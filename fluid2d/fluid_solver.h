/*
Portions of this module are adapted/ported from work by:
  Copyright 2022 Matthias Mueller - Ten Minute Physics
  www.youtube.com/c/TenMinutePhysics
  www.matthiasMueller.info/tenMinutePhysics

Licensed under the MIT License. See examples/esp32/LICENSE for the full text.
*/
#ifndef FLUID2D_FLUID_SOLVER_H_
#define FLUID2D_FLUID_SOLVER_H_

#include <cstddef>
#include <cstdint>

namespace fluid2d {

enum class Status {
    Ok,
    InvalidConfig,
    InsufficientMemory,
    NotInitialized,
    InvalidTimeStep
};

struct Config {
    Config();

    // The padded grid cells must remain approximately square.
    float simulation_width;
    float simulation_height;
    std::uint16_t grid_width;
    std::uint16_t grid_height;
    std::uint32_t max_particles;

    // Fractions of the simulation dimensions, in the range (0, 1].
    float initial_fill_width;
    float initial_fill_height;
    float fluid_density;
    float particle_radius_factor;
    float neighbor_spacing_factor;
    float gravity_scale;
    float flip_ratio;
    float pressure_over_relaxation;
    std::uint16_t particle_push_iterations;
    std::uint16_t pressure_iterations;
    bool compensate_drift;
};

struct ParticleView {
    // Interleaved x, y components. The view does not own this memory.
    const float* positions_xy;
    const float* velocities_xy;
    std::size_t count;
};

struct GridView {
    // Interior cells only, indexed as values[x * stride + y].
    const float* values;
    std::uint16_t width;
    std::uint16_t height;
    std::size_t stride;
    float rest_density;
};

class FluidSolver {
public:
    FluidSolver();

    FluidSolver(const FluidSolver&) = delete;
    FluidSolver& operator=(const FluidSolver&) = delete;

    static std::size_t requiredMemory(const Config& config);

    // The solver does not own workspace; it must outlive the solver and views.
    Status initialize(const Config& config, void* workspace,
                      std::size_t workspace_size);
    Status reset();

    void setGravity(float gx, float gy);
    Status setSolverQuality(std::uint16_t particle_push_iterations,
                            std::uint16_t pressure_iterations,
                            float flip_ratio);
    Status step(float dt);

    bool initialized() const;
    std::size_t particleCapacity() const;
    ParticleView particles() const;
    GridView densityGrid() const;

private:
    void* state_;
};

}  // namespace fluid2d

#endif  // FLUID2D_FLUID_SOLVER_H_
