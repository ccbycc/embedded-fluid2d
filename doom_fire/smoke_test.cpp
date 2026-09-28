#include <doom_fire/doom_fire.h>

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
    doom_fire::Config config;
    const std::size_t bytes = doom_fire::DoomFire::requiredMemory(config);
    CHECK(bytes > 0U);

    doom_fire::Config invalid = config;
    invalid.height = 11U;
    CHECK(doom_fire::DoomFire::requiredMemory(invalid) == 0U);

    std::vector<unsigned char> workspace_a(bytes);
    std::vector<unsigned char> workspace_b(bytes);
    doom_fire::DoomFire fire_a;
    doom_fire::DoomFire fire_b;
    CHECK(fire_a.initialize(config, workspace_a.data(), bytes - 1U,
                            0x12345678U) ==
          doom_fire::Status::InsufficientMemory);
    CHECK(fire_a.initialize(config, workspace_a.data(), bytes, 0x12345678U) ==
          doom_fire::Status::Ok);
    CHECK(fire_b.initialize(config, workspace_b.data(), bytes, 0x12345678U) ==
          doom_fire::Status::Ok);
    CHECK(fire_a.step(std::nanf(""), 0U) == doom_fire::Status::InvalidInput);
    CHECK(fire_a.step(std::numeric_limits<float>::max(), 0U) ==
          doom_fire::Status::InvalidInput);

    for (std::uint32_t frame = 0; frame < 120U; ++frame) {
        const float gravity_x =
            static_cast<float>(static_cast<int>(frame % 21U) - 10) * 0.25f;
        const std::uint32_t time_ms = 1000U + frame * 33U;
        CHECK(fire_a.step(gravity_x, time_ms) == doom_fire::Status::Ok);
        CHECK(fire_b.step(gravity_x, time_ms) == doom_fire::Status::Ok);
    }

    const doom_fire::HeatView heat_a = fire_a.heatGrid();
    const doom_fire::HeatView heat_b = fire_b.heatGrid();
    CHECK(heat_a.width == 16U);
    CHECK(heat_a.height == 16U);
    CHECK(heat_a.stride == 16U);
    CHECK(std::memcmp(heat_a.values, heat_b.values,
                      heat_a.width * heat_a.height * sizeof(float)) == 0);

    double heat_sum = 0.0;
    double heat_weighted_sum = 0.0;
    for (std::size_t y = 0; y < heat_a.height; ++y) {
        for (std::size_t x = 0; x < heat_a.width; ++x) {
            const float value = heat_a.values[y * heat_a.stride + x];
            CHECK(std::isfinite(value));
            CHECK(value >= 0.0f);
            CHECK(value <= heat_a.max_heat);
            heat_sum += value;
            heat_weighted_sum += value * (y * heat_a.stride + x + 1U);
        }
    }
    // Golden values captured from the frozen ESP32 C solver with the same
    // seed, gravity sequence and 120 time samples.
    CHECK(near(heat_sum, 1350.7769681215286, 1.0e-4));
    CHECK(near(heat_weighted_sum, 216393.68287277222, 1.0e-3));

    CHECK(fire_a.reset() == doom_fire::Status::Ok);
    const doom_fire::HeatView reset_heat = fire_a.heatGrid();
    for (std::size_t i = 0; i < reset_heat.width * reset_heat.height; ++i) {
        CHECK(reset_heat.values[i] == 0.0f);
    }
    return 0;
}
