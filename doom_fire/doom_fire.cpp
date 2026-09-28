#include <doom_fire/doom_fire.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>

namespace doom_fire {
namespace {

const float kPi = 3.14159265358979323846f;
const int kSourceRows = 12;

struct FireState {
    Config config;
    std::uint32_t rng;
    int width;
    int height;
    int cell_count;
    float* heat;
    float* next;
};

const std::size_t kWorkspaceAlignment = alignof(FireState);

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

bool validConfig(const Config& config, int* cell_count) {
    if (config.width == 0U || config.height < kSourceRows ||
        !finitePositive(config.decay) || !finitePositive(config.intensity) ||
        !finitePositive(config.max_heat)) {
        return false;
    }
    const std::uint64_t cells =
        static_cast<std::uint64_t>(config.width) * config.height;
    if (cells > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
        return false;
    }
    *cell_count = static_cast<int>(cells);
    return true;
}

bool layoutSize(int cell_count, std::size_t* size) {
    std::size_t offset = 0U;
    if (!addBlock(1U, sizeof(FireState), alignof(FireState), &offset) ||
        !addBlock(static_cast<std::size_t>(cell_count), sizeof(float),
                  alignof(float), &offset) ||
        !addBlock(static_cast<std::size_t>(cell_count), sizeof(float),
                  alignof(float), &offset)) {
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

FireState* state(void* value) {
    return static_cast<FireState*>(value);
}

float maximum(float left, float right) {
    return left > right ? left : right;
}

float minimum(float left, float right) {
    return left < right ? left : right;
}

int clampInt(int value, int low, int high) {
    if (value < low) {
        return low;
    }
    if (value > high) {
        return high;
    }
    return value;
}

std::uint32_t nextRandom(std::uint32_t* random_state) {
    std::uint32_t value = *random_state == 0U ? 1U : *random_state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *random_state = value;
    return value;
}

float randomUnit(std::uint32_t* random_state) {
    return (nextRandom(random_state) >> 8) *
           (1.0f / 16777216.0f);
}

int randomInt(std::uint32_t* random_state, int limit) {
    return static_cast<int>(nextRandom(random_state) %
                            static_cast<std::uint32_t>(limit));
}

void clearState(FireState* fire) {
    const std::size_t bytes =
        static_cast<std::size_t>(fire->cell_count) * sizeof(float);
    std::memset(fire->heat, 0, bytes);
    std::memset(fire->next, 0, bytes);
}

void propagate(FireState* fire, float gravity_x) {
    const int width = fire->width;
    const int height = fire->height;
    const float decay = fire->config.decay;
    const float gx = gravity_x * 0.5f;
    const float center_x = (width - 1) * 0.5f;
    const float gravity_strength = std::fabs(gx);
    const float horizontal_drift = -(gx * 3.0f);

    const float weight_up = 3.8f;
    const float weight_below = 0.5f;
    const float weight_left = maximum(0.0f, 1.0f + horizontal_drift);
    const float weight_right = maximum(0.0f, 1.0f - horizontal_drift);
    const float total = maximum(
        1.0f, weight_up + weight_below + weight_left + weight_right);
    const float inverse_total = 1.0f / total;
    const float taper_relax =
        maximum(0.2f, 1.0f - gravity_strength * 0.3f);
    const int drift_shift =
        horizontal_drift == 0.0f
            ? 0
            : (horizontal_drift > 0.0f ? 1 : -1) *
                  static_cast<int>(
                      std::ceil(std::fabs(horizontal_drift) * 0.8f));

    std::memset(&fire->next[(height - 1) * width], 0,
                sizeof(float) * static_cast<std::size_t>(width));

    for (int y = 0; y < height - 1; ++y) {
        const float rows_from_bottom = static_cast<float>((height - 1) - y);
        const int source_row = (y + 1) * width;
        const int below_row =
            y + 2 < height ? (y + 2) * width : source_row;
        const float lean_shift =
            horizontal_drift * rows_from_bottom * 0.35f;
        const float effective_center = center_x + lean_shift;
        const float taper =
            y < height - 6
                ? static_cast<float>((height - 6) - y) * 0.045f * taper_relax
                : 0.0f;

        for (int x = 0; x < width; ++x) {
            const int left_x = x > 0 ? x - 1 : x;
            const int right_x = x + 1 < width ? x + 1 : x;
            const float value_up = fire->heat[source_row + x];
            const float value_left = fire->heat[source_row + left_x];
            const float value_right = fire->heat[source_row + right_x];
            const float value_below = fire->heat[below_row + x];
            const float average =
                (value_up * weight_up + value_left * weight_left +
                 value_right * weight_right + value_below * weight_below) *
                inverse_total;

            float cooling =
                decay * (randomUnit(&fire->rng) * 0.4f + 0.8f);
            if (taper > 0.0f) {
                const float distance =
                    std::fabs(static_cast<float>(x) - effective_center);
                cooling += distance * distance * taper;
            }

            const float new_heat = maximum(0.0f, average - cooling);
            fire->next[y * width + x] = new_heat;

            if (new_heat > 12.0f && randomUnit(&fire->rng) < 0.006f) {
                const int jump = 2 + randomInt(&fire->rng, 4);
                const int target_y = y - jump;
                int target_x =
                    x + (randomInt(&fire->rng, 3) - 1) + drift_shift;
                target_x = clampInt(target_x, 0, width - 1);

                if (target_y >= 0) {
                    const int target = target_y * width + target_x;
                    const float spark = minimum(
                        new_heat * 2.2f + 10.0f, fire->config.max_heat);
                    if (spark > fire->next[target]) {
                        fire->next[target] = spark;
                    }
                }
            }
        }
    }
}

void ignite(FireState* fire, std::uint32_t time_ms) {
    const int width = fire->width;
    const int height = fire->height;
    const float time = static_cast<float>(time_ms);
    const float sway =
        std::sin(time / 800.0f) * 1.2f +
        std::sin(time / 350.0f) * 0.5f;
    const float center_x = (width - 1) * 0.5f + sway;
    const float center_y = static_cast<float>(height) - 5.5f;
    const float radius = 4.8f;
    const float breathing = std::sin(time / 200.0f) * 4.0f;
    const float flicker = randomUnit(&fire->rng) * 2.0f;
    const float base_intensity =
        maximum(0.0f, fire->config.intensity + breathing - flicker);

    for (int y = height - kSourceRows; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const float dx = static_cast<float>(x) - center_x;
            const float dy = static_cast<float>(y) - center_y;
            const float distance = std::sqrt(dx * dx + dy * dy);
            if (distance <= radius) {
                const float falloff =
                    std::cos((distance / radius) * (kPi * 0.5f));
                const float heat = minimum(base_intensity * falloff,
                                           fire->config.max_heat);
                const int index = y * width + x;
                if (heat > fire->next[index]) {
                    fire->next[index] = heat;
                }
            }
        }
    }
}

}  // namespace

Config::Config()
    : width(16U),
      height(16U),
      decay(3.0f),
      intensity(36.0f),
      max_heat(80.0f) {}

DoomFire::DoomFire() : state_(NULL) {}

std::size_t DoomFire::requiredMemory(const Config& config) {
    int cell_count = 0;
    if (!validConfig(config, &cell_count)) {
        return 0U;
    }
    std::size_t layout_size = 0U;
    if (!layoutSize(cell_count, &layout_size)) {
        return 0U;
    }
    std::size_t total = 0U;
    if (!checkedAdd(layout_size, kWorkspaceAlignment - 1U, &total)) {
        return 0U;
    }
    return total;
}

Status DoomFire::initialize(const Config& config, void* workspace,
                            std::size_t workspace_size, std::uint32_t seed) {
    state_ = NULL;
    int cell_count = 0;
    if (!validConfig(config, &cell_count)) {
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
    FireState* fire = cursor.take<FireState>(1U);
    if (fire == NULL) {
        return Status::InsufficientMemory;
    }
    new (fire) FireState();
    fire->config = config;
    fire->rng = seed == 0U ? 1U : seed;
    fire->width = config.width;
    fire->height = config.height;
    fire->cell_count = cell_count;
    fire->heat = cursor.take<float>(static_cast<std::size_t>(cell_count));
    fire->next = cursor.take<float>(static_cast<std::size_t>(cell_count));
    if (fire->heat == NULL || fire->next == NULL) {
        return Status::InsufficientMemory;
    }

    state_ = fire;
    clearState(fire);
    return Status::Ok;
}

Status DoomFire::reset() {
    if (state_ == NULL) {
        return Status::NotInitialized;
    }
    clearState(state(state_));
    return Status::Ok;
}

Status DoomFire::step(float gravity_x, std::uint32_t time_ms) {
    if (state_ == NULL) {
        return Status::NotInitialized;
    }
    const double drift_shift =
        std::ceil(std::fabs(static_cast<double>(gravity_x)) * 1.5 * 0.8);
    if (!std::isfinite(gravity_x) ||
        drift_shift > std::numeric_limits<int>::max()) {
        return Status::InvalidInput;
    }
    FireState* fire = state(state_);
    propagate(fire, gravity_x);
    ignite(fire, time_ms);
    std::memcpy(fire->heat, fire->next,
                static_cast<std::size_t>(fire->cell_count) * sizeof(float));
    return Status::Ok;
}

bool DoomFire::initialized() const {
    return state_ != NULL;
}

HeatView DoomFire::heatGrid() const {
    HeatView view = {NULL, 0U, 0U, 0U, 0.0f};
    if (state_ != NULL) {
        const FireState* fire = state(state_);
        view.values = fire->heat;
        view.width = fire->config.width;
        view.height = fire->config.height;
        view.stride = fire->config.width;
        view.max_heat = fire->config.max_heat;
    }
    return view;
}

}  // namespace doom_fire
