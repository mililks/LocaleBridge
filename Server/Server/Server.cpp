#define WIN32_LEAN_AND_MEAN
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <iostream>
#include <windows.h>
#include <winsock2.h>
#include <vector>
#include <mutex>
#include <string>
#include <map>
#include <thread>
#include <algorithm>

#pragma comment(lib, "ws2_32.lib")

using namespace std;

const int TCP_PORT = 5000;

vector<SOCKET> clientSockets;
map<SOCKET, string> clientNames;
mutex clientMutex;

void ClientHandler(SOCKET clientSocket);

int main()
{
    SetConsoleCP(65001);
    SetConsoleOutputCP(65001);

    WSADATA wsaData{};
    const int startupResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (startupResult != 0) {
        cout << "Error initializing Winsock!" << endl;
        return 0;
    }
    cout << "Winsock initialized successfully." << endl;

    SOCKET listeningSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listeningSocket == INVALID_SOCKET) {
        cout << "Error creating socket!" << endl;
        WSACleanup();
        return 0;
    }
    cout << "Socket created successfully." << endl;

    sockaddr_in serverHint{};
    serverHint.sin_family = AF_INET;
    serverHint.sin_port = htons(TCP_PORT);
    serverHint.sin_addr.S_un.S_addr = INADDR_ANY;
    if (bind(listeningSocket, (sockaddr*)&serverHint, sizeof(serverHint)) == SOCKET_ERROR) {
        cout << "Error binding socket to address." << endl;
        closesocket(listeningSocket);
        WSACleanup();
        return 0;
    }
    cout << "Socket bound to address successfully!" << endl;

    if (listen(listeningSocket, SOMAXCONN) == SOCKET_ERROR) {
        cout << "Error listening on socket!" << endl;
        closesocket(listeningSocket);
        WSACleanup();
        return 0;
    }
    cout << "Server is listening on port 5000. Waiting for clients..." << endl;

    while (true) {
        sockaddr_in client{};
        int clientSize = sizeof(client);
        SOCKET clientSocket = accept(listeningSocket, (sockaddr*)&client, &clientSize);

        if (clientSocket == INVALID_SOCKET) {
            cout << "Error accepting connection." << endl;
            continue;
        }

        clientMutex.lock();
        clientSockets.push_back(clientSocket);
        clientMutex.unlock();

        thread clientThread(ClientHandler, clientSocket);
        clientThread.detach();
    }

    const int cleanupResult = WSACleanup();
    if (cleanupResult != 0) {
        cout << "Error cleaning up resources!" << endl;
    }

    return 0;
}

void ClientHandler(SOCKET clientSocket) {
    char buf[4096];
    int bytesReceived;
    string clientIP;
    sockaddr_in client_addr;
    int client_addr_size = sizeof(client_addr);
    string clientName;

    if (getpeername(clientSocket, (sockaddr*)&client_addr, &client_addr_size) == 0) {
        clientIP = inet_ntoa(client_addr.sin_addr);
    }
    else {
        clientIP = "Unknown IP address";
    }
    cout << "New client '" << clientIP << "' connected. Waiting for name..." << endl;

    fflush(stdout);

    bytesReceived = recv(clientSocket, buf, sizeof(buf) - 1, 0);

    if (bytesReceived > 0) {
        buf[bytesReceived] = '\0';
        string nameCandidate(buf);

        nameCandidate.erase(nameCandidate.find_last_not_of("\n\r\t") + 1);

        if (nameCandidate.empty()) {
            clientName = clientIP;
        }
        else {
            clientName = nameCandidate;
        }

        clientMutex.lock();
        clientNames[clientSocket] = clientName;
        clientName = nameCandidate;
        clientMutex.unlock();

        cout << "'" << clientIP << "' registered as '" << clientName << "'." << endl;
    }
    else {
        clientMutex.lock();
        clientSockets.erase(remove(clientSockets.begin(), clientSockets.end(), clientSocket), clientSockets.end());
        clientMutex.unlock();
        closesocket(clientSocket);
        cout << "'" << clientIP << "' Client disconnected before sending name." << endl;
        return;
    }

    while (true) {
        bytesReceived = recv(clientSocket, buf, sizeof(buf) - 1, 0);

        if (bytesReceived <= 0) {
            break;
        }

        buf[bytesReceived] = '\0';
        string receivedMessage = buf;
        string broadcastMessage = "[" + clientName + "] : " + receivedMessage;

        cout << ">> " << broadcastMessage << endl;

        clientMutex.lock();
        for (SOCKET otherSocket : clientSockets) {
            send(otherSocket, broadcastMessage.c_str(), (int)broadcastMessage.length(), 0);
        }
        clientMutex.unlock();
    }

    clientMutex.lock();

    for (auto it = clientSockets.begin(); it != clientSockets.end(); it++) {
        if (*it == clientSocket) {
            clientSockets.erase(it);
            break;
        }
    }

    clientNames.erase(clientSocket);
    clientMutex.unlock();
    closesocket(clientSocket);

    cout << "'" << clientName << "' Client disconnected. Connection closed." << endl;
}