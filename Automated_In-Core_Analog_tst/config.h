#pragma once

// This file keeps the values that may need to change from one test setup to
// another in one place. The main program should not need to be edited just
// because the COM port, IP address, relay timing, or channel count changes.

// The Martel 3001 is connected through a USB-to-serial adapter. Windows uses
// the "\\.\COM#" format so the same code will still work if the assigned COM
// number is greater than COM9.
static const char* const CURRENT_CALIBRATOR_PORT = "\\\\.\\COM6";

// This is the same EX1268 chassis address used by DCOffset. The chassis holds
// the Fgen card that supplies the 24 V relay-control outputs.
static const char* const FGEN_RESOURCE = "TCPIP::10.107.42.49::INSTR";

// DCOffset currently initializes the Fgen driver with an empty options string,
// so this program keeps that same behavior. If the driver stops selecting the
// correct card automatically, use: "DriverSetup= Slots= (8=Fgen)"
static const char* const FGEN_DRIVER_OPTIONS = "";

// There are eight analog channels. Each channel uses one Fgen output because
// the HIGH and LOW relay coils for that channel are jumpered together.
static const int NUMBER_OF_TEST_CHANNELS = 8;
static const int INITIAL_TEST_CHANNEL = 1;

// The operator can only select 1, 2, 3, or 4 uA. The arrow-key rollover logic
// uses these limits, and the current-output function checks them again before
// any command is sent to the Martel.
static const int MIN_CURRENT_UA = 1;
static const int MAX_CURRENT_UA = 4;
static const int INITIAL_CURRENT_UA = 1;

// The relay coils require 24 V. One Fgen channel energizes both the HIGH and
// LOW relay coils for the selected analog channel at the same time.
static const double RELAY_VOLTAGE = 24.0;

// The release delay gives the previous relay pair time to open before another
// channel is selected. The settle delay gives the new relay pair time to close
// before the Martel current output is restored.
static const int RELAY_RELEASE_DELAY_MS = 500;
static const int RELAY_SETTLE_DELAY_MS = 500;

// The keyboard loop polls instead of blocking forever in _getch. This short
// delay keeps CPU use low while still making the arrow keys feel immediate and
// allowing Ctrl+C to be noticed when no key is being pressed.
static const int KEYBOARD_POLL_DELAY_MS = 25;
