#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <iostream>
#include <unistd.h>

using namespace std;

#define DEFAULT_BUFLEN 1024
#define DEFAULT_PORT "95"

int __cdecl main(int argc, char **argv)
{
    WSADATA wsaData;
    SOCKET ConnectSocket = INVALID_SOCKET;
    struct addrinfo *result = NULL,
                    *ptr = NULL,
                    hints;

    char recvbuf[DEFAULT_BUFLEN];
    int iResult;
    int recvbuflen = DEFAULT_BUFLEN;
    char *buffer;

    // Initialize Winsock
    iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0)
    {
        cout << "WSAStartup failed with error: " << iResult << endl;
        return 1;
    }

    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    // Resolve the server address and port
    iResult = getaddrinfo(argv[1], DEFAULT_PORT, &hints, &result);
    if (iResult != 0)
    {
        cout << "getaddrinfo failed with error: " << iResult << endl;

        WSACleanup();
        return 1;
    }

    // Attempt to connect to an address until one succeeds
    for (ptr = result; ptr != NULL; ptr = ptr->ai_next)
    {

        // Create a SOCKET for connecting to server
        ConnectSocket = socket(ptr->ai_family, ptr->ai_socktype,
                               ptr->ai_protocol);
        if (ConnectSocket == INVALID_SOCKET)
        {
            cout << "socket failed with error: " << WSAGetLastError() << endl;
            WSACleanup();
            return 1;
        }

        // Connect to server.
        iResult = connect(ConnectSocket, ptr->ai_addr, (int)ptr->ai_addrlen);
        if (iResult == SOCKET_ERROR)
        {
            closesocket(ConnectSocket);
            ConnectSocket = INVALID_SOCKET;
            continue;
        }
        break;
    }

    freeaddrinfo(result);

    if (ConnectSocket == INVALID_SOCKET)
    {
        cout << "Unable to connect to server!" << endl;
        WSACleanup();
        return 1;
    }
    int count = 0;
    string username;
    cout << "Enter your UserName: " << endl;
    getline(cin, username);

    iResult = send(ConnectSocket, username.c_str(), int(username.length()), 0);

    if (iResult == SOCKET_ERROR)
    {
        cout << "send failed with error: " << WSAGetLastError() << endl;
        closesocket(ConnectSocket);
        WSACleanup();
    }

    string input;
    
    do
    {

        // Send an initial buffer

        cout << "Enter your commands: " << endl;

        getline(cin, input);

        iResult = send(ConnectSocket, input.c_str(), int(input.length()), 0);

        if (iResult == SOCKET_ERROR || iResult <= 0)
        {
            cout << "send failed with error: " << WSAGetLastError() << endl;
            closesocket(ConnectSocket);
            WSACleanup();
            count++;
            if (count == 1)
                continue;
            else
                break;
        }
        

        while (true)
        {

            iResult = recv(ConnectSocket, recvbuf, recvbuflen, 0);
            if (iResult > 0)
            {
                recvbuf[iResult] = '\0';
                cout << recvbuf << endl;
                if (recvbuf[iResult - 1] == '#')
                    break;
                else
                {
                    iResult = 0;
                    continue;
                }
            }
            else if (iResult == 0)
            {
                cout << "Connection closed" << endl;
                cout << "Nothing to receive" << endl;
                break;
            }
            else
                cout << "recv failed with error: " << WSAGetLastError() << endl;
        }

    } while (input != "exit;");


    cout<<"Connection Closing"<<endl;
    // shutdown the connection since no more data will be sent
    iResult = shutdown(ConnectSocket, SD_SEND);
    if (iResult == SOCKET_ERROR)
    {
        cout << "shutdown failed with error: " << WSAGetLastError() << endl;
        closesocket(ConnectSocket);
        WSACleanup();
        return 1;
    }

    // cleanup
    closesocket(ConnectSocket);
    WSACleanup();

    return 0;
}