#include <iostream>
#include <WinSock2.h>
#include <fstream>
#include <string>
#include <sstream>
#include <filesystem>
#include <vector>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <set>
#include <queue>
#include <unordered_set>
#include <atomic>
#pragma comment(lib, "ws2_32.lib")
#define CHUNK_SIZE 1024
#define MAX_CLIENTS 20
#define ROOMS 4
#include <memory>
using namespace std;
using namespace filesystem;
mutex console;
atomic<int> active_clients(0);
const path database = ".\\database";
void Print(const string& output) {
    lock_guard<mutex> lock(console);
    cout << output << endl;
}
   
struct Sending {
    static void sendOneByte(const SOCKET& client_socket, const char value) {
        send(client_socket, &value, 1, 0);
    }

    static void sendIntegerValue(const SOCKET& client_socket, int value) {
        value = htonl(value);
        send(client_socket, (char*)(&value), sizeof(value), 0);
    }

    static void sendMessage(const SOCKET& client_socket, const string& message, char tag) {
        send(client_socket, &tag, 1, 0);
        sendIntegerValue(client_socket, message.length());
        send(client_socket, message.c_str(), (int)message.length(), 0);
    }

    static void sendFile(const SOCKET& client_socket, const string& file_name) {
        sendOneByte(client_socket, 0x03);
        path p = database / path(file_name);
        sendIntegerValue(client_socket, file_size(p));
        ifstream file(p, ios::binary);
        char buffer_for_data[CHUNK_SIZE];
        while (file.read(buffer_for_data, sizeof(buffer_for_data))) {
            send(client_socket, buffer_for_data, (int)(file.gcount()), 0);
        }
        if (file.gcount() > 0)send(client_socket, buffer_for_data, (int)(file.gcount()), 0);
        file.close();
    }
};

struct Receiving {
    static int receiveInteger(const SOCKET& client_socket) {
        int value;
        recv(client_socket, (char*)(&value), sizeof(value), 0);
        return ntohl(value);
    }
    
    static string receiveMessage(const SOCKET& client_socket) {
        int length = receiveInteger(client_socket);
        if (length <= 0)return "";
        char* message = new char[length + 1];
        int received = recv(client_socket, message, length, 0);
        if (received <= 0) {
            delete[] message;
            return "";
        }
        message[length] = '\0';
        string result(message);
        delete[] message;
        return result;
    }

    static char receiveOneByte(const SOCKET& client_socket) {
        char byte;
        int received = recv(client_socket, &byte, 1, 0);
        if (received != 1)return 0;
        return byte;
    }

    static int receiveFile(const SOCKET& client_socket, const path& file_path, const int size) {
        path file_name = database / file_path.filename();
        if (exists(file_name))return 1;
        ofstream file(file_name, ios::binary);
        int i = 0;
        char buffer[CHUNK_SIZE] = { 0 };
        while (i != size) {
            int bytes_received = recv(client_socket, buffer, CHUNK_SIZE, 0);
            if (bytes_received <= 0 && i != size)return 0;
            file.write(buffer, bytes_received);
            i += bytes_received;
        }
        file.close();
        return 1;
    }
};

struct Client {
    const SOCKET socket;
    string name="";
    int room=-1;
    Client(const SOCKET& s): socket(s){}
};

struct Message {
    const string content;
    string add_info;
    const SOCKET sender;
    const char tag;
    Message(const char t, const string& m, const SOCKET& s, const string& a_i = "") : tag(t), content(m), sender(s), add_info(a_i) {}
};


class Room {
    int number;
    mutex queue_mutex, client_storage_mutex_;
    condition_variable cv;
    unordered_set<SOCKET> clients;
    queue<Message> message_queue;
    thread room_manager;
    bool server_alive = true;
    
    void sendToNeighbours(const Message& message)const {
        for (SOCKET client : clients) {
            if (client != message.sender) {
                Sending::sendMessage(client, message.content, message.tag);
                if (message.tag == 0x02)Sending::sendMessage(client, message.add_info, 0x04);
            }
        }
    }

public:
    Room(int n) :number(n) {
        room_manager = thread(&Room::broadcastMessage, this);
    }

    void broadcastMessage() {
        while (true) {
            unique_lock<mutex> lock(queue_mutex);
            cv.wait(lock, [this] {return !message_queue.empty() && clients.size() > 0 && server_alive; });
            if (!server_alive)break;
            while (!message_queue.empty()) {
                Message message = message_queue.front();
                message_queue.pop();
                if (message.tag == 0x06)Sending::sendMessage(message.sender, message.content, message.tag);
                else if (message.tag == 0x03)Sending::sendFile(message.sender, message.content);
                else sendToNeighbours(message);
            }
        }
    }

    void addMessageToQueue(const Message& message) {
        {
            lock_guard<mutex> lock(queue_mutex);
            message_queue.push(message);
        }
        cv.notify_all();
    }

    void addClient(const Client& client) {
        {
            lock_guard<mutex> lock(client_storage_mutex_);
            clients.insert(client.socket);
        }
        string confirmation = client.name + "-"+ to_string(client.socket)+ " joined";
        addMessageToQueue(Message(0x01, confirmation, client.socket));
        Print("\033[94mClient\033[36m " + client.name + "\033[94m joined ROOM\033[36m " + to_string(client.room) + "\033[0m");///
    }

    void takeClientAway(const Client& client) {    
        
        lock_guard<mutex> lock(client_storage_mutex_);
        clients.erase(client.socket);
        string bye_message = client.name + "-" + to_string(client.socket) + " is leaving the room";
        addMessageToQueue(Message(0x01, bye_message, client.socket));
        Print("\033[94mClient\033[36m " + client.name + "\033[94m left ROOM\033[36m " + to_string(client.room) + "\033[0m");///
    }
    
    ~Room() {
        server_alive = false;
        room_manager.join();
    }
};

class Chat {

    vector<unique_ptr<Room>> rooms;

    void createRooms() {
        for (int i = 0; i < ROOMS; i++) {
            rooms.emplace_back(make_unique<Room>(i));  
        }
    }

    int getRoom(const SOCKET& client_socket) {
        int room_number =Receiving::receiveOneByte(client_socket)-'0';
        while (room_number <= 0 || room_number > ROOMS) {
            char incorrect_room = 0x00;
            Sending::sendOneByte(client_socket, incorrect_room); 
            room_number = Receiving::receiveOneByte(client_socket)-'0';

        }
        char correct_room_confirmation = 0x01;
        Sending::sendOneByte(client_socket, correct_room_confirmation);
        return --room_number;
    }

    bool isGreetingSuccessful(const SOCKET& client_socket) {
        const string expected_greeting = "Hello, server! This is client:)";
        string received_greeting = Receiving::receiveMessage(client_socket);
        if (received_greeting == expected_greeting) {
            if (received_greeting == expected_greeting) {
                Print("\033[95mClient with socket " + to_string(client_socket) + ": " + received_greeting + "\033[0m");
                return true;
            } 
        }
        return false;
    }

    string getName(const SOCKET& client_socket) {  
        Sending::sendMessage(client_socket,"Tell me your name, please!", 0x06 );
        string name = Receiving::receiveMessage(client_socket);
        string hello = "Hello, " + name + "! This is the server. What is the room number?";
        Sending::sendMessage(client_socket, hello, 0x06);
        return name;     
    }

    void closeClient(const Client& client) {

        if(client.room != -1)rooms[client.room]->takeClientAway(client);
        active_clients--;
        closesocket(client.socket);
        Print("\033[94m" + client.name + "-" + to_string(client.socket) + " disconnected\033[94m");
    }

    void registerClient(Client& client) {
        client.name = getName(client.socket);
        client.room = getRoom(client.socket);
        int room = client.room + 1;
        string message = "Successfully registered with a name " + client.name + "-" + to_string(client.socket) + " in the room " + to_string(room)+".";
        Sending::sendMessage(client.socket, message, 0x06);
        rooms[client.room]->addClient(client);
    }

    void changeRoom(Client& client, int room_number) {
        if (room_number < 0 || room_number > ROOMS || room_number == client.room) {
            rooms[client.room]->addMessageToQueue(Message(0x06, "Invalid room number!", client.socket));
            return;
        }
        rooms[client.room]->takeClientAway(client);
        client.room = --room_number;
        rooms[client.room]->addClient(client);
        rooms[client.room]->addMessageToQueue(Message(0x06, "You successfully entered the desired room!", client.socket));
    }

    void handleTag3(Client& client, const string& data) {
        int size = Receiving::receiveInteger(client.socket);
        int result = Receiving::receiveFile(client.socket, data, size);
        if (result == 0) {
            rooms[client.room]->addMessageToQueue(Message(0x06, "File was not sent due to some unexpected issues!", client.socket));
            return;
        }
        rooms[client.room]->addMessageToQueue(Message(0x06, "File was sent!", client.socket));
        string message = client.name + "-" + to_string(client.socket) + " wants to send a file of size " + to_string(size) + " bytes. Do you accept it? It's name is ";
        rooms[client.room]->addMessageToQueue(Message(0x02, message, client.socket, path(data).filename().string()));
    }
      
    void handleIncomingData(Client& client, char tag, const string& data) {
        switch(tag) {
        case 0x01:
            rooms[client.room]->addMessageToQueue(Message(0x01, client.name + "-" + to_string(client.socket) + ": " + data, client.socket));
            break;
        case 0x02:
            changeRoom(client, stoi(data));
            break;
        case 0x03:
            handleTag3(client, data);
            break;
        case 0x04:
            if(data != "no")rooms[client.room]->addMessageToQueue(Message(0x03, data, client.socket));
            break;
        }
    }

public:

    Chat() {
        createRooms();
    }

    void handleClient(const SOCKET& client_socket) {
        Client client(client_socket);
        if (isGreetingSuccessful(client_socket)) {
            registerClient(client);
            char buffer[1024];
            while (true) {
                char tag = Receiving::receiveOneByte(client_socket);
                if (tag == 0) break;
                if (tag == 0x05) {
                    Sending::sendMessage(client.socket, "Bye client!", 0x05);
                    break;
                }
                string message = Receiving::receiveMessage(client_socket);
                if (message.empty())break;
                Print("\033[94m" + client.name + " from room " + to_string(client.room) + ": " + message + "\033[94m");
                handleIncomingData(client, tag, message);
            }
        }
        else Sending::sendMessage(client.socket, "The protocol was ignored!",0x05);
        closeClient(client);
    }
};

int main()
{
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        cerr << "\033[94mWSAStartup failed!\033[0m" << endl;
        return 1;
    }

    const int port = 12345;
    SOCKET server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == INVALID_SOCKET) {
        cerr << "\033[94mError craeting socket: " << WSAGetLastError() << "\033[0m" << endl;
        WSACleanup();
        return 1;
    }

    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_socket, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) == SOCKET_ERROR)
    {
        cerr << "\033[94mBind failed with error: " << WSAGetLastError() << "\033[0m" << endl;
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    if (listen(server_socket, SOMAXCONN) == SOCKET_ERROR) {
        cerr << "\033[94mListen failed with error: " << WSAGetLastError() << "\033[0m" << endl;
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    cout << "\033[94mServer listening on port " << port << "\033[0m" << endl;
    Chat chat;
    while (true) {
        if (active_clients.load() < MAX_CLIENTS) {
            SOCKET client_socket = accept(server_socket, nullptr, nullptr);
            if (client_socket == INVALID_SOCKET) {
                Print("\033[95mAccept failed with error: " + to_string(WSAGetLastError()) + "\033[0m");
                closesocket(server_socket);
                WSACleanup();
                continue;
            }
            int expected = active_clients.load();
            while (expected < MAX_CLIENTS && !active_clients.compare_exchange_weak(expected, expected + 1)) {
                expected = active_clients.load();
            }
            if (expected >= MAX_CLIENTS) {
                closesocket(client_socket);  
                continue;
            }
           thread(&Chat::handleClient, &chat, client_socket).detach();
        }
    }

    closesocket(server_socket);
    WSACleanup();
    return 0;
}
