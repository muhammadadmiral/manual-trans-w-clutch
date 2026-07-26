#pragma once

using Ped = int;
using Vehicle = int;

namespace ServiceInteraction {

void TrackVehicle(Vehicle vehicle);
void Update(Ped player);
bool IsActive();

} // namespace ServiceInteraction
