# Manual Transmission with Clutch Mod for GTA V (E&E Compatible)

![C++](https://img.shields.io/badge/Language-C++-blue.svg)
![ScriptHookV](https://img.shields.io/badge/Platform-ScriptHookV-green.svg)
![License](https://img.shields.io/badge/License-Open--Source-orange.svg)
![GTA V Mod](https://img.shields.io/badge/Type-GTA%20V%20Mod-lightgrey.svg)

## Overview
This project is an open-source, modern manual transmission mod built from scratch to replace legacy mods that are no longer supported on the Grand Theft Auto V Enhanced Edition. This mod provides a much more realistic driving experience with manual gear shifting simulation, clutch mechanics, and real engine dynamics.

## Main Features
* **Realistic Clutch Simulation**: The engine can stall if the clutch is not used correctly or released too quickly without throttle.
* **Manual Gear Shifting (H-Shifter & Sequential)**: Full control over the vehicle's transmission for various driving styles using a keyboard or standard controller.
* **Custom HUD & Tachometer**: A modern and lightweight visual interface to display actual RPM, gear position, and clutch status directly on the screen.
* **Dynamic Memory Manipulation**: Reads and overrides core engine states (RPM) and gear directly into the game's memory using Pattern Scanning for absolute precision and game version compatibility.

## Project Architecture
This project is divided into two main parts to facilitate development, feature isolation, and testing:

* `poc-memory-reader/`: An isolated Proof-of-Concept (PoC) sub-project. This module is experimental and designed exclusively to dynamically scan, read, and verify memory offsets from the CVehicle class within the GTA V game engine (such as RPM, Gear, Clutch, and Throttle).
* `src/`: The core mod module that encapsulates the overall functionality. It contains modular transmission physics, clutch mechanics, gear shifting logic, engine stall simulation, player input reading, and UI (HUD) rendering processes.

## Prerequisites
Before starting development or compilation, ensure you have:
* **Visual Studio 2022** (with Desktop development with C++ workload)
* **ScriptHookV SDK** by Alexander Blade (contains inc and lib folders)
* The latest original version of Grand Theft Auto V

## Build Instructions
1. Clone or download this repository to your computer.
2. Extract the downloaded ScriptHookV SDK.
3. Copy the contents of the `inc` folder (from the SDK) into the `poc-memory-reader/include/` folder or configure Include Directories in Visual Studio to point to your SDK.
4. Copy the contents of the `lib` folder (from the SDK) into the `poc-memory-reader/lib/` folder or configure Library Directories in Visual Studio.
5. Open the `.sln` (Solution) file via Visual Studio 2022 once created.
6. Set the build configuration to Release and platform to x64.
7. Press `Ctrl+Shift+B` to compile (Build Solution).
8. Copy the output file with the `.asi` extension into your main GTA V installation directory (alongside GTA5.exe).

## Contribution
Contributions, bug reports, and suggestions are always welcome. If you have bug fixes, performance improvements, or want to add new features, please feel free to create a Pull Request.
