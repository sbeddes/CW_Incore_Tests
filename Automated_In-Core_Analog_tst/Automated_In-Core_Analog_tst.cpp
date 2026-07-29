// Automated_In-Core_Analog_tst.cpp
//
// Automates the existing in-core analog current test across eight channels.
// The Martel calibrator supplies 1-4 uA. The VTI Fgen card supplies 24 V to
// one relay-control output at a time. Each Fgen output is wired to the
// jumpered HIGH and LOW relay coils for its corresponding analog channel.
//
// Visual Studio 2010 compatible.

#include <Windows.h>
#include <signal.h>
#include <iostream>
#include <iomanip>
#include <string>
#include <sstream>
#include <comdef.h>

#import "IviDriverTypeLib.dll" no_namespace
#import "IviSessionFactory.dll" no_namespace
#import "VTEXFgen_64.dll" no_namespace

#include "config.h"

volatile sig_atomic_t gStopRequested = 0;
HANDLE gSerialPort = INVALID_HANDLE_VALUE;

void handleCtrlC(int)
{
    gStopRequested = 1;
}

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

bool waitSeconds(int seconds)
{
    return waitWithAbort(seconds * 1000);
}

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

    // STBY places the current output in its inactive/high-impedance state
    // before relay selection begins.
    if (!sendCalibratorCommand("STBY"))
        return false;

    Sleep(200);
    return true;
}

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

bool selectRelayChannel(IVTEXFgenPtr fgen, int channelNumber)
{
    if (channelNumber < 1 || channelNumber > NUMBER_OF_TEST_CHANNELS)
        return false;

    // Break-before-make. Remove every relay command before applying 24 V
    // to the next channel's jumpered HIGH/LOW relay-coil pair.
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

std::string currentCommand(int microamps)
{
    std::ostringstream stream;
    stream << "OUT " << microamps << " uA";
    return stream.str();
}

bool placeCurrentCalibratorInStandby()
{
    if (gSerialPort == INVALID_HANDLE_VALUE)
        return false;

    const bool success = sendCalibratorCommand("STBY");
    Sleep(200);
    return success;
}

void shutdownSystem(IVTEXFgenPtr fgen)
{
    // Remove test current before releasing or changing relay contacts.
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
              << "Automated in-core analog test starting." << std::endl
              << "Eight channels, 1-4 uA, 30 seconds per current."
              << std::endl
              << "Press Ctrl+C to stop safely." << std::endl;

    for (int testChannel = 1;
         testChannel <= NUMBER_OF_TEST_CHANNELS && !gStopRequested;
         ++testChannel)
    {
        std::cout << std::endl
                  << "Selecting analog channel " << testChannel
                  << " relay pair." << std::endl;

        // The Martel must be in STBY before any relay change.
        if (!placeCurrentCalibratorInStandby())
        {
            std::cerr << "Failed to place current calibrator in standby."
                      << std::endl;
            testFailed = true;
            break;
        }

        if (!selectRelayChannel(relayFgen, testChannel))
        {
            if (!gStopRequested)
            {
                std::cerr << "Failed to select relay channel "
                          << testChannel << "." << std::endl;
                testFailed = true;
            }
            break;
        }

        for (int microamps = START_CURRENT_UA;
             microamps <= END_CURRENT_UA && !gStopRequested;
             ++microamps)
        {
            const std::string command = currentCommand(microamps);

            if (!sendCalibratorCommand(command))
            {
                std::cerr << "Failed to set current to " << microamps
                          << " uA." << std::endl;
                testFailed = true;
                break;
            }

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

            // This dwell preserves the existing external voltage-read process.
            // Automated voltage acquisition can be inserted here later.
            if (!waitSeconds(CURRENT_DWELL_SECONDS))
                break;
        }

        // Open the Martel output before releasing this channel's relay pair.
        placeCurrentCalibratorInStandby();
        allRelaysOff(relayFgen);
        waitWithAbort(RELAY_RELEASE_DELAY_MS);

        if (testFailed)
            break;
    }

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
