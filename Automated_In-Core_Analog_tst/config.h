#pragma once

// Martel 3001 current calibrator serial connection.
static const char* const CURRENT_CALIBRATOR_PORT = "\\\\.\\COM6";

// VTI EX1268 chassis containing the Fgen card used by DCOffset.
static const char* const FGEN_RESOURCE = "TCPIP::10.107.42.49::INSTR";

// DCOffset currently initializes the Fgen driver with an empty options string.
// Keep the same working behavior here. If the chassis ever requires explicit
// slot selection, change this to: "DriverSetup= Slots= (8=Fgen)"
static const char* const FGEN_DRIVER_OPTIONS = "";

static const int NUMBER_OF_TEST_CHANNELS = 8;
static const int START_CURRENT_UA = 1;
static const int END_CURRENT_UA = 4;

// Each current level remains applied for 30 seconds before advancing.
static const int CURRENT_DWELL_SECONDS = 30;

// Each Fgen channel drives the jumpered HIGH and LOW relay coils for one
// analog test channel.
static const double RELAY_VOLTAGE = 24.0;
static const int RELAY_RELEASE_DELAY_MS = 500;
static const int RELAY_SETTLE_DELAY_MS = 500;
