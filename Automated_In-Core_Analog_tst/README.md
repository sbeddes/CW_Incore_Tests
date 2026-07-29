# Keyboard-Controlled In-Core Analog Test

This directory contains a standalone Visual Studio 2010 project that combines the Martel current-source behavior from `In-Core_Analog_tst` with the VTI Fgen relay control used by `DCOffset`.

## Operator controls

The program starts on analog channel 1 at 1 uA and keeps that state active until the operator presses a key.

- `RIGHT ARROW` moves to the next channel.
- `LEFT ARROW` moves to the previous channel.
- `UP ARROW` increases current by 1 uA.
- `DOWN ARROW` decreases current by 1 uA.
- `ESC` places the Martel in `STBY`, disables every relay output, returns the Martel to `LOCAL`, and exits.
- `Ctrl+C` remains available as a backup safe shutdown.

The settings roll over in both directions:

- Channel 8 followed by `RIGHT ARROW` returns to channel 1.
- Channel 1 followed by `LEFT ARROW` returns to channel 8.
- 4 uA followed by `UP ARROW` returns to 1 uA.
- 1 uA followed by `DOWN ARROW` returns to 4 uA.

## Safety behavior

The current is limited to 1-4 uA in two places. The keyboard logic only generates values inside that range, and the final output function rejects any value below 1 uA or above 4 uA before sending it to the Martel.

Before a channel changes, the program:

1. Places the Martel in `STBY`.
2. Disables all eight Fgen outputs.
3. Waits for the previous relay pair to release.
4. Applies 24 V to the selected Fgen channel.
5. Waits for the selected HIGH and LOW relay pair to settle.
6. Restores the currently selected 1-4 uA output.

Before a current changes, the program places the Martel in `STBY`, sends the new validated current command, and then sends `OPER`.

At ESC, Ctrl+C, or any detected error, the program uses one common shutdown path: current is removed first, all relays are released second, the Fgen session is closed, and the Martel is returned to `LOCAL`.

## Included files

- `Automated_In-Core_Analog_tst.sln` - Visual Studio 2010 solution
- `Automated_In-Core_Analog_tst.vcxproj` - x64 C++ project
- `Automated_In-Core_Analog_tst.cpp` - keyboard-controlled test program
- `config.h` - IP address, COM port, current limits, relay voltage, channel count, and timing constants

No R*TIME, MMI database, `mmidata`, `ptsinuse`, or DMM dependencies are used.

## Required installed vendor components

The test computer must already have the same IVI/VTI components used by `DCOffset` installed and registered:

- IVI Shared Components
- `IviDriverTypeLib.dll`
- `IviSessionFactory.dll`
- `VTEXFgen_64.dll`
- The VTI EX1200 Fgen driver and its runtime dependencies

These vendor DLLs are referenced through `#import`; they are not copied into the repository.

## Current configuration

- Martel serial port: `COM6`
- VTI resource: `TCPIP::10.107.42.49::INSTR`
- Analog channels: 8
- Current range: 1-4 uA
- Initial state: channel 1 at 1 uA
- Relay command voltage: 24 V
- Fgen initialization options: empty string, matching the active initialization call in `DCOffset`

Edit `config.h` if any of these values change.

## Relay wiring assumption

Each Fgen output controls one analog channel. The HIGH and LOW relay coils for that analog channel are jumpered in parallel so that both relays energize together from the same 24 V command.

Before applying power, verify that one Fgen channel can supply the combined current of both parallel relay coils and that the relay circuit includes appropriate inductive-load suppression.

## Build

Open `Automated_In-Core_Analog_tst.sln` in Visual Studio 2010 and build the x64 configuration. The project intentionally does not use precompiled headers or CLR support.

If the Fgen driver does not automatically select the same card as `DCOffset`, change `FGEN_DRIVER_OPTIONS` in `config.h` to the slot-specific option shown in its comment.
