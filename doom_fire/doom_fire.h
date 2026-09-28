#ifndef DOOM_FIRE_DOOM_FIRE_H_
#define DOOM_FIRE_DOOM_FIRE_H_

#include <cstddef>
#include <cstdint>

namespace doom_fire {

enum class Status {
    Ok,
    InvalidConfig,
    InsufficientMemory,
    NotInitialized,
    InvalidInput
};

struct Config {
    Config();

    std::uint16_t width;
    std::uint16_t height;
    float decay;
    float intensity;
    float max_heat;
};

struct HeatView {
    // Row-major grid, indexed as values[y * stride + x].
    const float* values;
    std::uint16_t width;
    std::uint16_t height;
    std::size_t stride;
    float max_heat;
};

class DoomFire {
public:
    DoomFire();

    DoomFire(const DoomFire&) = delete;
    DoomFire& operator=(const DoomFire&) = delete;

    static std::size_t requiredMemory(const Config& config);

    // The solver does not own workspace; it must outlive the solver and views.
    Status initialize(const Config& config, void* workspace,
                      std::size_t workspace_size, std::uint32_t seed);
    Status reset();
    Status step(float gravity_x, std::uint32_t time_ms);

    bool initialized() const;
    HeatView heatGrid() const;

private:
    void* state_;
};

}  // namespace doom_fire

#endif  // DOOM_FIRE_DOOM_FIRE_H_
