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
  
 /*   string ToUpper(string word)const {
        transform(word.begin(), word.end(), word.begin(), ::toupper);
        return word;
    }*/

   
struct Sending {

   /* static void sendOneByte(const SOCKET& client_socket, const uint8_t value) {
        send(client_socket, (char*)(&value), 1, 0);
    }*/
    static void sendOneByte(const SOCKET& client_socket, const char value) {
        send(client_socket, &value, 1, 0);
    }
    static void sendIntegerValue(const SOCKET& client_socket, int value) {
        value = htonl(value);
        send(client_socket, (char*)(&value), sizeof(value), 0);
    }
    static void sendMessage(const SOCKET& client_socket, const string& message) {
        sendIntegerValue(client_socket, message.length());
        send(client_socket, message.c_str(), (int)message.length(), 0);
    }
    static void sendFile(const SOCKET& client_socket, const string& file_name) {
        sendMessage(client_socket, file_name);
        sendIntegerValue(client_socket, file_size(path(file_name)));
        ifstream file(file_name, ios::binary);
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
        value = ntohl(value);
        return value;
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

    static int receiveOneByte(const SOCKET& client_socket) {
        char byte;
        int received = recv(client_socket, &byte, 1, 0);
        if (received != 1)return 0;
        return (int)byte;
    }

    static int receiveFile(const SOCKET& client_socket, const path& file_path, const int size) {
        path file_name = database / file_path.filename();
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
    const SOCKET sender;
    const char tag;
    Message(const char t, const string& m, const SOCKET& s) : tag(t), content(m), sender(s) {}

};
struct SocketHash {
    int operator()(const SOCKET& s) const {
        return hash<int>()((int)(s));
    }
};

class Room {
    int number;
    mutex queue_mutex, client_storage_mutex_;
    condition_variable cv;
    unordered_set<SOCKET> clients;
    queue<Message> message_queue;
    thread room_manager;

  /*  void broadcastServerInfo(string& message) {
        for (SOCKET client : clients) {
            send(client, message.c_str(), (int)message.length(), 0);
        }
    }*/

    void manageResponses(const Message& message) {
        int i = 0;
        while (i < clients.size()) {
            for (SOCKET client : clients) {
                int response = Receiving::receiveOneByte(client);
                if (response == 2) {
                    Sending::sendFile(client, message.content);
                    string confirm = Receiving::receiveMessage(client);
                    addMessageToQueue(Message(0x01, confirm, client));
                }
                i++;
                
            }
        }
        Sending::sendOneByte(message.sender, 0x01);
    }


    void broadcastMessage() {
        while (true) {
            unique_lock<mutex> lock(queue_mutex);
            cv.wait(lock, [this] {return !message_queue.empty() && clients.size() > 0; });
            while (!message_queue.empty()) {
                Message message = message_queue.front();
                message_queue.pop();
                for (SOCKET client : clients) {
                    if (client != message.sender) {
                        Sending::sendOneByte(client, message.tag);
                        Sending::sendMessage(client, message.content);           
                    }
                }
                if (message.tag == 0x02)manageResponses(message);
            }
        }
    }

public:
    Room(int n) :number(n) {
        room_manager = thread(&Room::broadcastMessage, this);
    }

    void addMessageToQueue(const Message& message) {
        {
            lock_guard<mutex> lock(queue_mutex);
            message_queue.push(message);
        }
        cv.notify_one();
    }

    void addClient(const Client& client) {
        {
            lock_guard<mutex> lock(client_storage_mutex_);
            clients.insert(client.socket);
        }
        string confirmation = "Client " + client.name + " "+ to_string(client.socket)+ " joined";
        addMessageToQueue(Message(0x01, confirmation, client.socket));
        Print("\033[94mClient\033[36m " + client.name + "\033[94m joined ROOM\033[36m " + to_string(number) + "\033[0m");
    }

    void takeClientAway(const Client& client) {
        {
            lock_guard<mutex> lock(client_storage_mutex_);
            clients.erase(client.socket);
        }
        string bye_message = "Client " + client.name + " " + to_string(client.socket) + " left the room";
        addMessageToQueue(Message(0x01,bye_message, client.socket));
        //broadcastServerInfo(bye_message);
    }
    ~Room() {
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
        int room_number=Receiving::receiveOneByte(client_socket);
        while (room_number < 0 || room_number > ROOMS) {
            char incorrect_room = 0x00;
            Sending::sendOneByte(client_socket, incorrect_room); 
            room_number = Receiving::receiveOneByte(client_socket);

        }
        char correct_room_confirmation = 0x01;
        Sending::sendOneByte(client_socket, correct_room_confirmation);
        return room_number;
    }

    bool isGreetingSuccessful(const SOCKET& client_socket) {
        const string expected_greeting = "Hello, server! This is client:)";
        char* received_greeting = new char[expected_greeting.length() + 1];
        int bytesReceived = recv(client_socket, received_greeting, (int)expected_greeting.length(), 0);
        if (bytesReceived == expected_greeting.length()) {
            received_greeting[expected_greeting.length()] = '\0';
            if (string(received_greeting) == expected_greeting) {
                Print("\033[95m Client with socket " + to_string(client_socket) + ": " + received_greeting + "\033[0m");
                delete[] received_greeting;
                return true;
            } 
        }
        delete[] received_greeting;
        return false;
    }

    string getName(const SOCKET& client_socket) {  
        Sending::sendMessage(client_socket,"Tell me your name, please!" );
        string name = Receiving::receiveMessage(client_socket);
        string hello = "Hello, " + name + "! This is the server. What is the room number?";
        Sending::sendMessage(client_socket, hello);
        return name;     
    }

    void CloseClient( Client& client) {
        Print("\033[94mClient" + client.name + " with socket " + to_string(client.socket) + " disconnected\033[94m");
        rooms[client.room]->takeClientAway(client.socket);
        active_clients--;
        closesocket(client.socket);
        Print("\033[94mClient " + client.name + " " + to_string(client.socket) + " disconnected.\033[0m");
    }
    void Register(Client& client) {
        client.name = getName(client.socket);
        client.room = getRoom(client.socket);
        Sending::sendMessage(client.socket, "Registered.");

    }
    void changeRoom(Client& client, int room_number) {
        if (room_number < 0 || room_number > ROOMS || room_number == client.room)Sending::sendMessage(client.socket, "Invalid room number!");
        rooms[client.room]->takeClientAway(client);
        client.room = room_number;
        rooms[room_number]->addClient(client);
        Sending::sendMessage(client.socket, "You successfully entered the desired room!");
    }

    void handleTag3(Client& client, const string& data) {
        int size = Receiving::receiveInteger(client.socket);
        int result = Receiving::receiveFile(client.socket, data, size);
        Sending::sendOneByte(client.socket, (char)result);
        if (result == 0)return;
        Sending::sendOneByte(client.socket, 0x02);
        string message = "Client " + client.name + " " + to_string(client.socket) + " wants to send a file " +
        data + " of size " + to_string(size) + " bytes. Do you accept it? ";
        rooms[client.room]->addMessageToQueue(Message(0x02, data, client.socket));
    }
    
    void handleIncomingData(Client& client, int tag, const string& data) {
        switch(tag) {
        case 1:
            rooms[client.room]->addMessageToQueue(Message(0x01, data, client.socket));
            break;
        case 2:
            changeRoom(client, stoi(data));
            break;
        case 3:
            handleTag3(client, data);
            break;
        }
    }
public:

    Chat() {
        createRooms();
    }

    void HandleClient(const SOCKET& client_socket) {
        Client client(client_socket);
        if (isGreetingSuccessful(client_socket)) {
            Register(client);
            char buffer[1024];
            while (true) {
                int tag = Receiving::receiveOneByte(client_socket);
                if (tag == 0) break;     
                string message = Receiving::receiveMessage(client_socket);
                if (message.empty())break;
                string messsage_from_client = "Client " + client.name + " " + to_string(client_socket) + ": " + message;
                rooms[client.room]->addMessageToQueue(Message(0x01, messsage_from_client, client_socket));

                Print("\033[94m" + client.name + ": " + message + "\033[94m");
            }
        }
        else Sending::sendMessage(client.socket, "The protocol was ignored!");
        CloseClient(client);
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
            active_clients++;
           thread(&Chat::HandleClient, &chat, client_socket).detach();
        }
    }
    closesocket(server_socket);
    WSACleanup();
    return 0;
}
