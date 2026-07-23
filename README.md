# Manual Transmission with Clutch Mod for GTA V

![C++](https://img.shields.io/badge/Language-C++-blue.svg)
![ScriptHookV](https://img.shields.io/badge/Platform-ScriptHookV-green.svg)
![License](https://img.shields.io/badge/License-Open--Source-orange.svg)

## Overview
This project is an open-source manual transmission mod built to provide a realistic driving experience in Grand Theft Auto V. It features manual gear shifting and a basic clutch simulation, achieved by dynamically manipulating the game's internal `CVehicle` memory layout.

## Main Features
* **Manual Gear Shifting**: Full control over the vehicle's transmission. The mod locks the game's automatic shifting mechanism, allowing you to hold gears up to the engine's redline.
* **Basic Clutch System**: Pressing the clutch key disconnects the engine torque from the wheels, allowing the vehicle to coast freely while engine RPM responds to the throttle.
* **Smart Vehicle Filtering**: Automatically ignores aircraft, boats, helicopters, trains, and vehicles with CVT/electric transmissions (1 gear).
* **Dynamic Memory Reading (AOB)**: Implements AOB (Array of Bytes) pattern scanning to dynamically resolve `CVehicle` offsets. Includes `.ini` fallback for older or unsupported game versions.

## Mod Configuration (`manual-trans.ini`)
Controls and memory fallback behaviors can be fully customized via the `manual-trans.ini` file located alongside the `.asi` plugin.

```ini
[Controls]
; Virtual-key codes: 16 = LSHIFT, 17 = LCONTROL, 88 = X
ShiftUp=16
ShiftDown=17
ClutchKey=88

[Debug]
Overlay=1
```

## Project Architecture
The source code has been heavily modularized for future expansion:
* `src/Core/`: Centralized configurations and core module definitions (`Config.cpp`).
* `src/Memory/`: Contains the Memory Pattern Scanner (`AOBScanner.cpp`) for dynamically finding addresses in `GTA5.exe`.
* `src/Vehicle/`: Handles direct memory reads/writes to the game engine's `CVehicle` entity structure (`VehicleData.cpp`).
* `main.cpp`: The primary ScriptHookV thread that handles player input, rendering debug UI, and tying all modules together.

## Build Instructions
1. Clone or download this repository.
2. Open `manual-trans-w-clutch.vcxproj` using **Visual Studio 2022** (or Visual Studio Build Tools).
3. Set the build configuration to **Release** and platform to **x64**.
4. Press `Ctrl+Shift+B` to compile (Build Solution).
5. Copy the generated `manual-trans-w-clutch.asi` and `manual-trans.ini` into your main GTA V installation directory (alongside `GTA5.exe` and `ScriptHookV.dll`).
