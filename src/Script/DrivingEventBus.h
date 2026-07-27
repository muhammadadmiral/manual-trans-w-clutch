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

using Vehicle = int;

enum class Event {
    // Gear events
    GearShiftUp,
    GearShiftDown,
    GearNeutral,
    GearGrind,
    SelectorChanged,

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
    ESCActivated,
    LaunchControlArmed,
    RolloverWarning,
    ParkingBrakeEngaged,
    ParkingBrakeReleased,

    // Environment events
    WaterIngestion,

    // Count (must be last)
    _Count
};

// Small, dependency-free payload shared by publishers. Fields that are not
// relevant to an event stay at their defaults.
struct EventData {
    Vehicle vehicle = 0;
    int fromGear = 0;
    int toGear = 0;
    float severity = 0.0f;
    float value = 0.0f;
    bool quickShift = false;
};

using Callback = std::function<void(const EventData&)>;

// Subscribe a callback to an event. Callbacks are called in order of registration.
void Subscribe(Event e, Callback callback);
void Subscribe(Event e, std::function<void()> callback);

// Publish an event. Callbacks will fire at the next FlushFrame().
void Publish(Event e, const EventData& data = {});

// Process all pending events. Call once per frame at the beginning of the loop.
void FlushFrame();

// Drop queued events while keeping long-lived subscriptions intact.
void ClearPending();

// Remove all subscriptions and pending events.
void Reset();

} // namespace DrivingEventBus
