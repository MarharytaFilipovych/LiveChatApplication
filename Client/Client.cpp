#include <iostream>
#include <WinSock2.h>
#include <filesystem>
#include <Ws2tcpip.h>
#include <fstream>
#include <sstream>
#include <thread>
#include <mutex>
#include <unordered_set>
#pragma comment(lib, "ws2_32.lib")
#define CHUNK_SIZE 1024
using namespace std;
using namespace filesystem;
mutex console;

void Print(const string& output) {
	lock_guard<mutex> lock(console);
	cout << output << "\033[0m"<< endl;
}

const path database = ".\\database";


struct Sending {

	static void sendOneByte(const SOCKET& client_socket, const uint8_t value) {
		send(client_socket, (char*)(&value), 1, 0);
	}

	static void sendIntegerValue(const SOCKET& client_socket, int value) {
		value = htonl(value);
		send(client_socket, (char*)(&value), sizeof(value), 0);
	}

	static void sendMessage(const SOCKET& client_socket, const string& message) {
		sendIntegerValue(client_socket, message.length());
		send(client_socket, message.c_str(), (int)message.length(), 0);
		
	}

	static void sendFile(const SOCKET& client_socket, const path& file_name) {
		sendIntegerValue(client_socket, file_size(file_name));
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

	static string receiveResponse(const SOCKET& client_socket) {
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

	static int receiveFile(const SOCKET& client_socket) {
		path file_name = database / path(receiveResponse(client_socket));
		int size_of_file = receiveInteger(client_socket);
		ofstream file(file_name, ios::binary);
		int i = 0;
		char buffer[CHUNK_SIZE] = { 0 };
		while (i != size_of_file) {
			int bytes_received = recv(client_socket, buffer, CHUNK_SIZE, 0);
			if (bytes_received <= 0 && i != size_of_file)return 0;
			file.write(buffer, bytes_received);
			i += bytes_received;
		}
		file.close();
		return 1;
	}
};


class Registration {
	const SOCKET socket;

	void SendInitialGreeting() {
		string initial_hello = "Hello, server! This is client:)";
		Sending::sendMessage(socket, initial_hello);
		Print("\033[94mWaiting for a server to accept us...\033[0m");
		string response = Receiving::receiveResponse(socket);
		if (response == "The protocol was ignored!!") {
			Print("\033[94m" + response);
			closesocket(socket);
			WSACleanup();
			exit(EXIT_FAILURE);
		}
	}

	void sendName() {
		string client_name;
		{
			lock_guard<mutex> lock(console);
			cout << "Enter client name: ";
			getline(cin, client_name);
		}		
		Sending::sendMessage(socket, client_name);
	}

	void sendRoom() {
		uint8_t room;
		{
			lock_guard<mutex> lock(console);
			cin >> room;
		}
		Sending::sendOneByte(socket, room);
		char confirmation = Receiving::receiveOneByte(socket);
		while (confirmation == 0x00) {
			{
				lock_guard<mutex> lock(console);
				cout << "Choose another room number: " << endl;
				cin >> room;
			}
			Sending::sendOneByte(socket, room);
			confirmation = Receiving::receiveOneByte(socket);
		}
	}
	void getRegistered() {
		SendInitialGreeting();
		sendName();
		string buffer = Receiving::receiveResponse(socket);
		Print("\033[94m" + buffer);
		sendRoom();
		string register_completed = Receiving::receiveResponse(socket);
		Print("\033[94m" + string(register_completed));

	}
public:
	Registration(const SOCKET& client_socket) :socket(client_socket) {
		getRegistered();
	}
};

struct InputParser {

	static void printInstructions() {
		lock_guard<mutex> lock(console);
		cout << "\033[94mRules:\n"
			<< "Yous hould enter such numbers before you input depending on the purpose:\n"
			<< "* 1 - simple message\n"
			<< "* 2 - change room: 2 ROOM_NUMBER\n"
			<< "* 3 - send file: 3 PATH" << endl;
	}

	static void getInput(string& input) {
		lock_guard<mutex> lock(console);
		cout << "> ";
		getline(cin, input);
	}

	static int getType(string& input) {
		if (input.empty() || input.length() < +2)return 0;
		string number;
		stringstream ss(input);
		ss >> number;
		if (number != "1" || number != "2" || number != "3") {
			return 0;
		}
		return stoi(number);
	}

	static bool stringConatinsOnlyDigits(const string& word) {
		return all_of(word.begin(), word.end(), ::isdigit);

	}

	static string getMessageItself(const string& input) {
		if (input[1] == ' ')return input.substr(2);
		return input.substr(1);
	}
	static path GetFileFromInput(const string& input) {
		string file;
		int index_end = input.length();
		if (input[0] == '"' && input[index_end - 1] == '"') file = input.substr(1, index_end - 1);
		replace(file.begin(), file.end(), '\\', '/');
		return file;
	}
	static string ToUpper(string input) {
		transform(input.begin(), input.end(), input.begin(), ::toupper);
		return input;
	}
	static bool isIncorrectFile(const path& file)  {
		return !is_regular_file(file) || !exists(file) || file_size(file) == 0;
	}

	
	static bool accept() {
		string decision;
		{
			lock_guard<mutex> lock(console);
			cout << "Type yes/no: ";
			cin >> decision;
		}
		return isYes(decision);
	}

private:
	static bool isYes(const string& decision) {
		unordered_set<string> possible_yes = { "yes", "y", "yeah", "yep" };
		return possible_yes.find(ToUpper(decision)) != possible_yes.end();
	}
};

class MessageHandler {
	const SOCKET socket;

public:

	MessageHandler(const SOCKET& s) :socket(s) {};

	void sendSimpleMessage(const string& input, uint8_t tag)const {
		Sending::sendOneByte(socket, tag);
		Sending::sendMessage(socket, InputParser::getMessageItself(input));
	}

	void changeRoom(const string& input)const {
		string message = InputParser::getMessageItself(input);
		if (!InputParser::stringConatinsOnlyDigits(message)) {
			Print("\033[95mIncorrect input!");
			return;
		}
		sendSimpleMessage(message, 2);
		string response = Receiving::receiveResponse(socket);
		Print("\033[94m" + response);
	}

	void sendFile(const string& input)const {
		path file = InputParser::GetFileFromInput(InputParser::getMessageItself(input));
		if (!InputParser::isIncorrectFile(file)) {
			Print("\033[95mIncorrect path!");
			return;
		}
		sendSimpleMessage(file.string(), 3);
		Sending::sendFile(socket, file);
		char result = Receiving::receiveOneByte(socket);
		if (result == 0x00)return;
		// main thread has to wait until gets confirm
		char confirm = Receiving::receiveOneByte(socket);
		while (confirm != 0x01) {
			confirm = Receiving::receiveOneByte(socket);
		}
	}

};

class Receiver {

	thread receiver;
	const SOCKET socket;




	void receiveMessages()  {
		while (true) {
			char tag = Receiving::receiveOneByte(socket);
			if (tag == 0) {
				Print("\033[94mServer disconnected!");
				exit(1);
			}
			string message = Receiving::receiveResponse(socket);
			if (message.empty()) {
				Print("\033[94mServer disconnected!");
				exit(1);
			}
			Print("\033[94" + message);
			if (tag==0x02) {
				if (InputParser::accept()) {
					Sending::sendOneByte(socket, 2);
					int res = Receiving::receiveFile(socket);
					if (res == 1)Sending::sendMessage(socket, "Received!");
					else Sending::sendMessage(socket, "Failed to receive!");
				}else Sending::sendOneByte(socket, 1);

			}
		}
	}

public:
	Receiver(const SOCKET& s):socket(s) {
		receiver = thread(&Receiver::receiveMessages, this);
	}
};

int main()
{
	WSADATA wsa_data;
	if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
		cerr << "WSAStartup failed" << endl;
		return 1;
	}

	const int port = 12345;
	const PCWSTR server_ip = L"127.0.0.1";
	SOCKET client_socket = socket(AF_INET, SOCK_STREAM, 0);
	if (client_socket == INVALID_SOCKET) {
		cerr << "Error creating socket: " << WSAGetLastError() << endl;
		WSACleanup();
		return 1;
	}

	sockaddr_in server_addr;
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port);
	InetPton(AF_INET, server_ip, &server_addr.sin_addr);


	if (connect(client_socket, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) == SOCKET_ERROR) {
		cerr << "Connect failed with error: " << WSAGetLastError() << endl;
		closesocket(client_socket);
		WSACleanup();
		return 1;
	}
	Registration regisration(client_socket);
	
	InputParser::printInstructions();
	MessageHandler handler(client_socket);
	Receiver receiver(client_socket);
	string input;
	InputParser::getInput(input);
	while (true) {
		if (InputParser::ToUpper(input) == "EXIT") {
			//client.SendMessageToServer("\033[91mClient decided to terminate the connection.\033[0m");
			break;
		}	
		int tag = InputParser::getType(input);
		if (tag == 0) Print("\033[95mInvalid input!");
		else if (tag == 1)handler.sendSimpleMessage(input, tag);
		else if (tag == 2)handler.changeRoom(input);
		else  handler.sendFile(input);
		
		InputParser::getInput(input);
	}

	closesocket(client_socket);
	WSACleanup();
}

