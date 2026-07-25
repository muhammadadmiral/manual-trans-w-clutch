#pragma once

namespace GearboxPatches {

// Patch cuma hidup saat drivetrain mod lagi mengambil alih kendaraan pemain.
// Kalau signature build GTA berubah, aktivasi batal total.
bool SetActive(bool active);
void Shutdown();

bool IsApplied();
bool IsResolved();
const char *GetFailureReason();

} // namespace GearboxPatches
