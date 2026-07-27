// =============================================================================
// SignalController.h — Turn signal & hazard light logic.
// =============================================================================
#pragma once

using Vehicle = int;

class SignalController {
public:
    void Reset();
    void Update(Vehicle veh);

    int  GetActiveSignal() const { return m_activeSignal; }

private:
    int m_activeSignal = 0;
};
