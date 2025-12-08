#include <iostream>
#include <thread>
#include <vector>
#include <string>
#include <cstring>
#include <map>
#include <fstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <atomic>
#include <mutex>
#include <algorithm>
#include <sstream>
#include <filesystem>

using namespace std;

class CampusClient {
private:
    string campus_name;
    string password;
    string server_ip;
    int tcp_port;
    int udp_port;

    int tcp_socket;
    int udp_socket;
    atomic<bool> running;

    vector<string> received_messages;
    mutex msg_mutex;

    string current_department;
    map<string, vector<string>> department_messages;

    string file_storage_path;

public:
    CampusClient(const string& name, const string& pass,
        const string& ip = "127.0.0.1", int tcp_port = 8080, int udp_port = 8081)
        : campus_name(name), password(pass), server_ip(ip),
        tcp_port(tcp_port), udp_port(udp_port), running(false) {

        initializeDepartments();

        file_storage_path = "./files_" + campus_name + "/";
        filesystem::create_directories(file_storage_path);
    }

    bool initialize() {
        tcp_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (tcp_socket < 0) {
            cerr << "Error creating TCP socket" << endl;
            return false;
        }

        udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
        if (udp_socket < 0) {
            cerr << "Error creating UDP socket" << endl;
            return false;
        }

        sockaddr_in server_addr;
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(tcp_port);
        inet_pton(AF_INET, server_ip.c_str(), &server_addr.sin_addr);

        if (connect(tcp_socket, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            cerr << "Error connecting to server" << endl;
            return false;
        }

        string auth_data = "Campus:" + campus_name + ",Pass:" + password;
        if (send(tcp_socket, auth_data.c_str(), auth_data.length(), 0) < 0) {
            cerr << "Error sending authentication data" << endl;
            return false;
        }

        char buffer[1024];
        int bytes_received = recv(tcp_socket, buffer, sizeof(buffer), 0);
        if (bytes_received <= 0) {
            cerr << "Error receiving authentication response" << endl;
            return false;
        }

        buffer[bytes_received] = '\0';
        if (string(buffer) != "AUTH_SUCCESS") {
            cerr << "Authentication failed" << endl;
            return false;
        }

        cout << "Successfully connected to Central Server as " << campus_name << endl;
        return true;
    }

    void start() {
        running = true;

        selectDepartment();

        thread receiver_thread(&CampusClient::receiveMessages, this);
        thread heartbeat_thread(&CampusClient::sendHeartbeat, this);

        userInterface();

        running = false;
        receiver_thread.join();
        heartbeat_thread.join();

        close(tcp_socket);
        close(udp_socket);
    }

private:
    void initializeDepartments() {
        department_messages["Admissions"] = vector<string>();
        department_messages["Academics"] = vector<string>();
        department_messages["IT"] = vector<string>();
        department_messages["Sports"] = vector<string>();
    }

    void selectDepartment() {
        cout << "\n=== " << campus_name << " Campus ===" << endl;
        cout << "Select Your Department:" << endl;
        cout << "1. Admissions" << endl;
        cout << "2. Academics" << endl;
        cout << "3. IT" << endl;
        cout << "4. Sports" << endl;
        cout << "Enter your choice (1-4): ";

        int choice;
        cin >> choice;
        cin.ignore();

        switch (choice) {
        case 1:
            current_department = "Admissions";
            break;
        case 2:
            current_department = "Academics";
            break;
        case 3:
            current_department = "IT";
            break;
        case 4:
            current_department = "Sports";
            break;
        default:
            cout << "Invalid choice! Defaulting to Admissions." << endl;
            current_department = "Admissions";
        }

        cout << "\nWelcome " << current_department << " Department!" << endl;
    }

    void receiveMessages() {
        char buffer[8192];

        while (running) {
            fd_set read_fds;
            FD_ZERO(&read_fds);
            FD_SET(tcp_socket, &read_fds);

            struct timeval timeout;
            timeout.tv_sec = 1;
            timeout.tv_usec = 0;

            int activity = select(tcp_socket + 1, &read_fds, nullptr, nullptr, &timeout);

            if (activity > 0 && FD_ISSET(tcp_socket, &read_fds)) {
                int bytes_received = recv(tcp_socket, buffer, sizeof(buffer), 0);
                if (bytes_received > 0) {
                    buffer[bytes_received] = '\0';
                    string message(buffer);

                    if (message.find("FILE|") == 0) {
                        handleFileTransfer(message);
                    }
                    else if (message.find("MSG from ") == 0) {
                        routeMessageToDepartment(message);
                    }

                    cout << "\n*** New Data Received ***" << endl;
                    cout << "Enter choice: " << flush;
                }
                else {
                    break;
                }
            }

            FD_ZERO(&read_fds);
            FD_SET(udp_socket, &read_fds);

            activity = select(udp_socket + 1, &read_fds, nullptr, nullptr, &timeout);

            if (activity > 0 && FD_ISSET(udp_socket, &read_fds)) {
                sockaddr_in server_addr;
                socklen_t server_len = sizeof(server_addr);

                int bytes_received = recvfrom(udp_socket, buffer, sizeof(buffer), 0,
                    (sockaddr*)&server_addr, &server_len);
                if (bytes_received > 0) {
                    buffer[bytes_received] = '\0';
                    string message(buffer);

                    if (message.find("BROADCAST:") == 0) {
                        string broadcast_msg = "System Broadcast: " + message.substr(11);
                        for (auto& dept : department_messages) {
                            dept.second.push_back(broadcast_msg);
                        }

                        cout << "\n*** System Broadcast Received ***" << endl;
                        cout << "Enter choice: " << flush;
                    }
                }
            }
        }
    }

    void handleFileTransfer(const string& file_data) {
        if (file_data.find("FILE|") != 0) {
            cerr << "Invalid file format " << endl;
            return;
        }

        vector<string> parts;
        size_t start = 5;
        size_t end = file_data.find('|', start);

        while (end != string::npos) {
            parts.push_back(file_data.substr(start, end - start));
            start = end + 1;
            end = file_data.find('|', start);

            if (parts.size() >= 3) {
                break;
            }
        }

        if (start < file_data.length()) {
            parts.push_back(file_data.substr(start));
        }

        if (parts.size() < 4) {
            cerr << "Invalid file format "<< endl;
            for (size_t i = 0; i < parts.size(); ++i) {
                cout << "DEBUG: Part " << i << ": " << parts[i].substr(0, 50) << "..." << endl;
            }
            return;
        }

        string filename = parts[0];
        string source_campus = parts[1];
        string source_dept = parts[2];
        string file_content = parts[3];

        cout << "DEBUG: File: " << filename << " from " << source_campus << " " << source_dept << endl;
        cout << "DEBUG: Content length: " << file_content.length() << " bytes" << endl;

        string file_path = file_storage_path + "received_from_" + source_campus + "_" + filename;
        ofstream file(file_path, ios::binary);
        if (file.is_open()) {
            file.write(file_content.c_str(), file_content.length());
            file.close();

            string notification = "File received: " + filename + " from " +
                source_campus + " " + source_dept + " (saved as " + file_path + ")";

            if (department_messages.find(source_dept) != department_messages.end()) {
                department_messages[source_dept].push_back(notification);
            }

            department_messages[current_department].push_back(notification);

            cout << "\n*** File Received: " << filename << " from " << source_campus << " ***" << endl;
            cout << "*** Saved as: " << file_path << " ***" << endl;
        }
        else {
            cerr << "Error saving received file: " << filename << " to " << file_path << endl;
        }
    }

    void routeMessageToDepartment(const string& message) {
        if (message.find("MSG from ") == 0) {
            size_t from_pos = 9;
            size_t dept_pos = message.find(' ', from_pos);
            size_t msg_pos = message.find(' ', dept_pos + 1);

            if (dept_pos != string::npos && msg_pos != string::npos) {
                string source_campus = message.substr(from_pos, dept_pos - from_pos);
                string target_dept = message.substr(dept_pos + 1, msg_pos - dept_pos - 1);
                string actual_message = message.substr(msg_pos + 1);

                string formatted_message = "From " + source_campus + " " + target_dept +
                    ": " + actual_message;

                if (department_messages.find(target_dept) != department_messages.end()) {
                    department_messages[target_dept].push_back(formatted_message);
                }

                {
                    lock_guard<mutex> lock(msg_mutex);
                    received_messages.push_back(formatted_message);
                }
            }
        }
    }

    void sendHeartbeat() {
        sockaddr_in server_addr;
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(udp_port);
        inet_pton(AF_INET, server_ip.c_str(), &server_addr.sin_addr);

        while (running) {
            string heartbeat = "HEARTBEAT:" + campus_name;
            sendto(udp_socket, heartbeat.c_str(), heartbeat.length(), 0,
                (sockaddr*)&server_addr, sizeof(server_addr));

            this_thread::sleep_for(chrono::seconds(10));
        }
    }

    void userInterface() {
        cout << "\n=== " << campus_name << " Campus - " << current_department << " Department ===" << endl;
        cout << "Available Campuses: Lahore, Karachi, Peshawar, CFD, Multan" << endl;
        cout << "Available Departments: Admissions, Academics, IT, Sports" << endl;

        while (running) {
            cout << "\nDepartment Menu:" << endl;
            cout << "1. Send message to another campus department" << endl;
            cout << "2. View messages for my department (" << current_department << ")" << endl;
            cout << "3. Send message to another department in this campus" << endl;
            cout << "4. View inter-campus messages" << endl;
            cout << "5. Send file to another campus department" << endl;
            cout << "6. List received files" << endl;
            cout << "7. Switch Department" << endl;
            cout << "8. Exit" << endl;
            cout << "Enter your choice: ";

            int choice;
            cin >> choice;
            cin.ignore();

            switch (choice) {
            case 1:
                sendInterCampusMessage();
                break;
            case 2:
                viewDepartmentMessages();
                break;
            case 3:
                sendIntraCampusMessage();
                break;
            case 4:
                viewAllMessages();
                break;
            case 5:
                sendFileToCampus();
                break;
            case 6:
                listReceivedFiles();
                break;
            case 7:
                switchDepartment();
                break;
            case 8:
                running = false;
                break;
            default:
                cout << "Invalid choice!" << endl;
            }
        }
    }

    void sendInterCampusMessage() {
        string target_campus, target_dept, message;

        cout << "Enter target campus: ";
        getline(cin, target_campus);

        cout << "Enter target department: ";
        getline(cin, target_dept);

        cout << "Enter your message: ";
        getline(cin, message);

        string full_message = "send " + target_campus + " " + target_dept + " " + message;

        if (send(tcp_socket, full_message.c_str(), full_message.length(), 0) < 0) {
            cerr << "Error sending message" << endl;
        }
        else {
            cout << "Message sent to " << target_campus << " " << target_dept << " successfully!" << endl;
        }
    }

    void sendFileToCampus() {
        string target_campus, target_dept, file_path;

        cout << "Enter target campus: ";
        getline(cin, target_campus);

        cout << "Enter target department: ";
        getline(cin, target_dept);

        cout << "Enter file path to send: ";
        getline(cin, file_path);

        ifstream file(file_path, ios::binary);
        if (!file.is_open()) {
            cerr << "Error: Cannot open file " << file_path << endl;
            return;
        }

        file.seekg(0, ios::end);
        size_t file_size = file.tellg();
        file.seekg(0, ios::beg);

        if (file_size > 4096) {
            cerr << "Error: File too large. Maximum size is 4KB." << endl;
            file.close();
            return;
        }

        string file_content;
        file_content.resize(file_size);
        file.read(&file_content[0], file_size);
        file.close();

        string filename = file_path.substr(file_path.find_last_of("/\\") + 1);

        replace(filename.begin(), filename.end(), ':', '_');

        cout << "DEBUG: Sending file: " << filename << " (" << file_size << " bytes)" << endl;
        cout << "DEBUG: Target: " << target_campus << " " << target_dept << endl;

        string file_transfer_msg = "sendfile|" + target_campus + "|" + target_dept + "|" +
            filename + "|" + file_content;

        if (send(tcp_socket, file_transfer_msg.c_str(), file_transfer_msg.length(), 0) < 0) {
            cerr << "Error sending file" << endl;
        }
        else {
            cout << "File " << filename << " sent to " << target_campus << " " << target_dept << " successfully!" << endl;
        }
    }

    void sendIntraCampusMessage() {
        string target_dept, message;

        cout << "Available departments in " << campus_name << ": ";
        for (const auto& dept : department_messages) {
            cout << dept.first << " ";
        }
        cout << endl;

        cout << "Enter target department: ";
        getline(cin, target_dept);

        if (department_messages.find(target_dept) == department_messages.end()) {
            cout << "Invalid department!" << endl;
            return;
        }

        cout << "Enter your message: ";
        getline(cin, message);

        string formatted_message = "Internal from " + current_department + ": " + message;

        department_messages[target_dept].push_back(formatted_message);

        cout << "Message sent to " << target_dept << " department successfully!" << endl;

        string sent_message = "To " + target_dept + ": " + message;
        department_messages[current_department].push_back("(Sent) " + sent_message);
    }

    void listReceivedFiles() {
        cout << "\n=== Received Files in " << file_storage_path << " ===" << endl;

        if (!filesystem::exists(file_storage_path)) {
            cout << "No files received yet." << endl;
            return;
        }

        int file_count = 0;
        for (const auto& entry : filesystem::directory_iterator(file_storage_path)) {
            if (entry.is_regular_file()) {
                cout << ++file_count << ". " << entry.path().filename() << " ("
                    << entry.file_size() << " bytes)" << endl;
            }
        }

        if (file_count == 0) {
            cout << "No files received yet." << endl;
        }
    }

    void viewDepartmentMessages() {
        cout << "\n=== Messages for " << current_department << " Department ===" << endl;

        const auto& messages = department_messages[current_department];
        if (messages.empty()) {
            cout << "No messages for your department." << endl;
            return;
        }

        for (size_t i = 0; i < messages.size(); ++i) {
            cout << i + 1 << ". " << messages[i] << endl;
        }
    }

    void viewAllMessages() {
        lock_guard<mutex> lock(msg_mutex);

        if (received_messages.empty()) {
            cout << "No inter-campus messages received." << endl;
            return;
        }

        cout << "\n=== All Inter-Campus Messages ===" << endl;
        for (size_t i = 0; i < received_messages.size(); ++i) {
            cout << i + 1 << ". " << received_messages[i] << endl;
        }
    }

    void switchDepartment() {
        cout << "\nSwitching Department..." << endl;
        selectDepartment();
        cout << "Now operating as " << current_department << " Department" << endl;
    }
};

int main(int argc, char* argv[]) {
    if (argc != 3) {
        cout << "Usage: " << argv[0] << " <campus_name> <password>" << endl;
        cout << "Available campuses: Lahore, Karachi, Peshawar, CFD, Multan" << endl;
        cout << "Corresponding passwords: NU-LHR-123, NU-KHI-456, NU-PEW-789, NU-CFD-101, NU-MUL-112" << endl;
        return 1;
    }

    CampusClient client(argv[1], argv[2]);

    if (!client.initialize()) {
        return -1;
    }

    client.start();
    return 0;
}