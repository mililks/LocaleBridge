#define WIN32_LEAN_AND_MEAN
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <iostream>
#include <windows.h>
#include <winsock2.h>
#include<vector>
#include<mutex>
#include<string>
#include<map>
#include<thread>
#include<algorithm>

#pragma comment(lib, "ws2_32.lib")

using namespace std;

const int TCP_PORT = 5000;
const int UDP_PORT = 5001;

vector<SOCKET> clientSockets;
map<SOCKET, string> clientNames;
mutex clientMutex;

void ClientHandler(SOCKET clientSocket);
void UdpDiscoveryThread();

int main()
{
    SetConsoleCP(1251);
    SetConsoleOutputCP(1251);

    WSADATA wsaData{};
    const int startupResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (startupResult != 0) {
        cout << "Помилка ініціалізації Winsock!" << endl;
        return 0;
    }
    cout << "Winsock успішно ініціалізовано." << endl;

    SOCKET listeningSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listeningSocket == INVALID_SOCKET) {
        cout << "Помилка створення сокета!" << endl;
        WSACleanup();
        return 0;
    }
    cout << "Сокет успішно створено." << endl;

    sockaddr_in serverHint{};
    serverHint.sin_family = AF_INET;
    serverHint.sin_port = htons(5000);
    serverHint.sin_addr.S_un.S_addr = INADDR_ANY;
    if (bind(listeningSocket, (sockaddr*)&serverHint, sizeof(serverHint)) == SOCKET_ERROR) {
        cout << "Помилка прив'язки сокета до стурктури." << endl;
        closesocket(listeningSocket);
        WSACleanup();
        return 0;
    }
    cout << "Прив'язка сокета до структури успішна!" << endl;

    if (listen(listeningSocket, SOMAXCONN) == SOCKET_ERROR) {
        cout << "Помилка прослуховування!" << endl;
        closesocket(listeningSocket);
        WSACleanup();
        return 0;
    }
    cout << "Сервер прослуховує порт 5000. Очікування клієнтів..." << endl;

    thread udpThread(UdpDiscoveryThread);
    udpThread.detach();

    while (true) {
        sockaddr_in client{};
        int clientSize = sizeof(client);
        SOCKET clientSocket = accept(listeningSocket, (sockaddr*)&client, &clientSize);

        if (clientSocket == INVALID_SOCKET) {
            cout << "Помилка прийняття з'єднання." << endl;
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
        cout << "Помилка очищення ресурсів!" << endl;
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
        clientIP = "Невідома IP-адреса";
    }
    cout << "Новий клієнт '" << clientIP << "' підключився. Очікування імені..." << endl;

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

        cout << "'" << clientIP << "' зареєстрований як '" << clientName << "'." << endl;
    }
    else {
        clientMutex.lock();
        clientSockets.erase(remove(clientSockets.begin(), clientSockets.end(), clientSocket), clientSockets.end());
        clientMutex.unlock();
        closesocket(clientSocket);
        cout << "'" << clientIP << "' Клієнт відключився до надсилання імені." << endl;
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

    cout << "'" << clientName << "' Клієнт відключився. З'єднання закрито." << endl;
}

void UdpDiscoveryThread() {
    SOCKET udpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udpSocket == INVALID_SOCKET) {
        cout << "UDP Помилка створення сокета." << endl;
        return;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(UDP_PORT);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(udpSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        cout << "UDP Помилка прив'язки." << endl;
        closesocket(udpSocket);
        return;
    }
    cout << "UDP прослуховує порт " << UDP_PORT << "." << endl;

    char recvBuf[1024];
    sockaddr_in clientAddr{};
    int clientAddrSize = sizeof(clientAddr);

    while (true) {
        int bytesReceived = recvfrom(udpSocket, recvBuf, sizeof(recvBuf), 0, (sockaddr*)&clientAddr, &clientAddrSize);

        if (bytesReceived == SOCKET_ERROR) {
            if (WSAGetLastError() != 10004) {
                cout << "UDP Помилка прийому." << endl;
            }
            break;
        }

        string response = "OK:" + to_string(TCP_PORT);

        sendto(udpSocket, response.c_str(), (int)response.length(), 0, (sockaddr*)&clientAddr, clientAddrSize);

        string clientIP = inet_ntoa(clientAddr.sin_addr);
        cout << "UDP Discovery: відповів клієнту " << clientIP << endl;
    }

    closesocket(udpSocket);
}