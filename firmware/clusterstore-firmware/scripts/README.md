# Firmware Scripts

## `check-firmware-env.ps1`

This script is the hardware-free entry point for validating the native firmware workspace on Windows.

It does six things:

- locates `arm-none-eabi-gcc`, `cmake`, a host C compiler, and `ninja`
- locates `mingw32-make` and prefers it for MinGW host builds on Windows
- reports whether the STM32Cube G4 HAL driver tree is present under `bsp/stm32g474/cube_generated/Drivers/`
- reports which tools are missing versus only absent from `PATH`
- runs an ARM syntax smoke compile over the portable core, no-HAL BSP, and host-side test sources
- attempts a host CMake configure/build only when `cmake`, a host compiler, and either `mingw32-make` or `ninja` are available

Run it from PowerShell:

```powershell
.\scripts\check-firmware-env.ps1
```

If local execution policy blocks repo scripts, use:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\check-firmware-env.ps1
```

Optional flags:

- `-SkipSmoke`
- `-SkipHostConfigure`
