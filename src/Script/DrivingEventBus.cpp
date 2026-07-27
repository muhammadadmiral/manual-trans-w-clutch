// =============================================================================
// DrivingEventBus.cpp — Lightweight event pub/sub implementation.
// =============================================================================
#include "DrivingEventBus.h"

#include <array>
#include <utility>
#include <vector>

namespace DrivingEventBus {
namespace {

static constexpr int kEventCount = static_cast<int>(Event::_Count);

struct EventChannel {
    std::vector<Callback> subscribers;
};

std::array<EventChannel, kEventCount> s_channels;

struct PendingEvent {
    Event event;
    EventData data;
};

std::vector<PendingEvent> s_pending;

} // namespace

void Subscribe(Event e, Callback callback) {
    const int idx = static_cast<int>(e);
    if (idx >= 0 && idx < kEventCount)
        s_channels[idx].subscribers.push_back(std::move(callback));
}

void Subscribe(Event e, std::function<void()> callback) {
    Subscribe(e, [callback = std::move(callback)](const EventData&) {
        callback();
    });
}

void Publish(Event e, const EventData& data) {
    const int idx = static_cast<int>(e);
    if (idx >= 0 && idx < kEventCount)
        s_pending.push_back({e, data});
}

void FlushFrame() {
    // Swap first: an event published by a callback is deliberately deferred
    // until the following frame instead of recursively re-entering the bus.
    std::vector<PendingEvent> frameEvents;
    frameEvents.swap(s_pending);
    for (const auto& pending : frameEvents) {
        const int idx = static_cast<int>(pending.event);
        for (const auto& callback : s_channels[idx].subscribers)
            callback(pending.data);
    }
}

void ClearPending() {
    s_pending.clear();
}

void Reset() {
    for (auto& ch : s_channels)
        ch.subscribers.clear();
    ClearPending();
}

} // namespace DrivingEventBus
