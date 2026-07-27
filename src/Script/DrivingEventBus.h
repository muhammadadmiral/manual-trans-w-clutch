// =============================================================================
// DrivingEventBus.h — Lightweight event pub/sub system for inter-module
// communication without direct coupling.
//
// Usage:
//   DrivingEventBus::Subscribe(Event::EngineStall, []() { PlayStallSound(); });
//   DrivingEventBus::Publish(Event::EngineStall);
//   DrivingEventBus::FlushFrame();  // at the top of each frame
// =============================================================================
#pragma once

#include <functional>
#include <vector>

namespace DrivingEventBus {

enum class Event {
    // Gear events
    GearShiftUp,
    GearShiftDown,
    GearNeutral,

    // Engine events
    EngineStart,
    EngineStall,

    // Clutch events
    ClutchDump,
    ClutchOverheat,

    // Drivetrain events
    MoneyShift,
    TurboBlowoff,

    // Assist events
    ABSActivated,
    TCSActivated,
    LaunchControlArmed,

    // Environment events
    WaterIngestion,

    // Count (must be last)
    _Count
};

// Subscribe a callback to an event. Callbacks are called in order of registration.
void Subscribe(Event e, std::function<void()> callback);

// Publish an event. Callbacks will fire at the next FlushFrame().
void Publish(Event e);

// Process all pending events. Call once per frame at the beginning of the loop.
void FlushFrame();

// Remove all subscriptions and pending events.
void Reset();

} // namespace DrivingEventBus
