// =============================================================================
// VehicleSessionController.h — Detects vehicle changes and coordinates resets.
// =============================================================================
#pragma once

#include "../../Vehicle/VehicleData.h"
#include "../../Vehicle/VehicleProfile.h"
#include <Windows.h>

using Vehicle = int;

class EngineController;
class SignalController;

class VehicleSessionController {
public:
    // Returns true if the vehicle just changed this frame.
    bool CheckAndUpdate(Vehicle current, int maxGear);

    bool    IsInGracePeriod() const;
    bool    JustChanged()     const { return m_justChanged; }
    Vehicle GetActiveVehicle() const { return m_activeVehicle; }
    bool    IsNotificationShown() const { return m_notifyShown; }

    // Marks notification as shown.
    void    MarkNotified()   { m_notifyShown = true; }

    // Full reset (player exited vehicle).
    void Reset();

    // Reset all sub-systems for a new vehicle. Called internally by CheckAndUpdate.
    void ResetSubsystems(Vehicle veh);

private:
    Vehicle   m_activeVehicle   = 0;
    ULONGLONG m_enterTick       = 0;
    bool      m_justChanged     = false;
    bool      m_notifyShown     = false;
};
