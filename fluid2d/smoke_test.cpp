#include <fluid2d/fluid_solver.h>

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::fprintf(stderr, "check failed at line %d: %s\n", __LINE__,   \
                         #condition);                                           \
            return 1;                                                           \
        }                                                                       \
    } while (false)

bool near(double actual, double expected, double tolerance) {
    return std::fabs(actual - expected) <= tolerance;
}

int main() {
    fluid2d::Config config;
    const std::size_t bytes = fluid2d::FluidSolver::requiredMemory(config);
    CHECK(bytes > 0U);

    fluid2d::FluidSolver uninitialized;
    CHECK(uninitialized.step(1.0f / 30.0f) ==
          fluid2d::Status::NotInitialized);

    fluid2d::Config invalid_config = config;
    invalid_config.grid_width = 0U;
    CHECK(fluid2d::FluidSolver::requiredMemory(invalid_config) == 0U);
    invalid_config = config;
    invalid_config.initial_fill_height = 1.1f;
    CHECK(fluid2d::FluidSolver::requiredMemory(invalid_config) == 0U);
    invalid_config = config;
    invalid_config.simulation_width = 2.0f;
    CHECK(fluid2d::FluidSolver::requiredMemory(invalid_config) == 0U);
    invalid_config = config;
    invalid_config.max_particles =
        static_cast<std::uint32_t>(std::numeric_limits<int>::max());
    invalid_config.particle_radius_factor = 1.0e-10f;
    invalid_config.neighbor_spacing_factor = 1.0e10f;
    CHECK(fluid2d::FluidSolver::requiredMemory(invalid_config) == 0U);

    fluid2d::Config rectangular_config = config;
    rectangular_config.simulation_width = 2.0f;
    rectangular_config.grid_width = 32U;
    rectangular_config.grid_height = 15U;
    CHECK(fluid2d::FluidSolver::requiredMemory(rectangular_config) > 0U);

    std::vector<unsigned char> workspace_a(bytes);
    std::vector<unsigned char> workspace_b(bytes);
    fluid2d::FluidSolver solver_a;
    fluid2d::FluidSolver solver_b;

    CHECK(solver_a.initialize(config, workspace_a.data(), bytes - 1U) ==
          fluid2d::Status::InsufficientMemory);
    CHECK(solver_a.initialize(config, workspace_a.data(), bytes) ==
          fluid2d::Status::Ok);
    CHECK(solver_b.initialize(config, workspace_b.data(), bytes) ==
          fluid2d::Status::Ok);

    solver_a.setGravity(0.35f, 0.75f);
    solver_b.setGravity(0.35f, 0.75f);
    CHECK(solver_a.step(0.0f) == fluid2d::Status::InvalidTimeStep);
    for (int frame = 0; frame < 30; ++frame) {
        CHECK(solver_a.step(1.0f / 30.0f) == fluid2d::Status::Ok);
        CHECK(solver_b.step(1.0f / 30.0f) == fluid2d::Status::Ok);
    }

    const fluid2d::ParticleView particles_a = solver_a.particles();
    const fluid2d::ParticleView particles_b = solver_b.particles();
    CHECK(particles_a.count == 180U);
    CHECK(particles_a.count == particles_b.count);
    CHECK(std::memcmp(particles_a.positions_xy, particles_b.positions_xy,
                      particles_a.count * 2U * sizeof(float)) == 0);
    CHECK(std::memcmp(particles_a.velocities_xy, particles_b.velocities_xy,
                      particles_a.count * 2U * sizeof(float)) == 0);

    const fluid2d::GridView density_a = solver_a.densityGrid();
    const fluid2d::GridView density_b = solver_b.densityGrid();
    CHECK(density_a.width == 16U);
    CHECK(density_a.height == 16U);
    CHECK(density_a.stride == 18U);
    CHECK(density_a.rest_density > 0.0f);

    double position_x_sum = 0.0;
    double position_y_sum = 0.0;
    double velocity_x_sum = 0.0;
    double velocity_y_sum = 0.0;
    for (std::size_t i = 0; i < particles_a.count; ++i) {
        position_x_sum += particles_a.positions_xy[2U * i];
        position_y_sum += particles_a.positions_xy[2U * i + 1U];
        velocity_x_sum += particles_a.velocities_xy[2U * i];
        velocity_y_sum += particles_a.velocities_xy[2U * i + 1U];
    }

    double density_sum = 0.0;
    double density_weighted_sum = 0.0;
    for (std::size_t x = 0; x < density_a.width; ++x) {
        for (std::size_t y = 0; y < density_a.height; ++y) {
            const float a = density_a.values[x * density_a.stride + y];
            const float b = density_b.values[x * density_b.stride + y];
            CHECK(std::isfinite(a));
            CHECK(a == b);
            density_sum += a;
            density_weighted_sum += a * (1U + x * 17U + y);
        }
    }
    // Golden values captured from the frozen ESP32 C solver with the same
    // default config, gravity and 30 fixed time steps.
    CHECK(near(position_x_sum, 113.61013618856668, 1.0e-4));
    CHECK(near(position_y_sum, 127.97422081232071, 1.0e-4));
    CHECK(near(velocity_x_sum, -15.363585213664919, 1.0e-4));
    CHECK(near(velocity_y_sum, -5.5403132252395153, 1.0e-4));
    CHECK(near(density_sum, 179.91129964077845, 1.0e-4));
    CHECK(near(density_weighted_sum, 32356.768919050694, 1.0e-3));
    CHECK(near(density_a.rest_density, 1.91489375, 1.0e-6));

    const std::size_t rectangular_bytes =
        fluid2d::FluidSolver::requiredMemory(rectangular_config);
    std::vector<unsigned char> rectangular_workspace(rectangular_bytes);
    fluid2d::FluidSolver rectangular_solver;
    CHECK(rectangular_solver.initialize(rectangular_config,
                                        rectangular_workspace.data(),
                                        rectangular_workspace.size()) ==
          fluid2d::Status::Ok);
    rectangular_solver.setGravity(0.0f, 1.0f);
    CHECK(rectangular_solver.step(1.0f / 30.0f) == fluid2d::Status::Ok);
    CHECK(rectangular_solver.densityGrid().width == 32U);
    CHECK(rectangular_solver.densityGrid().height == 15U);

    CHECK(solver_a.reset() == fluid2d::Status::Ok);
    CHECK(solver_a.particles().count == 180U);
    return 0;
}
