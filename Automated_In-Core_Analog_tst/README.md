# Automated In-Core Analog Test

This directory contains a standalone Visual Studio 2010 project that combines the current-source behavior from `In-Core_Analog_tst` with the VTI Fgen channel control used by `DCOffset`.

## Test sequence

For each of eight analog channels, the program:

1. Places the Martel current calibrator in `STBY`.
2. Disables all eight VTI Fgen outputs.
3. Applies 24 V to the selected Fgen channel.
4. The selected output energizes the jumpered HIGH and LOW relay coils for that analog channel.
5. Applies 1, 2, 3, and 4 uA through the Martel calibrator.
6. Holds each current for 30 seconds.
7. Returns the Martel to `STBY` before releasing the relay pair.
8. Moves to the next analog channel.

At normal completion, error, or Ctrl+C, the program commands `STBY`, disables all relay outputs, closes the Fgen session, returns the Martel to `LOCAL`, and closes the serial port.

## Included files

- `Automated_In-Core_Analog_tst.sln` - Visual Studio 2010 solution
- `Automated_In-Core_Analog_tst.vcxproj` - x64 C++ project
- `Automated_In-Core_Analog_tst.cpp` - combined test program
- `config.h` - IP address, COM port, relay voltage, channel count, and timing constants

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
- Current steps: 1-4 uA
- Dwell time: 30 seconds per current
- Relay command voltage: 24 V
- Fgen initialization options: empty string, matching the active initialization call in `DCOffset`

Edit `config.h` if any of these values change.

## Relay wiring assumption

Each Fgen output controls one analog channel. The HIGH and LOW relay coils for that analog channel are jumpered in parallel so that both relays energize together from the same 24 V command.

Before applying power, verify that one Fgen channel can supply the combined current of both parallel relay coils and that the relay circuit includes appropriate inductive-load suppression.

## Build

Open `Automated_In-Core_Analog_tst.sln` in Visual Studio 2010 and build the x64 configuration. The project intentionally does not use precompiled headers or CLR support.

If the Fgen driver does not automatically select the same card as `DCOffset`, change `FGEN_DRIVER_OPTIONS` in `config.h` to the slot-specific option shown in its comment.
