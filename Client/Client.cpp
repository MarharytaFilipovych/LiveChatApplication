#include <iostream>
#include <WinSock2.h>
#include <filesystem>
#include <Ws2tcpip.h>
#include <fstream>
#include <sstream>
#include <thread>
#include <mutex>
#include <unordered_set>
#include <queue>
#pragma comment(lib, "ws2_32.lib")
#define CHUNK_SIZE 1024
using namespace std;
using namespace filesystem;
path database = path("C:\\Margo\\Parallel and clien-server programming\\Assignment 6\\Client\\database");

void exitWithFailure(const SOCKET& socket) {
		closesocket(socket);
		WSACleanup();
		exit(EXIT_FAILURE);
}

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
		int received = recv(client_socket, (char*)(&value), sizeof(value), 0);
		if (received <= 0)return 0;
		return ntohl(value);
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

	static int receiveFile(const SOCKET& client_socket, const string& file_name) {
		int size_of_file = Receiving::receiveInteger(client_socket);
		if (size_of_file == 0)return 0;
		path path_to_file = database / path(file_name);
		if (exists(path_to_file))return 1;
		ofstream file(path_to_file, ios::binary);
		int i = 0;
		char buffer[CHUNK_SIZE] = { 0 };
		while (i != size_of_file) {
				int bytes_received = recv(client_socket, buffer, CHUNK_SIZE, 0);
				if (bytes_received <= 0 && i != size_of_file) return 0;
				file.write(buffer, bytes_received);
				i += bytes_received;
		}
		file.close();
		return 1;
	}
};


class Registration {
	const SOCKET& socket;

	void SendInitialGreeting() {
		Sending::sendMessage(socket, "Hello, server! This is client:)");
		cout << "\033[95mWaiting for a server to accept us...\033[0m\n";
		char tag = Receiving::receiveOneByte(socket);
		if (tag == 0x05) {
			closesocket(socket);
			WSACleanup();
			exit(EXIT_FAILURE);
		}
		string response = "\033[94m" + Receiving::receiveResponse(socket) + "\033[0m\n";
		cout << response;
		
	}

	void sendName() {
		string client_name;	
		getline(cin, client_name);
		while (client_name.empty()) {
			getline(cin, client_name);
		}
		client_name.erase(remove(client_name.begin(), client_name.end(), ' '), client_name.end());
		database = database / path(client_name) / path(to_string(socket));
		create_directories(database);		
		Sending::sendMessage(socket, client_name);
	}

	void sendRoom() {
		char room;
		cin >> room;
		Sending::sendOneByte(socket, room);
		char confirmation = Receiving::receiveOneByte(socket);
		while (confirmation == 0x00) {		
			cout << "Choose another room number: " << endl;
			cin >> room;	
			Sending::sendOneByte(socket, room);
			confirmation = Receiving::receiveOneByte(socket);
		}
	}

	void getRegistered() {
		SendInitialGreeting();
		sendName();
		char b = Receiving::receiveOneByte(socket);
		if ( b == 0x00)exitWithFailure(socket);
		string buffer = Receiving::receiveResponse(socket);
		if (buffer == "")exitWithFailure(socket);
		buffer = "\033[94m" + buffer + "\033[0m\n";
		cout << buffer;
		sendRoom();
		char tag = Receiving::receiveOneByte(socket);
		if(tag == 0x00)exitWithFailure(socket);
		string register_completed = Receiving::receiveResponse(socket);
		if (register_completed == "")exitWithFailure(socket);
		register_completed = "\033[91m" + register_completed + "\033[0m\n";
		cout << register_completed;
		if (tag == 0x05)exitWithFailure(socket);
	}

public:
	Registration(const SOCKET& client_socket) :socket(client_socket) {
		getRegistered();
	}
};
struct InputParser {

	static void printInstructions() {
		cout << "\033[96mRules:\n"
			<< "You should enter such numbers before your input depending on the purpose:\n"
			<< "* 1 - simple message\n"
			<< "* 2 - change room: 2 ROOM_NUMBER\n"
			<< "* 3 - send file: 3 PATH\033[0m\n";
	}

	static char getType(string& input) {
		if (input.empty() || input.length() < 2)return '0';
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
		unordered_set<string> possible_yes = { "YES", "Y", "YEAH", "YEP" };
		return possible_yes.find(ToUpper(decision)) != possible_yes.end();
	}
};


class Communication {
	const SOCKET& socket;
	condition_variable cv_input, cv_response;
	thread receiver;
	bool stop = false , need_user_response = false, block_input = false, confirmation = false;
	queue<string> files;
	string input;
	mutex m;

	void sendSimpleMessage(const string& input, char tag)const {
		Sending::sendOneByte(socket, tag);
		Sending::sendMessage(socket, input);
	}

	void changeRoom(const string& input) {
		unique_lock<mutex> lock(m);
		confirmation = false;
		if (!InputParser::stringConatinsOnlyDigits(input)) {
			cout << "\033[31mIncorrect input!\033[0m\n";
			return;
		}	
		sendSimpleMessage(input, 0x02);		
		cv_input.wait(lock, [this] {return confirmation ||stop; });
	}

	void sendFile(const string& input)const {
		path file = InputParser::GetFileFromInput(input);
		if (InputParser::isIncorrectFile(file)) {
			cout << "\033[31mIncorrect path!\033[0m\n";
			return;
		}
		sendSimpleMessage(file.string(), 0x03);
		Sending::sendFile(socket, file);
	}

	void Print(string color) {
		string sth = Receiving::receiveResponse(socket);
		sth = color + sth + "\033[0m\n";
		cout << sth;
	}

	void handleTag3() {
		block_input = true;
		int result = Receiving::receiveFile(socket, files.front());
		if (result == 1)cout << "\033[95mFile received\033[0m\n";	
		else cout << "\033[91mFile was not recieved\033[0m\n";
		block_input = false;
		cv_input.notify_one();
		files.pop();
	}

	void handleTag2() {
		need_user_response = true;
		string question = Receiving::receiveResponse(socket);
		if (question == "")exitWithFailure(socket);
		char b = Receiving::receiveOneByte(socket);
		if(b == 0x00)exitWithFailure(socket);
		string file_name = Receiving::receiveResponse(socket);
		if (file_name == "")exitWithFailure(socket);
		string to_print = "\033[92m" + question + file_name + "\033[0m\n";
		cout << to_print;
		files.push(file_name);
		unique_lock<mutex> lock(m);
		cv_response.wait(lock, [this] { return !need_user_response || stop; });	
	}

	void getResponse() {
		while (input.empty())getline(cin, input);
		InputParser::isYes(input) ? sendSimpleMessage(files.front(), 0x04) : sendSimpleMessage("no", 0x04);
		need_user_response = false;
		cv_response.notify_one();
	}


	void getUserInput() {
		while (true) {
			unique_lock<mutex> lock(m);
			cv_input.wait(lock, [this] {return !block_input || stop; });
			if (stop)break;
			lock.unlock();
			getline(cin, input);
			if (need_user_response) getResponse();
			else {
				if (InputParser::ToUpper(input) == "EXIT") {
					Sending::sendOneByte(socket, 0x05);
					break;
				}
				char tag = InputParser::getType(input);
				if (tag == '0') cout << "\033[31mInvalid input!\033[0m\n";
				else if (tag == '1')sendSimpleMessage(InputParser::getMessageItself(input), 0x01);
				else if (tag == '2') changeRoom(InputParser::getMessageItself(input)); 
				else sendFile(InputParser::getMessageItself(input));
			}
		}
	}

	void cleanResources() {
		closesocket(socket);
		WSACleanup();
	}

	void receiveMessages() {
		while (!stop) {
			char tag = Receiving::receiveOneByte(socket);
			switch (tag) {
			case 0x00:
				cout << "The server terminated unexpectedly!" << endl;
				cleanResources();
				exit(EXIT_FAILURE);
			case 0x01:
				Print("\033[93m");
				break;
			case 0x02:
				handleTag2();
				break;
			case 0x03:
				handleTag3();
				break;
			case 0x05:
				Print("\033[94m");
				break;
			case 0x06:
				Print("\033[94m");
				confirmation = true;
				cv_input.notify_one();
				break;
			}
		}
	}
	
	public:
	Communication(const SOCKET& s) :socket(s) {
		getline(cin, input);
		receiver = thread(&Communication::receiveMessages, this);
		getUserInput();
	}

	~Communication() {
		stop = true;
		cv_input.notify_all();
		cv_response.notify_all();
		if (receiver.joinable())receiver.join();
		cleanResources();
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
	Communication communication(client_socket);
	return 0;
}

