// Automated_In-Core_Analog_tst.cpp
//
// Purpose:
// This program automates the existing in-core analog current test across all
// eight analog channels. The Martel 3001 supplies the 1-4 uA test current, and
// the VTI Fgen card supplies 24 V to the relay pair for the selected channel.
//
// Relay setup:
// Each analog channel has one HIGH relay and one LOW/ground relay. The two relay
// coils for a channel are jumpered together, so one Fgen output switches both
// relays at the same time. Fgen CH1 selects analog channel 1, CH2 selects analog
// channel 2, and this continues through CH8.
//
// Test sequence:
// 1. Place the Martel in STBY so its current output is inactive.
// 2. Turn off all Fgen relay outputs.
// 3. Apply 24 V to the relay pair for the channel being tested.
// 4. Step the Martel through 1, 2, 3, and 4 uA.
// 5. Hold each current for 30 seconds so the voltage can be read externally.
// 6. Return the Martel to STBY before opening the relays or changing channels.
//
// The shutdown path follows the same safe order whether the test finishes,
// encounters an error, or is stopped with Ctrl+C.
//
// Visual Studio 2010 compatible.

#include <Windows.h>
#include <signal.h>
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

// The hardware addresses, current range, relay voltage, and timing values are
// kept in config.h so they can be changed without rewriting the test sequence.
#include "config.h"

// The Ctrl+C handler only sets this flag. The normal program flow notices the
// flag and performs the full shutdown instead of trying to control hardware
// directly from inside the signal handler.
volatile sig_atomic_t gStopRequested = 0;

// The Martel serial handle is global because the send, read, standby, and
// shutdown functions all need to use the same open connection.
HANDLE gSerialPort = INVALID_HANDLE_VALUE;

// Ctrl+C requests a safe stop. The current wait or loop ends, then shutdownSystem
// places the Martel in STBY and turns off every relay output.
void handleCtrlC(int)
{
    gStopRequested = 1;
}

// Send one command to the Martel calibrator.
//
// The Martel expects a carriage return at the end of every command, so it is
// added here instead of requiring every caller to remember it. The function
// returns true only when Windows reports that the entire command was written.
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

// Read one response from the Martel until a carriage return is received or the
// allowed time expires. Reading one character at a time keeps the response
// handling simple and matches the short text replies sent by the calibrator.
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

        // A read can return without providing a character because of the serial
        // timeout settings. Wait briefly and continue until the full timeout.
        if (bytesRead == 0)
        {
            Sleep(10);
            continue;
        }

        // The Martel terminates its response with a carriage return. Line feeds
        // are ignored so they do not become part of the returned identity text.
        if (character == '\r')
            break;

        if (character != '\n')
            response += character;
    }

    return response;
}

// Wait for a requested number of milliseconds while checking for Ctrl+C every
// 100 ms. A single long Sleep would make the program appear unresponsive during
// each 30-second current dwell.
bool waitWithAbort(int milliseconds)
{
    int elapsed = 0;

    while (elapsed < milliseconds)
    {
        if (gStopRequested)
            return false;

        const int remaining = milliseconds - elapsed;
        const int step = remaining < 100 ? remaining : 100;
        Sleep(step);
        elapsed += step;
    }

    return true;
}

// Convenience wrapper used when a delay is easier to define in seconds.
bool waitSeconds(int seconds)
{
    return waitWithAbort(seconds * 1000);
}

// Configure the serial settings used by the Martel 3001. These values preserve
// the settings from the original In-Core_Analog_tst program: 9600 baud, 8 data
// bits, no parity, one stop bit, and XON/XOFF software flow control.
bool configureSerialPort()
{
    DCB settings;
    COMMTIMEOUTS timeouts;

    ZeroMemory(&settings, sizeof(settings));
    settings.DCBlength = sizeof(settings);

    // Start with the current Windows port settings so fields that are not being
    // changed here keep valid values.
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

    // Keep individual reads short because readCalibratorResponse controls the
    // overall response timeout itself. The longer write timeout allows Windows
    // time to transmit the complete command through the USB serial adapter.
    ZeroMemory(&timeouts, sizeof(timeouts));
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 100;
    timeouts.WriteTotalTimeoutConstant = 1000;

    return SetCommTimeouts(gSerialPort, &timeouts) != 0;
}

// Open and verify the Martel current calibrator before any relay is energized.
// The connection is opened for both reading and writing because the program
// sends commands and reads the reply to *IDN?.
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

    // Clear any old characters left in the USB serial buffers. This prevents a
    // response from a previous run from being mistaken for the new *IDN? reply.
    PurgeComm(gSerialPort, PURGE_RXCLEAR | PURGE_TXCLEAR);

    // Confirm that a device is responding before the test is allowed to continue.
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

    // REMOTE allows the program to control the Martel through the serial port.
    if (!sendCalibratorCommand("REMOTE"))
        return false;

    Sleep(200);

    // STBY removes the active current output before the Fgen selects a channel.
    // It is treated as a high-impedance state for relay switching, but the relay
    // contacts are still used when channel-to-channel isolation is required.
    if (!sendCalibratorCommand("STBY"))
        return false;

    Sleep(200);
    return true;
}

// Convert an integer Fgen channel number into the channel name required by the
// VTI driver. For example, channel 1 becomes "CH1" and channel 8 becomes "CH8".
std::string fgenChannelName(int channelNumber)
{
    std::ostringstream stream;
    stream << "CH" << channelNumber;
    return stream.str();
}

// Print the operation that failed and the COM error code returned by the VTI
// driver. The error code is kept in hexadecimal because that is the most useful
// format when comparing it to IVI/VTI driver documentation.
void printComError(const char* operation, const _com_error& error)
{
    std::cerr << operation << " failed. HRESULT=0x"
              << std::hex << static_cast<unsigned long>(error.Error())
              << std::dec << std::endl;
}

// Turn off one Fgen relay-control output. The channel is disabled first, then
// its stored DC offset is returned to zero so it is safe for the next selection.
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

// Make a best effort to disable every relay channel. The loop continues even
// if one output reports an error so the program still attempts to shut down the
// other seven outputs. The return value records whether every channel succeeded.
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

// Configure all eight Fgen outputs once at startup. The outputs are set for DC
// voltage operation but remain disabled and at zero volts until a test channel
// is selected. This prevents a relay from energizing during initialization.
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

            // Disable the output before changing any of its operating settings.
            fgen->Output->Enabled[channel] = VARIANT_FALSE;

            // The relay circuit needs a DC voltage, not a generated AC waveform.
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

    // End initialization by explicitly commanding every relay output off.
    return allRelaysOff(fgen);
}

// Connect to the VTI chassis and create the Fgen driver session. The two TRUE
// values request an identity query and a device reset so the session starts from
// a known condition before the relay outputs are configured.
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

        // Save the generic IVI driver as the Fgen-specific pointer used by the
        // remaining relay-control functions.
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

// Select one analog channel by applying 24 V to its Fgen output. Since the HIGH
// and LOW relay coils are jumpered, enabling one Fgen channel closes both sides
// of the analog path at the same time.
bool selectRelayChannel(IVTEXFgenPtr fgen, int channelNumber)
{
    if (channelNumber < 1 || channelNumber > NUMBER_OF_TEST_CHANNELS)
        return false;

    // Use break-before-make switching. Every existing relay command is removed
    // and given time to release before the next channel receives 24 V.
    if (!allRelaysOff(fgen))
        return false;

    if (!waitWithAbort(RELAY_RELEASE_DELAY_MS))
        return false;

    try
    {
        const std::string name = fgenChannelName(channelNumber);
        _bstr_t channel(name.c_str());

        // Set the requested relay voltage before enabling the physical output.
        fgen->StandardWaveform->DCOffset[channel] = RELAY_VOLTAGE;
        fgen->Output->Enabled[channel] = VARIANT_TRUE;
    }
    catch (const _com_error& error)
    {
        printComError("Select relay channel", error);

        // If channel selection fails, remove every relay command before returning.
        allRelaysOff(fgen);
        return false;
    }

    // Wait for both relays to close and settle before current is applied.
    return waitWithAbort(RELAY_SETTLE_DELAY_MS);
}

// Build the Martel command used to set each current value. This uses a stream
// instead of std::to_string because the project must compile in Visual Studio 2010.
std::string currentCommand(int microamps)
{
    std::ostringstream stream;
    stream << "OUT " << microamps << " uA";
    return stream.str();
}

// Command the Martel to STBY and allow a short time for its output to become
// inactive. This function is called before every relay change and during shutdown.
bool placeCurrentCalibratorInStandby()
{
    if (gSerialPort == INVALID_HANDLE_VALUE)
        return false;

    const bool success = sendCalibratorCommand("STBY");
    Sleep(200);
    return success;
}

// Return both pieces of test equipment to a safe state. The order is important:
// remove the test current first, release the relays second, then close the hardware
// sessions. This prevents the current source from remaining active while contacts
// are opening or while the program is exiting.
void shutdownSystem(IVTEXFgenPtr fgen)
{
    // Remove the test current before releasing any relay contacts.
    if (gSerialPort != INVALID_HANDLE_VALUE)
        placeCurrentCalibratorInStandby();

    if (fgen != NULL)
    {
        // Try every channel even if one output reports an error.
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
        // LOCAL returns front-panel control to the operator before the Windows
        // serial handle is closed.
        sendCalibratorCommand("LOCAL");
        Sleep(200);
        CloseHandle(gSerialPort);
        gSerialPort = INVALID_HANDLE_VALUE;
    }
}

int main()
{
    // Register Ctrl+C before opening either device so an operator stop uses the
    // same controlled shutdown path as the rest of the program.
    signal(SIGINT, handleCtrlC);

    // The VTI driver is a COM component, so COM must be initialized before the
    // Fgen object is created.
    HRESULT comResult = CoInitialize(NULL);
    if (FAILED(comResult))
    {
        std::cerr << "Unable to initialize COM." << std::endl;
        return 1;
    }

    IVTEXFgenPtr relayFgen;
    bool testFailed = false;

    // Verify the current calibrator first. No relay is energized unless the
    // Martel connection is open, identified, in REMOTE, and in STBY.
    if (!openCurrentCalibrator())
    {
        shutdownSystem(relayFgen);
        CoUninitialize();
        return 1;
    }

    // Connect to the VTI chassis and prepare all eight relay-control outputs.
    if (!initializeRelayFgen(relayFgen))
    {
        shutdownSystem(relayFgen);
        CoUninitialize();
        return 1;
    }

    std::cout << std::endl
              << "Automated in-core analog test starting." << std::endl
              << "Eight channels, 1-4 uA, 30 seconds per current."
              << std::endl
              << "Press Ctrl+C to stop safely." << std::endl;

    // Test one complete analog channel at a time. A channel remains selected
    // while all four current values are applied, then both relays are opened
    // before the program advances to the next channel.
    for (int testChannel = 1;
         testChannel <= NUMBER_OF_TEST_CHANNELS && !gStopRequested;
         ++testChannel)
    {
        std::cout << std::endl
                  << "Selecting analog channel " << testChannel
                  << " relay pair." << std::endl;

        // The Martel must be in STBY before any relay output is changed. This
        // removes the active current while the old contacts open and the new
        // HIGH/LOW relay pair closes.
        if (!placeCurrentCalibratorInStandby())
        {
            std::cerr << "Failed to place current calibrator in standby."
                      << std::endl;
            testFailed = true;
            break;
        }

        if (!selectRelayChannel(relayFgen, testChannel))
        {
            // A Ctrl+C stop is expected and does not need to be reported as a
            // hardware failure. Any other selection failure ends the test.
            if (!gStopRequested)
            {
                std::cerr << "Failed to select relay channel "
                          << testChannel << "." << std::endl;
                testFailed = true;
            }
            break;
        }

        // Apply 1, 2, 3, and 4 uA while the selected channel's HIGH and LOW
        // relays remain energized.
        for (int microamps = START_CURRENT_UA;
             microamps <= END_CURRENT_UA && !gStopRequested;
             ++microamps)
        {
            const std::string command = currentCommand(microamps);

            // OUT selects the requested current value but does not place the
            // Martel output into operation by itself.
            if (!sendCalibratorCommand(command))
            {
                std::cerr << "Failed to set current to " << microamps
                          << " uA." << std::endl;
                testFailed = true;
                break;
            }

            // OPER connects and enables the current output at the value selected
            // by the OUT command above.
            if (!sendCalibratorCommand("OPER"))
            {
                std::cerr << "Failed to enable current output."
                          << std::endl;
                testFailed = true;
                break;
            }

            std::cout << "Channel " << testChannel
                      << ": " << microamps
                      << " uA applied for "
                      << CURRENT_DWELL_SECONDS
                      << " seconds." << std::endl;

            // The external voltage-reading process occurs during this 30-second
            // window. Automated voltage acquisition can be added at this point
            // later without changing the channel and current sequencing.
            if (!waitSeconds(CURRENT_DWELL_SECONDS))
                break;
        }

        // Open the Martel output before releasing this channel's relay pair.
        // This same order is used whether all four currents completed, an error
        // occurred, or the operator pressed Ctrl+C during the dwell.
        placeCurrentCalibratorInStandby();
        allRelaysOff(relayFgen);
        waitWithAbort(RELAY_RELEASE_DELAY_MS);

        if (testFailed)
            break;
    }

    // Always use the common shutdown routine so normal completion and failure
    // leave the equipment in the same known state.
    shutdownSystem(relayFgen);
    CoUninitialize();

    if (gStopRequested)
    {
        std::cout << "Test stopped by operator. Outputs were shut down."
                  << std::endl;
        return 2;
    }

    if (testFailed)
    {
        std::cerr << "Test ended because of an error. Outputs were shut down."
                  << std::endl;
        return 1;
    }

    std::cout << "Test complete. All relay outputs are off and the Martel is local."
              << std::endl;
    return 0;
}
