// In-Core_Analog_tst.cpp : Defines the entry point for the console application.
//
//#include "stdafx.h"
#include <Windows.h>
#include <iostream>
#include <string>
#include <signal.h>

// com port of calibrator !!! USE NULL MODEM !!!
const char* PORT = "\\\\.\\COM6";

const int START_uA = 1;
const int END_uA = 4;
const int DELAY_SECONDS = 15;

volatile sig_atomic_t stopRequested = 0;
HANDLE serialPort = INVALID_HANDLE_VALUE;

void handleCtrlC(int){
	stopRequested = 1;
}


// Function to send command via serial
bool sendcommand(const std::string& command){
	std::string message = command + "\r";
	DWORD bytesWritten = 0;

	// for debugging purposes, can enable if wanted
	//std::cout << "Command: " << command << std::endl;

	return WriteFile(
		serialPort,
		message.c_str(),
		static_cast<DWORD>(message.size()),
		&bytesWritten,
		NULL
		) != 0;

}


// function to read a responce if desired, mostly used for debugging
std::string readResponce(){

	std::string responce;
	char character;
	DWORD bytesRead;
	DWORD startTime = GetTickCount();

	// wait 3 sec for a return
	while(GetTickCount() - startTime < 3000){
		bytesRead = 0;
		if(!ReadFile(
			serialPort,
			&character,
			1,
			&bytesRead,
			NULL)){
				return "READ ERROR";
		}

		if(bytesRead == 0){
			Sleep(10);
			continue;
		}

		// documentation states comunication ends in \r
		if(character == '\r')
			break;

		if(character != '\n')
			responce += character;
	}
	return responce;
}

bool waitSeconds(int seconds){
	for(int i = 0; i < seconds * 10; i++){
		if(stopRequested)
			return false;
		Sleep(100);
	}
	return true;
}


// help function to quickly shut down output
void shutdownOutput(){
	if(serialPort != INVALID_HANDLE_VALUE){
		std::cout << "Placing device in standby" << std::endl;

		// commented out via request from carson bush, would normally want this...
		//sendcommand("STBY");
		Sleep(200);

		sendcommand("LOCAL");
		Sleep(200);
	}
} 


// configure the serial port, these are pretty standard but may have to google for more
// info. PS I had to google 
bool configureSerialPort(){
	DCB settings;
	COMMTIMEOUTS timeouts;

	ZeroMemory(&settings, sizeof(settings));
	settings.DCBlength = sizeof(settings);

	if(!GetCommState(serialPort,&settings))
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

	if(!SetCommState(serialPort, &settings))
		return false;

	ZeroMemory(&timeouts, sizeof(timeouts));
	timeouts.ReadIntervalTimeout = 50;
	timeouts.ReadTotalTimeoutConstant = 100;
	timeouts.WriteTotalTimeoutConstant = 1000;

	return SetCommTimeouts(serialPort, &timeouts) != 0;

}

int main(){

	// ctrl c handling for manual shutdown
	signal(SIGINT,handleCtrlC);

	// create the serial port
	serialPort =  CreateFileA(
		PORT,
		GENERIC_READ | GENERIC_WRITE,
		0,
		NULL,
		OPEN_EXISTING,
		0,
		NULL
		);

		// error handling if serial port is bad
		if(serialPort == INVALID_HANDLE_VALUE){
			std::cerr << " Could not open serial port " << std::endl;
			return 1;
		}

		if(!configureSerialPort()){
			std::cerr << " Could not configure serial port" << std::endl;
			CloseHandle(serialPort);
			return 1;
		}

		// clear rx and tx caches
		PurgeComm(
			serialPort,
			PURGE_RXCLEAR | PURGE_TXCLEAR
			);

		// get calibrator info and wait for user to return to rtime monitor
		std::cout << "Sending Info request" << std::endl;
		Sleep(5000);
		sendcommand("*IDN?");
		std::string responce = readResponce();

		if(responce.empty()){
			std::cerr << "Failed to receive a responce" << std::endl;
			Sleep(500);
			CloseHandle(serialPort);
			serialPort = INVALID_HANDLE_VALUE;
			return 0;
		}
		else{ 
			std::cout << responce << std::endl;
			Sleep(5000);
		}

		// ENTER remote and STANDBY
		sendcommand("REMOTE");
		// commented out at request of carson bush
		//sendcommand("STBY");
		
		// set 1 uamp
		sendcommand("OUT 1 uA");
		sendcommand("OPER");

		std::cout << "Output enabled at 0.001 mA" << std::endl;
		
		// loop through remaining amperages with 10 seconds between
		for (int microamps = START_uA + 1; microamps <= END_uA; microamps++){
			if(!waitSeconds(DELAY_SECONDS))
				break;
			
			// build command string [ OUT NUMAMP UNIT ]
			std::string command = "OUT " + std::string(1,static_cast<char>('0' + microamps)) + " uA";

			// if command fails exit
			if(!sendcommand(command)){
				std::cerr << "Failed to send command" << std::endl;
				break;
			}

			std::cout << "Output changed to 0.00" << microamps << " mA" << std::endl;
		}

		if(!stopRequested)
			waitSeconds(DELAY_SECONDS);

		// shutdown after finish
		shutdownOutput();

		// close the serial handler
		CloseHandle(serialPort);
		serialPort = INVALID_HANDLE_VALUE;

		std::cout << " Test Complete " << std::endl;

		return 0;

}


		
