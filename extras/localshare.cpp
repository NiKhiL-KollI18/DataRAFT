#include <fstream>
#include <iostream>
#include <vector>
#include <winsock.h>
#include <ws2tcpip.h>
#include <filesystem>
#include <chrono>

using namespace std;
using namespace chrono;
using namespace filesystem;

struct filemeta {
    long long file_size;
    char file_name[256]{};
    char extension[16]{};

    filemeta(long long file_size , const string& file_name , const string& extension) {
        this->file_size = file_size;

        strncpy(this->file_name , file_name.c_str() , sizeof(this->file_name) - 1);
        this->file_name[sizeof(this->file_name) - 1] = '\0';

        strncpy(this->extension , extension.c_str() , sizeof(this->extension) - 1);
        this->extension[sizeof(this->extension) - 1] = '\0';
    }
};

string generate_received_filepath(const string &file_name) {
    string download_folder = "../received/";

    create_directories(download_folder);

    string filepath = download_folder + file_name;

    return filepath;
}

void run_receiver(int port) {
    SOCKET server = socket(AF_INET , SOCK_STREAM , 0);

    //config
    sockaddr_in server_address{};
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port);
    server_address.sin_addr.s_addr = INADDR_ANY;

    //binding to port and listening
    int bind_res = bind(server , reinterpret_cast<struct sockaddr *>(&server_address) , sizeof(server_address));
    cout << "receiver : listening on port : " << port << endl;
    cout << bind_res << endl;
    listen(server , 1);

    sockaddr_in client_address{};
    int client_addr_len = sizeof(client_address);

    //accept incoming request
    SOCKET client = accept(server , reinterpret_cast<struct sockaddr*>(&client_address) , &client_addr_len);

    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET , & client_address.sin_addr , client_ip , INET_ADDRSTRLEN);

    cout << ">>> INCOMING CONNECTION <<<\n";
    cout << "from ipv4_address : " << client_ip << endl;
    cout << "connect to " << client_ip << "?(y / n)" << endl;

    char response;
    cin >> response;

    if (response == 'n' || response == 'N') {
        cout << "connection rejected!" << endl;
        closesocket(server);
        closesocket(client);
        return;
    }

    cout << "connected to sender" << endl;

    //receiving file

    char meta_buffer[sizeof(filemeta)];

    int meta_bytes_received = 0;
    int meta_bytes_left = sizeof(filemeta);

    while (meta_bytes_received < sizeof(filemeta)) {
        int curr_recv = recv(client , meta_buffer + meta_bytes_received , meta_bytes_left , 0);

        if (curr_recv <= 0) {
            cout << "connection dropped" << endl;
            closesocket(client);
            closesocket(server);
            return;
        }

        meta_bytes_received += curr_recv;
        meta_bytes_left -= curr_recv;
    }

    auto metadata = reinterpret_cast<filemeta*>(meta_buffer);

    cout << "receiving file : " << "\n";
    cout << "file name : " << metadata->file_name << "\n";
    cout << "type : " << metadata->extension << "\n";
    cout << "size : " << metadata->file_size << "KB \n";

    cout << endl;

    string filepath = generate_received_filepath(metadata->file_name);

    ofstream outfile(filepath , ios::binary);

    vector<char> buffer(65536); //64bit chunk buckets
    int bytes_received;

    auto start_time = high_resolution_clock::now();

    cout << "receiving file..." << endl;
    while ((bytes_received = recv(client, buffer.data(), static_cast<int>(buffer.size()), 0)) > 0) {
        outfile.write(buffer.data(), bytes_received);
    }

    auto end_time = high_resolution_clock::now();
    cout << "file received successfully." << endl;
    cout << "time taken : " << static_cast<double>(duration_cast<milliseconds>(end_time - start_time).count()) / 1000.0 << "sec" << endl;

    outfile.close();
    closesocket(server);
    closesocket(client);
}

void run_sender(const string &ip , int port , const string &filepath) {
    SOCKET sender = socket(AF_INET , SOCK_STREAM , 0);
    sockaddr_in server_address{};
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port);
    server_address.sin_addr.s_addr = inet_addr(ip.c_str());

    if (connect(sender ,
        reinterpret_cast<struct sockaddr *>(&server_address) ,
        sizeof(server_address)
        ) == SOCKET_ERROR) {
        cout << "connection failed" << endl;
        return;
    }
    cout << "connected to receiver." << endl;
    ifstream infile(filepath , ios::binary);
    if (!infile) {
        cout << "could not open file!" << endl;
        return;
    }
    cout << "sending file..." << endl;

    vector<char> buffer(65536);

    path p(filepath);

    auto file_size_in_bytes = file_size(p);
    long long file_size = static_cast<long long>(file_size_in_bytes) / 1024;
    string file_name = p.filename().string();
    string extension = p.extension().string();

    filemeta metadata(file_size , file_name , extension);

    //sending metadata
    send(sender , reinterpret_cast<char*>(&metadata) , sizeof(metadata) , 0);

    auto start_time = high_resolution_clock::now();
    while (infile.read(buffer.data() , static_cast<int>(buffer.size())) || infile.gcount() > 0) {
        send(sender , buffer.data() , static_cast<int>(infile.gcount()), 0);
    }
    auto end_time = high_resolution_clock::now();

    cout << "file sent successfully..." << endl;
    cout << "time taken : " << static_cast<double>(duration_cast<milliseconds>(end_time - start_time).count()) / 1000.0 << "sec" << endl;
}

int main() {
    //requesting networking resources
    WSADATA wsa_data;
    WSAStartup(MAKEWORD(2 , 2) , &wsa_data);

    int choice;
    cout << "select mode : \n" << " 1.receiver \n 2.sender\n your choice : " << endl;
    cin >> choice;

    if (choice == 1) {
        run_receiver(5000);
    }
    else if (choice == 2) {
        string filepath;
        cout << "please enter the exact filepath to the file you want to send : " << endl;
        cin >> filepath;

        run_sender("127.0.0.1" , 5000 , filepath);
    }
    else {
         cout << "invalid choice!" << endl;
    }
    WSACleanup();
}//
// Created by nikhi on 3/15/2026.
//