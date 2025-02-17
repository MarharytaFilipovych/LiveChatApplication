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
using namespace std;
using namespace filesystem;
const path database = ".\\database";
mutex console;
atomic<int> active_clients(0);

void Print(const string& output) {
    lock_guard<mutex> lock(console);
    cout << output << endl;
}
  
 /*   string ToUpper(string word)const {
        transform(word.begin(), word.end(), word.begin(), ::toupper);
        return word;
    }*/

    /*string GetCommandForConstructor() {
        string command;
        stringstream ss(request_);
        ss >> command;
        return command;
    }*/
    
   /* bool ConatinsInvalidPath(const path& p) const {
        if (!exists(p)) return true;
        if (command_ == "REMOVE")  return !is_directory(p);
        else return !is_regular_file(p);
    }*/

    
   /* const path GetPath() {
        if (command_ == "LIST")return "";
        int start_index = command_.length() + 1;
        int index_end = request_.length();
        string file;
        if (request_[start_index] == '"' && request_[index_end - 1] == '"')file = request_.substr(start_index + 1, index_end - start_index - 2);
        else file = request_.substr(start_index, index_end - start_index);
        replace(file.begin(), file.end(), '\\', '/');
        return file;
    }*/

//struct Client {
//    const SOCKET socket_;
//    const string name_;
//    Client(const SOCKET& socket, const string& client_name) : socket_(socket), name_(client_name) {}    
//};



   /* void Get(const path& file_name) const {
        ifstream file(file_name, ios::binary);
        int size = htonl(file_size(file_name));
        send(client_.socket_, (char*)(&size), sizeof(size), 0);
        char buffer_for_data[CHUNK_SIZE];
        while (file.read(buffer_for_data, sizeof(buffer_for_data))) {
            send(client_.socket_, buffer_for_data, (int)(file.gcount()), 0);
        }
        int remaining_bytes = file.gcount();
        if (remaining_bytes > 0) {
            send(client_.socket_, buffer_for_data, (int)remaining_bytes, 0);
        }
        file.close();
        SendResponse("File transfer completed!");
    }*/

   

    /*void Put(const path& file_path) {
        int size_of_file;
        recv(client_.socket_, (char*)(&size_of_file), sizeof(size_of_file), 0);
        size_of_file = ntohl(size_of_file);

        path file_name = client_.folder_ / file_path.filename();
        ofstream file(file_name, ios::binary);
        int i = 0;
        while (i != size_of_file) {
            int bytes_received = GetReadBytes();
            file.write(client_.buffer_, bytes_received);
            i += bytes_received;
        }
        file.close();
        SendResponse("File transfer completed!");
    }*/


struct Client {
    const SOCKET socket;
    string name="";
    int room=-1;
    Client(const SOCKET& s): socket(s){}
};

struct Message {
    const string content;
    const SOCKET sender;
    Message(const string& m, const SOCKET& s) : content(m), sender(s) {}

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

    void broadcastMessage() {
        while (true) {
            unique_lock<mutex> lock(queue_mutex);
            cv.wait(lock, [this] {return !message_queue.empty() && clients.size() > 0; });
            while (!message_queue.empty()) {
                Message message = message_queue.front();
                message_queue.pop();
                for (SOCKET client : clients) {
                    if (client != message.sender)  send(client, (message.content).c_str(), message.content.length(), 0);
                }
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

    void addClient(const SOCKET& client, const string& client_name) {
        {
            lock_guard<mutex> lock(client_storage_mutex_);
            clients.insert(client);
        }
        string confirmation = "Client " + client_name + " "+ to_string(client)+ " joined";
        addMessageToQueue(Message(confirmation, client));
        Print("\033[94mClient\033[36m " + client_name + "\033[94m joined ROOM\033[36m " + to_string(number) + "\033[0m");
    }

    void takeClientAway(const Client& client) {
        {
            lock_guard<mutex> lock(client_storage_mutex_);
            clients.erase(client.socket);
        }
        string bye_message = "Client " + client.name + " " + to_string(client.socket) + " left the room";
        addMessageToQueue(Message(bye_message, client.socket));
        //broadcastServerInfo(bye_message);
    }
    ~Room() {
        room_manager.join();
    }
};


class Chat {

    vector<Room> rooms;

    void createRooms() {
        for (int i = 0; i < ROOMS; i++) {
            rooms.emplace_back(Room(i));
        }
    }

    int getRoom(const SOCKET& client_socket) {
        char room_number;
        recv(client_socket, &room_number, 1, 0);
        while ((int)room_number < 0 || (int)room_number > ROOMS) {
            char incorrect_room = 0x00;
            send(client_socket, &incorrect_room, 1, 0);
            recv(client_socket, &room_number, 1, 0);

        }
        char correct_room_confirmation = 0x01;
        send(client_socket, &correct_room_confirmation, 1, 0);
        return (int)room_number;
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
        char name_length;
        recv(client_socket, &name_length, 1, 0);
        name_length = (int)name_length;
        char* name = new char[name_length + 1];
        recv(client_socket, name, name_length, 0);
        name[name_length] = '\0';
        string hello = "Hello, " + string(name) + "! This is the server. What is the room number?";
        send(client_socket, hello.c_str(), (int)(hello.length()), 0);
        return string(name);     
    }

    void CloseClient( Client& client) {
        rooms[client.room].takeClientAway(client.socket);
        active_clients--;
        closesocket(client.socket);
        Print("\033[94mClient " + client.name + " " + to_string(client.socket) + " disconnected.\033[0m");
    }

public:

    Chat() {
        createRooms();
    }

    void HandleClient(const SOCKET& client_socket) {
        Client client(client_socket);
        if (isGreetingSuccessful(client_socket)) {
            client.name = getName(client_socket);
            client.room = getRoom(client_socket);
            char buffer[1024];
            while (true) {
                int bytesReceived = recv(client_socket, buffer, sizeof(buffer), 0);
                if (bytesReceived <= 0) {
                    Print("\033[94mClient" + client.name + " with socket " + to_string(client_socket) + " disconnected\033[94m");
                    break;
                }
                buffer[bytesReceived] = '\0';
                string message(buffer);
                string messsage_from_client = "Client " + client.name + " " + to_string(client_socket) + ": " + message;
                rooms[client.room].addMessageToQueue(Message(messsage_from_client, client_socket));

                Print("\033[94m" + client.name + ": " + message + "\033[94m");
            }
        }
        else {
            const char* error = "The protocol was ignored!";
            send(client_socket, error, (int)(strlen(error)), 0);
        }

        CloseClient(client);
    }
};

int main()
{
    vector<thread> threads;
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
            threads.emplace_back([&chat, client_socket] {
                chat.HandleClient(client_socket);
             });
            threads.back().detach();
        }
    }
    closesocket(server_socket);
    WSACleanup();
    return 0;
}
