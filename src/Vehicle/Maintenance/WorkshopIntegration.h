#pragma once

using Ped = int;
using Vehicle = int;

namespace WorkshopIntegration {

void Reset();
bool Update(Ped playerPed, Vehicle vehicle, bool engineOn);
bool IsOpen();
bool IsNearServiceBay();

} // namespace WorkshopIntegration
