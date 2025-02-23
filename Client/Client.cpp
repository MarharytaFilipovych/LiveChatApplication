#include <iostream>
#include <WinSock2.h>
#include <filesystem>
#include <Ws2tcpip.h>
#include <fstream>
#include <sstream>
#include <thread>
#include <mutex>
#include <unordered_set>
#include <limits>
#pragma comment(lib, "ws2_32.lib")
#define CHUNK_SIZE 1024
using namespace std;
using namespace filesystem;
mutex console;
void Print(const string& output) {
	lock_guard<mutex> lock(console);
	cout << output << "\033[0m"<< endl;
}
path database = path(".\\database");

struct Sending {

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
		Print("\033[94m" + response);
		if (response == "The protocol was ignored!!") {
			closesocket(socket);
			WSACleanup();
			exit(EXIT_FAILURE);
		}
	}

	void sendName() {
		string client_name;
		{
			lock_guard<mutex> lock(console);
			getline(cin, client_name);
			while (client_name.empty()) {
				getline(cin, client_name);
			}
			database = database / path(client_name) / path(to_string(socket));
			create_directories(database);
		}		
		Sending::sendMessage(socket, client_name);
	}

	void sendRoom() {
		char room;
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
			<< "You should enter such numbers before you input depending on the purpose:\n"
			<< "* 1 - simple message\n"
			<< "* 2 - change room: 2 ROOM_NUMBER\n"
			<< "* 3 - send file: 3 PATH\033[0m" << endl;
	}

	static void getInput(string& input) {
		//lock_guard<mutex> lock(console);
		getline(cin, input);
	}

	static char getType(string& input) {
		if (input.empty() || input.length() < +2)return '0';
		char number;
		stringstream ss(input);
		ss >> number;
		if (number != '1' && number != '2' && number != '3') {
			return '0';
		}
		return number;
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
		if (input[0] == '"' && input[index_end - 1] == '"') file = input.substr(1, index_end - 2);
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
	

	static bool isYes(const string& decision) {
		unordered_set<string> possible_yes = { "yes", "y", "yeah", "yep" };
		return possible_yes.find(ToUpper(decision)) != possible_yes.end();
	}
};


class InputHandler {
	mutex input_mutex;
	condition_variable cv;
	bool waiting_for_response = false;
	string input="";


public:
	string getYesNoResponse() {
		lock_guard<mutex> lock(input_mutex);
		waiting_for_response = true;
		cout << "Type yes/no: ";
		getline(cin, input);
		waiting_for_response = false;
		cv.notify_one();
		return input;
	}

	string getRegularMessage() {
		unique_lock<mutex> lock(input_mutex);
		cv.wait(lock, [this] {return !waiting_for_response; });
		getline(cin, input);
		return input;
	}
};

class MessageHandler {
	const SOCKET socket;

public:

	MessageHandler(const SOCKET& s) :socket(s) {};

	void sendSimpleMessage(const string& input, char tag)const {
		Sending::sendOneByte(socket, tag);
		Sending::sendMessage(socket, input);
	}

	void changeRoom(const string& input)const {
		if (!InputParser::stringConatinsOnlyDigits(input)) {
			Print("\033[95mIncorrect input!");
			return;
		}
		sendSimpleMessage(input, '2');
		string response = Receiving::receiveResponse(socket);
		Print("\033[94m" + response);
	}

	void sendFile(const string& input)const {
		path file = InputParser::GetFileFromInput(input);
		if (InputParser::isIncorrectFile(file)) {
			Print("\033[95mIncorrect path!");
			return;
		}
		sendSimpleMessage(file.string(), '3');
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
	bool stop = false;
	InputHandler& handler;

	void receiveMessages()  {
		while (true) {
			if (stop)break;
			char tag = Receiving::receiveOneByte(socket);
			while (tag == 0) {
				Receiving::receiveOneByte(socket);
			}
			string message = Receiving::receiveResponse(socket);
			if (message.empty()) {
				Print("\033[94mServer disconnected!");
				exit(1);
			}
			Print("\033[93m" + message);
			lock_guard<mutex> lock(console);
			
			if (tag==0x02) {
				string userResponse = handler.getYesNoResponse();
				if (InputParser::isYes(userResponse)) {
					Sending::sendOneByte(socket, '2');
					int res = Receiving::receiveFile(socket);
					if (res == 1)Sending::sendMessage(socket, "Received!");
					else Sending::sendMessage(socket, "Failed to receive!");
				}else Sending::sendOneByte(socket, '1');
			}
		}
	}

public:

	Receiver(const SOCKET& s, InputHandler& h):socket(s), handler(h) {
		receiver = thread(&Receiver::receiveMessages, this);
		
	}

	~Receiver() {
		stop = true;
		receiver.join();
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
	InputHandler input_handler;
	MessageHandler handler(client_socket);
	Receiver receiver(client_socket, input_handler);
	string input;
	getline(cin, input);
	input = input_handler.getRegularMessage();
	while (true) {
		if (InputParser::ToUpper(input) == "EXIT") {
			//client.SendMessageToServer("\033[91mClient decided to terminate the connection.\033[0m");
			break;
		}	
		char tag = InputParser::getType(input);
		string message_itself = InputParser::getMessageItself(input);
		if (tag == '0') Print("\033[95mInvalid input!");
		else if (tag == '1')handler.sendSimpleMessage(message_itself, tag);
		else if (tag == '2')handler.changeRoom(message_itself);
		else  handler.sendFile(message_itself);
		input = input_handler.getRegularMessage();
	}
	closesocket(client_socket);
	WSACleanup();
}

