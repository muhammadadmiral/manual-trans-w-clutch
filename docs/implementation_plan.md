# Realism System Implementation Plan

This plan outlines the architecture and gameplay mechanics for the requested "Realism System", which introduces persistent vehicle wear, interactive refueling, and oil degradation.

## User Review Required

> [!IMPORTANT]
> Please review the gameplay values (e.g., how fast oil degrades, how long the refueling animation should play) and confirm if you want me to proceed with this design!

## Open Questions
1. **Odometer Scale**: Realistically, cars need an oil change every 5,000 - 10,000 km. In a video game, this takes hundreds of hours to reach. Should we scale the odometer/oil life so that you need an oil change every ~100 km of driving?
2. **Refueling**: Do you want the character to physically hold a Jerry Can prop during the animation, or is a generic "working on vehicle" animation sufficient?

## Proposed Changes

---

### 1. Persistence & Odometer (`VehicleData.h` / `VehicleData.cpp`)

We will introduce a persistent save system for individual vehicles.
- **Save Location**: `melar-transmission-vehicles.ini`
- **Key**: Vehicle License Plate (e.g., `[Plate_46EDK202]`)
- **Tracked Stats**: `Odometer` (km), `OilLife` (0.0 to 1.0)
- **Logic**: Every few seconds of driving, update the odometer based on the distance traveled and save it to the INI file.

---

### 2. Interactive Refueling (`FuelSystem.h` / `FuelSystem.cpp`)

We will remove the "auto-refuel on engine off" placeholder and replace it with a fully interactive gas station system.

- **Detection**: Use `GET_CLOSEST_OBJECT_OF_TYPE` to scan for common gas pump models (`prop_gas_pump_1a`, `prop_gas_pump_1d`, etc.) within a 5-meter radius of the player.
- **Interaction**:
  - The player must be on foot, near a pump, and near their car.
  - A 3D prompt will appear: "Press [E] to Refuel".
- **Animation**:
  - Upon pressing [E], the player's controls are locked.
  - The player turns to face the vehicle.
  - We play a refueling animation (e.g., `timetable@gardener@filling_can`).
  - The fuel gauge slowly rises. When full (or when the player cancels), the animation ends.

---

### 3. Oil Degradation & Maintenance (`PhysicsEngine.cpp` / `InteractiveMenu`)

- **Degradation**: `OilLife` drops from 1.0 to 0.0 as the `Odometer` increases.
- **Consequences**: If `OilLife` is below 20%, the engine's base temperature runs hotter, and it stalls more easily. If it reaches 0%, the engine takes permanent damage until serviced.
- **Oil Change Mechanics**:
  - The player can walk to the front of their car (hood).
  - A prompt: "Press [E] to Change Oil".
  - The hood opens automatically, the player plays a mechanic animation (`anim@amb@clubhouse@tutorial@bkr_tut_ig3@`), and `OilLife` is restored to 1.0.

## Verification Plan

### Automated/Code Verification
- Ensure `melar-transmission-vehicles.ini` successfully creates sections for different license plates.
- Verify that `TASK_PLAY_ANIM` successfully triggers without crashing the script thread.

### Manual Verification
- Spawn a vehicle, drive to a gas station, get out, and test the refueling prompt and animation.
- Drive around to verify the odometer increments properly.
- Open the hood and perform an oil change.
