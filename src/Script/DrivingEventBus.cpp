// =============================================================================
// DrivingEventBus.cpp — Lightweight event pub/sub implementation.
// =============================================================================
#include "DrivingEventBus.h"

#include <array>
#include <vector>

namespace DrivingEventBus {
namespace {

static constexpr int kEventCount = static_cast<int>(Event::_Count);

struct EventChannel {
    std::vector<std::function<void()>> subscribers;
    bool pending = false;
};

std::array<EventChannel, kEventCount> s_channels;

} // namespace

void Subscribe(Event e, std::function<void()> callback) {
    const int idx = static_cast<int>(e);
    if (idx >= 0 && idx < kEventCount)
        s_channels[idx].subscribers.push_back(std::move(callback));
}

void Publish(Event e) {
    const int idx = static_cast<int>(e);
    if (idx >= 0 && idx < kEventCount)
        s_channels[idx].pending = true;
}

void FlushFrame() {
    for (int i = 0; i < kEventCount; ++i) {
        if (s_channels[i].pending) {
            s_channels[i].pending = false;
            for (const auto& callback : s_channels[i].subscribers)
                callback();
        }
    }
}

void Reset() {
    for (auto& ch : s_channels) {
        ch.subscribers.clear();
        ch.pending = false;
    }
}

} // namespace DrivingEventBus
