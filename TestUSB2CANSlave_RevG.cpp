// TestUSB2CANSlave_RevG.cpp : This file contains the 'main' function. Program execution begins here.
//

// TestUSB2CANMaster_RevG.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <windows.h>
#include <winreg.h>
#include <iostream>
#include <stdio.h>
#include <tchar.h>
#include "PECanConverterProtocol.h"

#define MAX_CAN_MESSAGE_LENGTH	20

HANDLE h_Serial;

LONG GetStringRegKey(HKEY hKey, const std::wstring& strValueName, std::wstring& strValue, const std::wstring& strDefaultValue)
{
    strValue = strDefaultValue;
    WCHAR szBuffer[512];
    DWORD dwBufferSize = sizeof(szBuffer);
    ULONG nError;
    nError = RegQueryValueExW(hKey, strValueName.c_str(), 0, NULL, (LPBYTE)szBuffer, &dwBufferSize);
    if (ERROR_SUCCESS == nError)
    {
        strValue = szBuffer;
    }
    return nError;
}

bool connectToCOMM(void)
{
    bool COMMstat = true;

    std::cout << "Waiting for Windows to recognize the CAN Converter\n";

    HKEY hKey;
    LONG lRes = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DEVICEMAP\\SERIALCOMM", 0, KEY_READ, &hKey);

    std::wstring strValueOfComPort;

    GetStringRegKey(hKey, L"\\Device\\USBSER000", strValueOfComPort, L"bad");

    h_Serial = CreateFile(strValueOfComPort.c_str(), GENERIC_READ | GENERIC_WRITE,
        0,
        0,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        0);

    if (h_Serial == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND) {
            //serial port not found. Handle error here.
            std::cout << "Serial port not found\n";
        }
        //any other error. Handle error here.
        //std::cout << "Invalid Handle!\n";
        return NULL;
    }

    DCB dcbSerialParam = { 0 };
    dcbSerialParam.DCBlength = sizeof(dcbSerialParam);

    if (!GetCommState(h_Serial, &dcbSerialParam)) {
        //handle error here
        std::cout << "Unable to get Comm state\n";
        return NULL;
    }

    dcbSerialParam.BaudRate = CBR_115200;
    dcbSerialParam.ByteSize = 8;
    dcbSerialParam.StopBits = ONESTOPBIT;
    dcbSerialParam.Parity = NOPARITY;

    if (!SetCommState(h_Serial, &dcbSerialParam)) {
        //handle error here
        std::cout << "Unable to set Comm state\n";
        return NULL;
    }

    COMMTIMEOUTS timeout = { 0 };
    timeout.ReadIntervalTimeout = 60;
    timeout.ReadTotalTimeoutConstant = 500;
    timeout.ReadTotalTimeoutMultiplier = 15;
    timeout.WriteTotalTimeoutConstant = 60;
    timeout.WriteTotalTimeoutMultiplier = 8;
    if (!SetCommTimeouts(h_Serial, &timeout)) {
        //handle error here
        std::cout << "Unable to set Comm timeouts\n";
        return NULL;
    }

    return COMMstat;
}

bool WriteCOMMport(uint8_t* buffer, uint8_t writeSize)
{
    bool writeStatus = true;
    DWORD dwRead = 0;

    if (!WriteFile(h_Serial, buffer, writeSize, &dwRead, NULL)) {
        //handle error here
        std::cout << "Unable to write to the Comm port\n";
        CloseHandle(h_Serial); // free the COM port
        writeStatus = false;
    }

    return writeStatus;
}

bool ReadCOMMport(uint8_t* buffer, uint8_t readSize)
{
    bool readStatus = true;
    DWORD dwRead = 0;

    if (!ReadFile(h_Serial, buffer, readSize, &dwRead, NULL)) {
        //handle error here
        std::cout << "Unable to read from the Comm port\n";
        CloseHandle(h_Serial); // free the COM port
        readStatus = false;
    }

    return readStatus;
}

void ProtocolSendAck(void)
{
    static uint8_t writeBuffer[3] = { 0xEF,0x04,0xBE };
    DWORD dwRead = 0;

    if (!WriteFile(h_Serial, writeBuffer, 3, &dwRead, NULL)) {
        //handle error here
        std::cout << "Unable to write to the Comm port\n";
        CloseHandle(h_Serial); // free the COM port
    }
}

int main()
{
    //uint8_t data[8];
    //uint8_t numberOfBytes;
    uint8_t sizeofUSBbuf = 0;
    uint8_t canMessage[MAX_CAN_MESSAGE_LENGTH] = { 0xEF };
    uint8_t readBuff[MAX_CAN_MESSAGE_LENGTH] = {};
    uint16_t crcCalculate = 0;
    uint8_t transmissionErrCntr = 0;
    uint8_t dataLength = 0;

    if (connectToCOMM())
    {
        std::cout << "Calling ProtocolInitialize()\n";
        canMessage[0] = STX;
        canMessage[1] = PACKET_TYPE_PING;
        canMessage[2] = ETX;

        if (!WriteCOMMport(canMessage, 3))
        {
            while (1); // the error message is displayed in the write function call so no need here
        }

        std::cout << "Waiting for the CAN Converter to respond\n";

        Sleep(100);

        /*if (ReadCOMMport(readBuff, 3))
        {
            if (readBuff[0] == STX && readBuff[1] == PACKET_TYPE_PING_ACK && readBuff[2] == ETX)
            {
                std::cout << "CAN Converter device responding, waiting for CAN messages\n";
            }
            else
            {
                std::cout << "message other than PING ACK received, waiting for PING ACK\n";
                while (1);
            }
        }
        else
        {
            while (1); // U2C board did not respond with PING ACK
        }*/

        ZeroMemory(readBuff, sizeof(readBuff)); // reset the read buffer;
        while (true)
        {
            if (ReadCOMMport(readBuff, sizeof(readBuff)))
            {
                if (readBuff[1] == 0)
                    continue;

                switch (readBuff[1])
                {
                case PACKET_TYPE_PING_ACK:
                    std::cout << "CAN Converter device responding, waiting for CAN messages\n";
                    break;

                case PACKET_TYPE_PING:
                    break;

                case PACKET_TYPE_ERROR_MESSAGE:
                    std::cout << "Transmission error\n";
                    break;

                case PACKET_TYPE_CAN_MESSAGE:
                    sizeofUSBbuf = 4 + ProtocolCANUnpackDataLength(readBuff + 2); //+4 for
                    crcCalculate = ProtocolCalculateCRC((uint8_t*)(readBuff + 2), 2 + ProtocolCANUnpackDataLength(readBuff + 2));
                    if (((uint16_t)readBuff[sizeofUSBbuf + 1] << 8U | readBuff[sizeofUSBbuf]) != crcCalculate) // CRC error
                    {
                        std::cout << "CRC error\n";
                        break;
                    }
                    ProtocolSendAck(); //Acknowledge to USB2CAN that the sensor data is received
                    std::cout << "ID: " << ProtocolCANUnpackID(readBuff + 2);
                    //printf("ID: %3x ", ProtocolCANUnpackID(readBuff + 2));
                    dataLength = ProtocolCANUnpackDataLength(readBuff + 2);
                    for (int i = 0; i < dataLength; i++)
                    {
                        //std::cout << " " << readBuff[4 + i]; // +0x30 converts the number to char to display the numbers
                        printf("%2x ", readBuff[4 + i]);
                    }
                    std::cout << "\n";
                    break;

                default:
                    std::cout << "Non-protocol (WinPREP) standard CAN message\n";
                    break;
                }

                readBuff[1] = 0;
            }
        }
    }
    else
    {
        while (1);
    }


    return 0;
}

// Run program: Ctrl + F5 or Debug > Start Without Debugging menu
// Debug program: F5 or Debug > Start Debugging menu

// Tips for Getting Started: 
//   1. Use the Solution Explorer window to add/manage files
//   2. Use the Team Explorer window to connect to source control
//   3. Use the Output window to see build output and other messages
//   4. Use the Error List window to view errors
//   5. Go to Project > Add New Item to create new code files, or Project > Add Existing Item to add existing code files to the project
//   6. In the future, to open this project again, go to File > Open > Project and select the .sln file
