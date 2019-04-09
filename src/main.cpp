/////////////////////////////////////////////////////////////////////// Includes
#include <charconv>
#include <iostream>
#include <fstream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <Windows.h>

/////////////////////////////////////////////////////////// Constant expressions
constexpr char SOH = 0x01;
constexpr char EOT = 0x04;
constexpr char ACK = 0x06;
constexpr char NAK = 0x15;
constexpr char C = 0x43;
constexpr char SUB = 26;

/////////////////////////////////////////////////// WinAPI COM control variables
HANDLE comPortHandle;
bool isCrcEnabled;
std::string filename;

//////////////////////////////////////////////////////////// COM port operations
void initialiseComPort(std::string const &comPortName)
{
    // Open COM port
    comPortHandle = CreateFile(comPortName.c_str(),
                               GENERIC_READ | GENERIC_WRITE,
                               0,
                               nullptr,
                               OPEN_EXISTING,
                               0,
                               nullptr);

    // Initialize communication parameters for a specified port
    constexpr int INPUT_BUFFER_SIZE = 1;
    constexpr int OUTPUT_BUFFER_SIZE = 128;
    constexpr int BYTE_SIZE = 8;
    DCB dcb;

    SetupComm(comPortHandle,
              INPUT_BUFFER_SIZE,
              OUTPUT_BUFFER_SIZE);

    GetCommState(comPortHandle, &dcb);

    dcb.BaudRate = CBR_9600;
    dcb.ByteSize = BYTE_SIZE;
    dcb.DCBlength = sizeof(dcb);
    dcb.fParity = FALSE;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fRtsControl = RTS_CONTROL_HANDSHAKE;
    dcb.fDtrControl = DTR_CONTROL_HANDSHAKE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fAbortOnError = TRUE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    dcb.fErrorChar = FALSE;
    dcb.fNull = FALSE;

    SetCommState(comPortHandle, &dcb);
}

void readDataOffComPort(char *const data, int const &length)
{
    DWORD position = 0;
    DWORD numberOfBytesRead;

    while (position < length)
    {
        ReadFile(comPortHandle,
                 data + position,
                 length - position,
                 &numberOfBytesRead,
                 nullptr);
        position += numberOfBytesRead;
    }
}

void writeDataToComPort(char const *const bytes, int const &length)
{
    DWORD numberOfBytesWritten;

    WriteFile(comPortHandle,
              bytes,
              length,
              &numberOfBytesWritten,
              nullptr);
}

short int calculateCrc16Checksum(char const *buffer)
{
    int x = 0;
    int value = 0x16532 << 15;

    for (int i = 0; i < 3; i++)
        x = 256 * x + (unsigned char) buffer[i];

    x *= 256;

    for (int i = 3; i < 134; i++)
    {
        if (i < 128)
            x += (unsigned char) buffer[i];

        for (int j = 0; j < 8; j++)
        {
            if (x & (1 << 31))
                x ^= value;
            x <<= 1;
        }
    }

    return x >> 16;
}

void receiveFile()
{
    char buffer[3], fileBuffer[128];
    unsigned short sum, sumc;

    // Send NAK
    buffer[0] = isCrcEnabled ? C : NAK;
    writeDataToComPort(buffer, 1);

    // Open file
    FILE *fileHandle = fopen(filename.c_str(),
                             "wb");

    // Perform receiver functions
    readDataOffComPort(buffer, 1);

    while (true)
    {
        readDataOffComPort(buffer + 1, 2);
        readDataOffComPort(fileBuffer, 128);

        sum = sumc = 0;
        readDataOffComPort((char *) &sum, isCrcEnabled ? 2 : 1);

        if (isCrcEnabled)
            sumc = calculateCrc16Checksum(fileBuffer);
        else
        {
            for (char i : fileBuffer)
                sumc += (unsigned char) i;
            sumc %= 256;
        }

        if (sum != sumc)
        {
            buffer[0] = NAK;
            writeDataToComPort(buffer, 1);
            continue;
        }

        buffer[0] = ACK;
        writeDataToComPort(buffer, 1);
        readDataOffComPort(buffer, 1);

        if (buffer[0] == EOT)
        {
            unsigned char last = 127;
            while (fileBuffer[last] == SUB)
                last--;

            fwrite(fileBuffer, last + 1, 1, fileHandle);
            break;
        }

        fwrite(fileBuffer, 128, 1, fileHandle);

    }

    // Close file stream
    fclose(fileHandle);

    // End transmission
    buffer[0] = ACK;
    writeDataToComPort(buffer, 1);
}

void sendFile()
{
    char buffer[3], fileBuffer[128];

    readDataOffComPort(buffer, 1);

    if (buffer[0] == NAK)
        isCrcEnabled = false;
    else if (buffer[0] == C)
        isCrcEnabled = true;
    else
        return;

    int n = 1;
    FILE *fileHandle = fopen(filename.c_str(), "rb");
    fseek(fileHandle, 0, SEEK_END);
    int fsize = ftell(fileHandle);
    fseek(fileHandle, 0, SEEK_SET);

    while (ftell(fileHandle) < fsize)
    {
        unsigned char length = fread(fileBuffer, 1, 128, fileHandle);
        for (int i = length; i < 128; i++)
            fileBuffer[i] = SUB;

        unsigned short sum = 0;

        sum = 0;

        if (isCrcEnabled)
            sum = calculateCrc16Checksum(fileBuffer);
        else
        {
            for (char i : fileBuffer)
                sum += (unsigned char) i;
            sum %= 256;
        }

        buffer[0] = SOH;
        buffer[1] = n;
        buffer[2] = 255 - n;

        writeDataToComPort(buffer, 3);
        writeDataToComPort(fileBuffer, 128);
        writeDataToComPort((char *) &sum, isCrcEnabled ? 2 : 1);

        readDataOffComPort(buffer, 1);
        if (buffer[0] == ACK)
            n++;
        else
            fseek(fileHandle, -128, SEEK_CUR);
    }

    fclose(fileHandle);
    do
    {
        buffer[0] = EOT;
        writeDataToComPort(buffer, 1);
        readDataOffComPort(buffer, 1);
    } while (buffer[0] != ACK);
}

/////////////////////////////////////////////////////////////////////////// Main
int main()
{
    // Choose work mode
    char mode;

    std::cout << "Do you want to [s]end or [r]eceive?: ";
    std::cin >> mode;

    // Choose filename
    std::cout << "Input filename: ";
    std::cin.get();
    std::getline(std::cin, filename);

    // Choose port number
    int comPortNumber;

    std::cout << "Enter desired COM port number: ";
    std::cin >> comPortNumber;

    // Choose if user wants to use CRC
    char crcMode;

    std::cout << "Do you want to use CRC, [y]/[n]: ";
    std::cin >> crcMode;

    isCrcEnabled = crcMode == 'y';

    // Perform main functionality
    initialiseComPort("COM" + std::to_string(comPortNumber));

    if (mode == 's')
        sendFile();
    else
        receiveFile();

    // End program successfully
    return 0;
}

////////////////////////////////////////////////////////////////////////////////
