// Automated_In-Core_Analog_tst.cpp
//
// Purpose:
// This program gives the operator direct keyboard control of the in-core analog
// test. The Martel 3001 supplies the test current, and the VTI Fgen card supplies
// 24 V to the relay pair for the selected analog channel.
//
// Relay setup:
// Each analog channel has one HIGH relay and one LOW/ground relay. The two relay
// coils for a channel are jumpered together, so one Fgen output switches both
// relays at the same time. Fgen CH1 selects analog channel 1, CH2 selects analog
// channel 2, and this continues through CH8.
//
// Keyboard controls:
// RIGHT ARROW - move to the next analog channel
// LEFT ARROW  - move to the previous analog channel
// UP ARROW    - increase the current by 1 uA
// DOWN ARROW  - decrease the current by 1 uA
// ESC         - shut down both instruments and exit
//
// Both settings roll over. Channel 8 rolls back to channel 1, channel 1 rolls
// back to channel 8, 4 uA rolls back to 1 uA, and 1 uA rolls back to 4 uA.
// The current command is also checked immediately before it is sent so a future
// code change cannot command the Martel below 1 uA or above 4 uA.
//
// Visual Studio 2010 compatible.

#include <Windows.h>
#include <signal.h>
#include <conio.h>
#include <iostream>
#include <iomanip>
#include <string>
#include <sstream>
#include <comdef.h>

// These are the same IVI and VTI COM libraries used by DCOffset. The DLLs must
// already be installed and registered on the Windows test computer.
#import "IviDriverTypeLib.dll" no_namespace
#import "IviSessionFactory.dll" no_namespace
#import "VTEXFgen_64.dll" no_namespace

#include "config.h"

// Ctrl+C remains available as a backup stop. The signal handler only sets a flag
// because hardware commands should not be sent from inside a signal handler.
volatile sig_atomic_t gStopRequested = 0;

// The Martel serial handle is global because the serial and shutdown functions
// all use the same open connection.
HANDLE gSerialPort = INVALID_HANDLE_VALUE;

// Windows returns a two-byte sequence for an arrow key. The first byte is 0 or
// 224 and the second byte identifies which arrow was pressed.
enum KeyAction
{
    KEY_ACTION_NONE,
    KEY_ACTION_LEFT,
    KEY_ACTION_RIGHT,
    KEY_ACTION_UP,
    KEY_ACTION_DOWN,
    KEY_ACTION_ESCAPE,
    KEY_ACTION_STOP_REQUESTED
};

void handleCtrlC(int)
{
    gStopRequested = 1;
}

// Send one command to the Martel. A carriage return is added here so every
// command is terminated the same way.
bool sendCalibratorCommand(const std::string& command)
{
    if (gSerialPort == INVALID_HANDLE_VALUE)
        return false;

    const std::string message = command + "\r";
    DWORD bytesWritten = 0;

    if (!WriteFile(
            gSerialPort,
            message.c_str(),
            static_cast<DWORD>(message.size()),
            &bytesWritten,
            NULL))
    {
        return false;
    }

    return bytesWritten == static_cast<DWORD>(message.size());
}

// Read one Martel response until a carriage return is received or the timeout
// expires. The response is only used during the startup identity check.
std::string readCalibratorResponse(DWORD timeoutMs)
{
    std::string response;
    char character = 0;
    DWORD bytesRead = 0;
    const DWORD startTime = GetTickCount();

    while ((GetTickCount() - startTime) < timeoutMs)
    {
        bytesRead = 0;

        if (!ReadFile(
                gSerialPort,
                &character,
                1,
                &bytesRead,
                NULL))
        {
            return "READ ERROR";
        }

        if (bytesRead == 0)
        {
            Sleep(10);
            continue;
        }

        if (character == '\r')
            break;

        if (character != '\n')
            response += character;
    }

    return response;
}

// Relay delays stay interruptible so Ctrl+C can stop the program even while a
// relay is releasing or settling.
bool waitWithAbort(int milliseconds)
{
    int elapsed = 0;

    while (elapsed < milliseconds)
    {
        if (gStopRequested)
            return false;

        const int remaining = milliseconds - elapsed;
        const int step = remaining < 50 ? remaining : 50;
        Sleep(step);
        elapsed += step;
    }

    return true;
}

// Configure the Martel serial connection using the same settings as the original
// in-core analog test: 9600 baud, 8 data bits, no parity, one stop bit, and
// XON/XOFF software flow control.
bool configureSerialPort()
{
    DCB settings;
    COMMTIMEOUTS timeouts;

    ZeroMemory(&settings, sizeof(settings));
    settings.DCBlength = sizeof(settings);

    if (!GetCommState(gSerialPort, &settings))
        return false;

    settings.BaudRate = CBR_9600;
    settings.ByteSize = 8;
    settings.Parity = NOPARITY;
    settings.StopBits = ONESTOPBIT;
    settings.fBinary = TRUE;
    settings.fParity = FALSE;
    settings.fOutX = TRUE;
    settings.fInX = TRUE;
    settings.fOutxCtsFlow = FALSE;
    settings.fOutxDsrFlow = FALSE;

    if (!SetCommState(gSerialPort, &settings))
        return false;

    ZeroMemory(&timeouts, sizeof(timeouts));
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 100;
    timeouts.WriteTotalTimeoutConstant = 1000;

    return SetCommTimeouts(gSerialPort, &timeouts) != 0;
}

// Open and verify the Martel before any relay is energized. Startup ends with
// the Martel in REMOTE and STBY so the first channel can be selected safely.
bool openCurrentCalibrator()
{
    gSerialPort = CreateFileA(
        CURRENT_CALIBRATOR_PORT,
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        0,
        NULL);

    if (gSerialPort == INVALID_HANDLE_VALUE)
    {
        std::cerr << "Could not open current calibrator on "
                  << CURRENT_CALIBRATOR_PORT << std::endl;
        return false;
    }

    if (!configureSerialPort())
    {
        std::cerr << "Could not configure current calibrator serial port."
                  << std::endl;
        CloseHandle(gSerialPort);
        gSerialPort = INVALID_HANDLE_VALUE;
        return false;
    }

    // Clear any characters left in the serial buffers from a previous run.
    PurgeComm(gSerialPort, PURGE_RXCLEAR | PURGE_TXCLEAR);

    if (!sendCalibratorCommand("*IDN?"))
    {
        std::cerr << "Failed to send *IDN? to current calibrator."
                  << std::endl;
        return false;
    }

    const std::string response = readCalibratorResponse(3000);

    if (response.empty() || response == "READ ERROR")
    {
        std::cerr << "Failed to receive current calibrator identity."
                  << std::endl;
        return false;
    }

    std::cout << "Current calibrator: " << response << std::endl;

    if (!sendCalibratorCommand("REMOTE"))
        return false;

    Sleep(200);

    if (!sendCalibratorCommand("STBY"))
        return false;

    Sleep(200);
    return true;
}

// Convert an integer channel number into the name expected by the VTI driver.
std::string fgenChannelName(int channelNumber)
{
    std::ostringstream stream;
    stream << "CH" << channelNumber;
    return stream.str();
}

void printComError(const char* operation, const _com_error& error)
{
    std::cerr << operation << " failed. HRESULT=0x"
              << std::hex << static_cast<unsigned long>(error.Error())
              << std::dec << std::endl;
}

// Disable one relay-control output. The output is disabled before its stored
// offset is reset to zero.
bool disableRelayOutput(IVTEXFgenPtr fgen, int channelNumber)
{
    try
    {
        const std::string name = fgenChannelName(channelNumber);
        _bstr_t channel(name.c_str());

        fgen->Output->Enabled[channel] = VARIANT_FALSE;
        fgen->StandardWaveform->DCOffset[channel] = 0.0;
        return true;
    }
    catch (const _com_error& error)
    {
        printComError("Disable relay output", error);
        return false;
    }
}

// Make a best effort to turn off all eight relay outputs. The loop continues if
// one channel reports an error so the program still attempts to release the rest.
bool allRelaysOff(IVTEXFgenPtr fgen)
{
    bool success = true;

    if (fgen == NULL)
        return false;

    for (int channel = 1; channel <= NUMBER_OF_TEST_CHANNELS; ++channel)
    {
        if (!disableRelayOutput(fgen, channel))
            success = false;
    }

    return success;
}

// Configure all Fgen outputs for DC voltage operation. They remain disabled and
// at zero volts until a channel is selected.
bool configureRelayOutputs(IVTEXFgenPtr fgen)
{
    try
    {
        for (int channelNumber = 1;
             channelNumber <= NUMBER_OF_TEST_CHANNELS;
             ++channelNumber)
        {
            const std::string name = fgenChannelName(channelNumber);
            _bstr_t channel(name.c_str());

            fgen->Output->Enabled[channel] = VARIANT_FALSE;
            fgen->Output->DriveMode[channel] = VTEXFgenDriveModeVoltage;
            fgen->Output->Range[channel] = RELAY_VOLTAGE;
            fgen->Output->ChannelOutputMode[channel] =
                VTEXFgenOutputModeFunction;
            fgen->StandardWaveform->Waveform[channel] =
                VTEXFgenWaveformDC;
            fgen->StandardWaveform->DCOffset[channel] = 0.0;
        }
    }
    catch (const _com_error& error)
    {
        printComError("Configure relay outputs", error);
        return false;
    }

    return allRelaysOff(fgen);
}

// Connect to the VTI chassis and prepare the Fgen card used for relay control.
bool initializeRelayFgen(IVTEXFgenPtr& fgen)
{
    try
    {
        IIviDriverPtr driver(__uuidof(VTEXFgen));

        driver->Initialize(
            FGEN_RESOURCE,
            VARIANT_TRUE,
            VARIANT_TRUE,
            FGEN_DRIVER_OPTIONS);

        fgen = driver;

        if (!configureRelayOutputs(fgen))
            return false;

        std::cout << "Relay Fgen connected at " << FGEN_RESOURCE
                  << std::endl;
        return true;
    }
    catch (const _com_error& error)
    {
        printComError("Initialize relay Fgen", error);
        return false;
    }
}

// Select one analog channel using break-before-make switching. All relay outputs
// are removed first, the old pair is allowed to release, and only then is 24 V
// applied to the requested channel's jumpered HIGH and LOW relay coils.
bool selectRelayChannel(IVTEXFgenPtr fgen, int channelNumber)
{
    if (channelNumber < 1 || channelNumber > NUMBER_OF_TEST_CHANNELS)
    {
        std::cerr << "Refusing invalid relay channel " << channelNumber
                  << "." << std::endl;
        return false;
    }

    if (!allRelaysOff(fgen))
        return false;

    if (!waitWithAbort(RELAY_RELEASE_DELAY_MS))
        return false;

    try
    {
        const std::string name = fgenChannelName(channelNumber);
        _bstr_t channel(name.c_str());

        fgen->StandardWaveform->DCOffset[channel] = RELAY_VOLTAGE;
        fgen->Output->Enabled[channel] = VARIANT_TRUE;
    }
    catch (const _com_error& error)
    {
        printComError("Select relay channel", error);
        allRelaysOff(fgen);
        return false;
    }

    return waitWithAbort(RELAY_SETTLE_DELAY_MS);
}

// Build the Martel command without std::to_string so the project remains
// compatible with Visual Studio 2010.
std::string currentCommand(int microamps)
{
    std::ostringstream stream;
    stream << "OUT " << microamps << " uA";
    return stream.str();
}

// STBY removes the active current before a current value or relay channel is
// changed. We do this even when changing from one allowed current to another so
// a failed command cannot leave the old current active during a partial update.
bool placeCurrentCalibratorInStandby()
{
    if (gSerialPort == INVALID_HANDLE_VALUE)
        return false;

    const bool success = sendCalibratorCommand("STBY");
    Sleep(200);
    return success;
}

// This is the final current safety gate. Keyboard rollover should only produce
// values from 1 through 4 uA, but this check prevents any out-of-range value from
// reaching the calibrator even if the calling code is changed later.
bool enableCurrentOutput(int microamps)
{
    if (microamps < MIN_CURRENT_UA || microamps > MAX_CURRENT_UA)
    {
        std::cerr << "Refusing unsafe current command: " << microamps
                  << " uA. Allowed range is " << MIN_CURRENT_UA
                  << "-" << MAX_CURRENT_UA << " uA." << std::endl;
        return false;
    }

    if (!sendCalibratorCommand(currentCommand(microamps)))
    {
        std::cerr << "Failed to set current to " << microamps
                  << " uA." << std::endl;
        return false;
    }

    if (!sendCalibratorCommand("OPER"))
    {
        std::cerr << "Failed to enable current output." << std::endl;
        return false;
    }

    return true;
}

// Change the current in a controlled order: STBY, set the new allowed value,
// then OPER. The output is never intentionally active while the value changes.
bool changeCurrentOutput(int microamps)
{
    if (!placeCurrentCalibratorInStandby())
    {
        std::cerr << "Failed to place current calibrator in standby."
                  << std::endl;
        return false;
    }

    return enableCurrentOutput(microamps);
}

// Change channels in the same safe order we discussed. Current is removed first,
// the old relay pair is released, the new pair is selected, and then the current
// is restored at the operator's existing setting.
bool changeSelectedChannel(
    IVTEXFgenPtr fgen,
    int newChannel,
    int currentMicroamps)
{
    if (!placeCurrentCalibratorInStandby())
    {
        std::cerr << "Failed to place current calibrator in standby."
                  << std::endl;
        return false;
    }

    if (!selectRelayChannel(fgen, newChannel))
        return false;

    if (!enableCurrentOutput(currentMicroamps))
    {
        // If current cannot be restored, release the newly selected relays before
        // returning the failure to the main loop.
        allRelaysOff(fgen);
        return false;
    }

    return true;
}

// Move one step up and wrap from the maximum back to the minimum.
int incrementWithRollover(int value, int minimum, int maximum)
{
    return value >= maximum ? minimum : value + 1;
}

// Move one step down and wrap from the minimum back to the maximum.
int decrementWithRollover(int value, int minimum, int maximum)
{
    return value <= minimum ? maximum : value - 1;
}

// Polling instead of blocking forever in _getch allows the Ctrl+C flag to be
// noticed even while the operator is not pressing keys.
KeyAction readKeyAction()
{
    while (!gStopRequested)
    {
        if (!_kbhit())
        {
            Sleep(KEYBOARD_POLL_DELAY_MS);
            continue;
        }

        const int firstByte = _getch();

        if (firstByte == 27)
            return KEY_ACTION_ESCAPE;

        if (firstByte == 0 || firstByte == 224)
        {
            const int secondByte = _getch();

            switch (secondByte)
            {
                case 75:
                    return KEY_ACTION_LEFT;
                case 77:
                    return KEY_ACTION_RIGHT;
                case 72:
                    return KEY_ACTION_UP;
                case 80:
                    return KEY_ACTION_DOWN;
                default:
                    return KEY_ACTION_NONE;
            }
        }

        // All other keys are ignored. This prevents an accidental letter or
        // number key from changing the hardware state.
        return KEY_ACTION_NONE;
    }

    return KEY_ACTION_STOP_REQUESTED;
}

void printControls()
{
    std::cout << std::endl
              << "Keyboard controls:" << std::endl
              << "  LEFT / RIGHT : previous or next channel" << std::endl
              << "  UP / DOWN    : increase or decrease current" << std::endl
              << "  ESC          : safe shutdown and exit" << std::endl
              << "  Ctrl+C       : backup safe shutdown" << std::endl
              << std::endl;
}

void printCurrentState(int channel, int microamps)
{
    std::cout << "Active state -> Channel " << channel
              << " | Current " << microamps << " uA"
              << std::endl;
}

// Return both instruments to a safe state. Current is removed first, the relays
// are released second, and only then are the hardware sessions closed.
void shutdownSystem(IVTEXFgenPtr fgen)
{
    if (gSerialPort != INVALID_HANDLE_VALUE)
        placeCurrentCalibratorInStandby();

    if (fgen != NULL)
    {
        allRelaysOff(fgen);
        Sleep(200);

        try
        {
            fgen->Close();
        }
        catch (const _com_error& error)
        {
            printComError("Close relay Fgen", error);
        }
    }

    if (gSerialPort != INVALID_HANDLE_VALUE)
    {
        sendCalibratorCommand("LOCAL");
        Sleep(200);
        CloseHandle(gSerialPort);
        gSerialPort = INVALID_HANDLE_VALUE;
    }
}

int main()
{
    signal(SIGINT, handleCtrlC);

    HRESULT comResult = CoInitialize(NULL);
    if (FAILED(comResult))
    {
        std::cerr << "Unable to initialize COM." << std::endl;
        return 1;
    }

    IVTEXFgenPtr relayFgen;
    bool testFailed = false;
    bool escapePressed = false;
    int selectedChannel = INITIAL_TEST_CHANNEL;
    int selectedCurrentUa = INITIAL_CURRENT_UA;

    if (!openCurrentCalibrator())
    {
        shutdownSystem(relayFgen);
        CoUninitialize();
        return 1;
    }

    if (!initializeRelayFgen(relayFgen))
    {
        shutdownSystem(relayFgen);
        CoUninitialize();
        return 1;
    }

    std::cout << std::endl
              << "Keyboard-controlled in-core analog test starting."
              << std::endl;
    printControls();

    // Start in the safest, most predictable state: channel 1 at 1 uA. The same
    // channel-change function used by the arrow keys is used here so startup and
    // later switching follow exactly the same relay and current sequence.
    if (!changeSelectedChannel(
            relayFgen,
            selectedChannel,
            selectedCurrentUa))
    {
        std::cerr << "Failed to establish the initial test state."
                  << std::endl;
        testFailed = true;
    }
    else
    {
        printCurrentState(selectedChannel, selectedCurrentUa);
    }

    while (!testFailed && !escapePressed && !gStopRequested)
    {
        const KeyAction action = readKeyAction();

        if (action == KEY_ACTION_ESCAPE)
        {
            escapePressed = true;
            break;
        }

        if (action == KEY_ACTION_STOP_REQUESTED)
            break;

        if (action == KEY_ACTION_RIGHT || action == KEY_ACTION_LEFT)
        {
            int requestedChannel = selectedChannel;

            if (action == KEY_ACTION_RIGHT)
            {
                requestedChannel = incrementWithRollover(
                    selectedChannel,
                    1,
                    NUMBER_OF_TEST_CHANNELS);
            }
            else
            {
                requestedChannel = decrementWithRollover(
                    selectedChannel,
                    1,
                    NUMBER_OF_TEST_CHANNELS);
            }

            if (!changeSelectedChannel(
                    relayFgen,
                    requestedChannel,
                    selectedCurrentUa))
            {
                std::cerr << "Failed to switch to channel "
                          << requestedChannel << "." << std::endl;
                testFailed = true;
                break;
            }

            selectedChannel = requestedChannel;
            printCurrentState(selectedChannel, selectedCurrentUa);
            continue;
        }

        if (action == KEY_ACTION_UP || action == KEY_ACTION_DOWN)
        {
            int requestedCurrent = selectedCurrentUa;

            if (action == KEY_ACTION_UP)
            {
                requestedCurrent = incrementWithRollover(
                    selectedCurrentUa,
                    MIN_CURRENT_UA,
                    MAX_CURRENT_UA);
            }
            else
            {
                requestedCurrent = decrementWithRollover(
                    selectedCurrentUa,
                    MIN_CURRENT_UA,
                    MAX_CURRENT_UA);
            }

            if (!changeCurrentOutput(requestedCurrent))
            {
                std::cerr << "Failed to change current to "
                          << requestedCurrent << " uA." << std::endl;
                testFailed = true;
                break;
            }

            selectedCurrentUa = requestedCurrent;
            printCurrentState(selectedChannel, selectedCurrentUa);
        }
    }

    // ESC, Ctrl+C, normal failure, and startup failure all use this one shutdown
    // path so there is no exit that intentionally leaves current or relay power on.
    shutdownSystem(relayFgen);
    CoUninitialize();

    if (gStopRequested)
    {
        std::cout << "Test stopped with Ctrl+C. Outputs were shut down."
                  << std::endl;
        return 2;
    }

    if (testFailed)
    {
        std::cerr << "Test ended because of an error. Outputs were shut down."
                  << std::endl;
        return 1;
    }

    std::cout << "ESC pressed. All relay outputs are off and the Martel is local."
              << std::endl;
    return 0;
}
