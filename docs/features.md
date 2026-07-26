# Melar Transmission - Feature Ideas

This document tracks upcoming ideas and enhancements for Melar Transmission.

## 1. Custom Asset Structure
We will set up a dedicated folder in the game directory to load custom assets.
- `melar-transmission/audio/`
- `melar-transmission/images/`

## 2. Audio Enhancements
- **Parking Sensors**: 
  - Play a fast beeping sound that increases in tempo when close to objects while in Reverse (`R`).
  - Solid tone (`titttttt`) when extremely close to a wall/object.
- **MotoGP Gear Shift (Sport Bikes)**:
  - Custom sound arrays specifically for sport bikes (e.g., Bati).
  - Quick-shifter popping sound effect.
- **Turbo Blow-Off Valve (BOV)**:
  - Play a flutter sound when releasing the throttle at high RPM, depending on the vehicle's engine upgrades.

## 3. Visual Enhancements (GUI)
- **Pedal Overlay UI**:
  - Replace current colored bars with SVG or video animations for the clutch, brake, and throttle pedals.
  - Clutch and gas handle animations for motorcycles.
- **Parking Indicator**:
  - Blinking visual indicator when parking sensors are active.

## 4. Edge Cases & Polish
- Ensure all vehicles are properly classified (e.g., distinguishing between cars and bikes for sound/handling).
- Prevent memory-related crashes during rapid gear shifting (implemented via `GearLogic` state checks).
