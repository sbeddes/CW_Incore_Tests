// This is the main project file for VC++ application project 
// generated using an Application Wizard.

/* Force time functions to be 32 bits for now */
#define _USE_32BIT_TIME_T

/* Disbale deprication of the unsecure CRT functions */
#define _CRT_SECURE_NO_DEPRECATE

/* Disable deprication of the Non STD C functions */
#define _CRT_NONSTDC_NO_DEPRECATE 


#include <rtime.h>


// R*TIME Include Files
#include <erf_ipc.h>
#include <vdb.h>
#include <stdio.h>
#include <process.h>
//#include "cbw.h"

#include <system.h>
#include <ptsinuse.h>
#include <messdef.h>
#include <mmidata.h>


#include "stdafx.h"
#include <conio.h>
#using <mscorlib.dll>
#import "IviDriverTypeLib.dll" no_namespace
#import "IviSessionFactory.dll" no_namespace
#import "VTEXFgen_64.dll" no_namespace
#import "VTEXDmm_64.dll" no_namespace

using namespace System;

#define SLOT_NAME_LEN 4

// Global Variables
const char		*gcszProcessName = "InstaCalSet";								// the program name
UINT32			gunTimeVID;														// the vid of SDZTIME
int				gnServerType;													// Server type
int				gnServerMode;													// Current server mode
int				gnDebugLevel;													/* Dynamic debugging level */
int				gnNumPoints;													// Number of test points
int				gnTestCards;													// Number of test cards
int				gnNumChannelsPerCard;											// Number of Channels per card
char			gszMessage[256];												/* Output message buffer */
wchar_t			gszMessageW[256];												/* Output message buffer */
char			gszService[256];												// Service Name
wchar_t			gszServiceW[256];												// Service Name
char			gszIpAddress[256];												// IpAddress
char			(*gszTestPointNames)[DB_PNT_NAME_LEN+1];						// Array of test point names - retrieved from config file
char			(*gszCardSlotNames)[SLOT_NAME_LEN+1];							// Array of card slot names - retrieved from config file
char			*gszIniName = "DCOffset.ini";									// name of config file
long			*gVids;															// VIDs for test points
float			outputvalue[8] = {0,0,0,0,0,0,0,0};								// last value from the output
float			expectedvalue[8] = {0.0001,0,0,0,0,0,0,0};						// current value from the network
float			lastexpectedvalue[8];											// last value from the network

int nGetINIInformation();
void PrintUsage();


int _tmain()
{
	HWND hWndw;
	HRESULT hr;
	int status;
	int card;
	int i;
	int loop;
	float DMM_read[8];
	char IpAddressCmd[64];
	char SlotCmd[64];
	char channelID[20];
	_bstr_t chan = "CH1";


	status = AllocConsole();
	Sleep(100);

	sprintf(gszMessage, "%s %d", gcszProcessName, getpid());
	status = SetConsoleTitleA(gszMessage);

	// Initialize the MMI Data structures
	if (!InitMmiData()) {
		printf("Unable to initialize the Viewer data access DLLs\n");
		return 0;
	}
	// connect to the points-in-use list
	if (ConnectToList() != PTSINUSE_OK) {
		return 0;
	}

	// Retrieve configuration information from the ini file
	if (nGetINIInformation() != EXIT_SUCCESS)
		return 0;

	i = 0;
	while(i < 20)
	{
		Sleep(1000);
		hWndw = FindWindowA(NULL, gszMessage);

		if(hWndw != NULL)
		{
			ShowWindow(hWndw, SW_NORMAL);
			i = 22;
		}
		else
			++i;

	}

    ::CoInitialize(NULL); //Start the COM layer
	/*We want to instantiate a pointer to the driver in a try/catch block so that we fail
	properly if the driver is not found in the COM registry.*/
	
	IVTEXFgenPtr fgen_slot[16];

	try {

		for(card = 0; card < gnTestCards; ++card) {	

			sprintf(IpAddressCmd, "TCPIP::%s::INSTR", gszIpAddress);

			sprintf(SlotCmd, "DriverSetup= Slots= (%s=Fgen)", gszCardSlotNames[card]);

			IIviDriverPtr fgen(__uuidof(VTEXFgen));


			/*We want to do the Initialization in a try/catch block so that our test code
			doesn't run if we fail to initialize.*/
			try {
				//sprintf(IpAddressCmd, "TCPIP::%s::INSTR", gszIpAddress);

				//sprintf(SlotCmd, "DriverSetup= Slots= (%s=Fgen)", gszCardSlotNames[card]);

				printf("IpAddressCmd: %s\n", IpAddressCmd);
				printf("Slot Cmd: %s\n", SlotCmd);

				/*We chose to give the driver an empty options string. You may want to 
				give your driver options - check the manual to see the available settings.
				Note that we also set the Reset bit so that we get a clean start to work 
				from.*/

				fgen->Initialize(IpAddressCmd,VARIANT_TRUE,VARIANT_TRUE, "");

				//fgen->Initialize(IpAddressCmd,VARIANT_TRUE,VARIANT_TRUE, SlotCmd);

				fgen_slot[card] = fgen;
				printf("OK card: %d\n", card);

				printf("\nVoltage output on Channels.\n");
				printf("Hit any key to end\n");
			}
			
			catch(_com_error &e) {
				::MessageBox(NULL, e.Description(), e.ErrorMessage(), MB_ICONERROR);
				printf("ERROR card: %d\n", card);
				Sleep(3000);
				exit(0);
			}
		}

		for (card = 0; card < gnTestCards; ++card) {
			
			for(i = 0; i < gnNumChannelsPerCard; ++i) {

				_bstr_t chan = "CH1";
				char channelID[20];

				sprintf(channelID, "CH%1d", i+1);
				chan = channelID;

				fgen_slot[card]->Output->DriveMode[chan] = VTEXFgenDriveModeVoltage;
			}
		}
		try {

			while (!_kbhit()) {

				for(card = 0; card < gnTestCards; ++card) {	

					loop = 0;

					//loop through all channels for a card
					for(i = 0; i < gnNumChannelsPerCard; ++i) {

						if (FetchValue(1, &gVids[card+i], &expectedvalue[i]) == EXIT_SUCCESS) {
							
						}
						else {
							printf("\n\nUnable to connect to network");
							Sleep(5000);
							return 0;
						}

						if(lastexpectedvalue[i] != expectedvalue[i]) {
							lastexpectedvalue[i] = expectedvalue[i];
							outputvalue[i] = expectedvalue[i];
							loop = 1;
						}
					}

					if (loop == 0) {

						for(i = 0; i < 8; i++) {

							float diff = ((expectedvalue[i] - DMM_read[i])/expectedvalue[i]);

							if(abs(diff) > 0.00001){
								outputvalue[i] = outputvalue[i] * (diff + 1);
							}
						}
				
					}
						
						
						
					for(i = 0; i < gnNumChannelsPerCard; ++i) {
	
						sprintf(channelID, "CH%1d", i+1);
						chan = channelID;
						// Set gain
						if (fabs(outputvalue[i]) < 1.0f)
							fgen_slot[card]->Output->Range[chan] = 1.0;
						else if (fabs(outputvalue[i]) < 2.0f)
							fgen_slot[card]->Output->Range[chan] = 2.0;
						else if (fabs(outputvalue[i]) < 5.0f)
							fgen_slot[card]->Output->Range[chan] = 5.0;
						else if (fabs(outputvalue[i]) < 10.0f)
							fgen_slot[card]->Output->Range[chan] = 10.0;
						else if (fabs(outputvalue[i]) < 20.0f)
							fgen_slot[card]->Output->Range[chan] = 20.0;

						//Set the output mode of the channels
						fgen_slot[card]->Output->ChannelOutputMode[chan] = VTEXFgenOutputModeFunction;

						//Setup to output DC waveform
						fgen_slot[card]->StandardWaveform->Waveform[chan] = VTEXFgenWaveformDC;

						//Set the DC offset of the channels
						fgen_slot[card]->StandardWaveform->DCOffset[chan] = outputvalue[i];

						//To physically produce the signal, you need to enable the channels
						fgen_slot[card]->Output->Enabled[chan] = VARIANT_TRUE;

						printf("\nSlot %s Channel %s Set Voltage: %f Enter DMM Read: ", gszCardSlotNames[card], channelID, expectedvalue[i]);
						std::cin >> DMM_read[i];

					}
					Sleep(500);
					system("cls");
					printf("IpAddressCmd: %s\n", IpAddressCmd);
					printf("Slot Cmd: %s\n", SlotCmd);
					printf("OK card: %d\n", card);
					printf("\nVoltage output on Channels.\n");
					printf("Hit any key to end.\n");
				}
			}
		}
			catch(_com_error &e) {
				::MessageBox(NULL, e.Description(), e.ErrorMessage(), MB_ICONERROR);
			}

			//Close the initialized session
			for(card = 0; card < gnTestCards; ++card) {
				fgen_slot[card]->Close();
	
			}
		}
		catch (_com_error& e) {
			::MessageBox(NULL, e.Description(), e.ErrorMessage(), MB_ICONERROR);
		}

	::CoUninitialize();

	printf("\nDone - Press Enter to Exit\n");
	getchar();

	return 0;
}


int nGetINIInformation(void)
{
	char *szSection;
	char *szKey;
	char szTemp[256];
	char szTempKey[80];
	char szIniPath[256];
	wchar_t szPointNameW[DB_PNT_NAME_LEN+1];
	short type = 0;
	int istat;
	int channel_num;
	int card_num;
	int old_card;

	sprintf(szIniPath, "%s\\data\\%s", getenv("RTIMEWHOME"), gszIniName);

	// Retrieve Service name
	szSection = (char *)"Config";
	szKey = (char *)"ServiceName";
	if (!GetPrivateProfileStringA(szSection, szKey, NULL, szTemp, sizeof(szTemp), szIniPath)) {
		printf("Unable to retrieve Service Name from configuration file\n");
		exit(0);
	}
	if (strlen(szTemp) <= 0) {
		printf("Service name not defined in configuration file\n");
		exit(0);
	}
	strcpy(gszService, szTemp);
	swprintf(gszServiceW, sizeof(gszServiceW), L"%S", gszService);

	szKey = (char *)"IpAddress";
	if (!GetPrivateProfileStringA(szSection, szKey, NULL, szTemp, sizeof(szTemp), szIniPath)) {
		printf("Unable to retrieve IpAddress from configuration file\n");
		exit(0);
	}
	strcpy(gszIpAddress, szTemp);

	// Number of test cards
	szKey = (char *)"NumberOfTestCards";
	gnTestCards = GetPrivateProfileIntA(szSection, szKey, 0, szIniPath);

	// Allocate space for the card slot names here
	gszCardSlotNames = (char (*)[SLOT_NAME_LEN+1])malloc((SLOT_NAME_LEN+1) * gnTestCards);

	// Retrieve the number of ChannelsPerCard, based on the DatabaseName
	szKey = (char *)"ChannelsPerCard";
	gnNumChannelsPerCard = GetPrivateProfileIntA(szSection, szKey, 0, szIniPath);

	gnNumPoints = gnTestCards * gnNumChannelsPerCard;
	szSection = (char *)gszService;

	// Allocate space for the test point names here
	gszTestPointNames = (char (*)[DB_PNT_NAME_LEN+1])malloc((DB_PNT_NAME_LEN+1) * gnNumPoints);
	gVids = new long[gnNumPoints];

	// Test Points - retrieved from the <servicename> section
	old_card = -1;
	card_num = 0;
	channel_num = 0;
	for(int i=0; i<gnNumPoints; i++)
	{
		if (old_card != card_num)
		{
			sprintf(szTempKey, "TestCrd%2.2dSlot", card_num+1);
			szKey = szTempKey;
			if (!GetPrivateProfileStringA(szSection, szKey, NULL, szTemp, sizeof(szTemp), szIniPath)) {
				printf("Unable to retrieve parameter: %s\n", szKey, " from section ", szSection);
				exit(0);
			}
			strcpy(&gszCardSlotNames[card_num][0], szTemp);
			old_card = card_num;
		}

		sprintf(szTempKey, "TestCrd%2.2dCh%2.2d", card_num+1, channel_num);
		szKey = szTempKey;
		if (!GetPrivateProfileStringA(szSection, szKey, NULL, szTemp, sizeof(szTemp), szIniPath)) {
			printf("Unable to retrieve parameter: %s\n", szKey, " from section ", szSection);
			exit(0);
		}
		strcpy(&gszTestPointNames[i][0], szTemp);
		// and convert to a VID
		type = 0;
		swprintf(szPointNameW, sizeof(szPointNameW), L"%S", gszTestPointNames[i]);
		if ((ConvertName(gszServiceW, 1, szPointNameW, &type, &gVids[i], &istat) != EXIT_SUCCESS) ||
			(istat != NAME_CONVERTED)) {
			gVids[i] = INVPOINTID;
		} else
			AddToList(gVids[i]);

		channel_num++;
		if (channel_num >= gnNumChannelsPerCard)
		{
			channel_num = 0;
			card_num++;
		}
	}

	return EXIT_SUCCESS;
}

void PrintUsage() {
	printf("\nUsage:  SetPoints [ ?]\n\n");
	printf("          ? displays this help information\n");
}