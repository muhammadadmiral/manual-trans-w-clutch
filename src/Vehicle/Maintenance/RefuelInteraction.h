#pragma once

using Ped = int;
using Vehicle = int;

namespace RefuelInteraction {

void Reset();
void TrackVehicle(Vehicle vehicle);
void Update(Ped player);
bool IsActive();
bool IsPromptVisible();
Vehicle GetTrackedVehicle();

} // namespace RefuelInteraction
