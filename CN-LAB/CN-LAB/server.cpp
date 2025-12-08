#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <map>
#include <string>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <algorithm>
#include <ctime>

using namespace std;

struct CampusClient {
    int socket_fd;
    string campus_name;
    string status;
    string last_seen;
    sockaddr_in udp_addr;
};

class CentralServer {
private:
    int tcp_socket;
    int udp_socket;
    int tcp_port;
    int udp_port;
    vector<CampusClient> clients;
    mutex clients_mutex;
    bool running;

    map<string, string> campus_credentials = {
        {"Lahore", "NU-LHR-123"},
        {"Karachi", "NU-KHI-456"},
        {"Peshawar", "NU-PEW-789"},
        {"CFD", "NU-CFD-101"},
        {"Multan", "NU-MUL-112"}
    };

public:
    CentralServer(int tcp_port = 8080, int udp_port = 8081)
        : tcp_port(tcp_port), udp_port(udp_port), running(false) {}

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

        int opt = 1;
        setsockopt(tcp_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in tcp_addr;
        tcp_addr.sin_family = AF_INET;
        tcp_addr.sin_addr.s_addr = INADDR_ANY;
        tcp_addr.sin_port = htons(tcp_port);

        if (bind(tcp_socket, (sockaddr*)&tcp_addr, sizeof(tcp_addr)) < 0) {
            cerr << "Error binding TCP socket" << endl;
            return false;
        }

        sockaddr_in udp_addr;
        udp_addr.sin_family = AF_INET;
        udp_addr.sin_addr.s_addr = INADDR_ANY;
        udp_addr.sin_port = htons(udp_port);

        if (bind(udp_socket, (sockaddr*)&udp_addr, sizeof(udp_addr)) < 0) {
            cerr << "Error binding UDP socket" << endl;
            return false;
        }

        if (listen(tcp_socket, 5) < 0) {
            cerr << "Error listening on TCP socket" << endl;
            return false;
        }

        cout << "Central Server started successfully!" << endl;
        cout << "TCP Server listening on port " << tcp_port << endl;
        cout << "UDP Server listening on port " << udp_port << endl;
        return true;
    }

    void start() {
        running = true;

        thread tcp_thread(&CentralServer::handleTCPConnections, this);
        thread udp_thread(&CentralServer::handleUDPConnections, this);
        thread admin_thread(&CentralServer::adminInterface, this);

        tcp_thread.join();
        udp_thread.join();
        admin_thread.join();
    }

private:
    void handleTCPConnections() {
        while (running) {
            sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);

            int client_socket = accept(tcp_socket, (sockaddr*)&client_addr, &client_len);
            if (client_socket < 0) {
                cerr << "Error accepting connection" << endl;
                continue;
            }

            thread client_thread(&CentralServer::handleClient, this, client_socket, client_addr);
            client_thread.detach();
        }
    }

    void handleClient(int client_socket, sockaddr_in client_addr) {
        char buffer[8192];
        string campus_name;
        bool authenticated = false;

        int bytes_received = recv(client_socket, buffer, sizeof(buffer), 0);
        if (bytes_received > 0) {
            buffer[bytes_received] = '\0';
            string auth_data(buffer);

            size_t campus_pos = auth_data.find("Campus:");
            size_t pass_pos = auth_data.find("Pass:");

            if (campus_pos != string::npos && pass_pos != string::npos) {
                campus_name = auth_data.substr(campus_pos + 7, pass_pos - campus_pos - 8);
                string password = auth_data.substr(pass_pos + 5);

                if (campus_credentials.find(campus_name) != campus_credentials.end() &&
                    campus_credentials[campus_name] == password) {

                    authenticated = true;
                    send(client_socket, "AUTH_SUCCESS", 12, 0);

                    CampusClient client;
                    client.socket_fd = client_socket;
                    client.campus_name = campus_name;
                    client.status = "Online";
                    updateLastSeen(client);

                    {
                        lock_guard<mutex> lock(clients_mutex);
                        clients.push_back(client);
                    }

                    cout << "[" << getCurrentTime() << "] " << campus_name
                        << " authenticated successfully from "
                        << inet_ntoa(client_addr.sin_addr) << endl;
                }
            }
        }

        if (!authenticated) {
            send(client_socket, "AUTH_FAILED", 11, 0);
            close(client_socket);
            return;
        }

        while (running) {
            bytes_received = recv(client_socket, buffer, sizeof(buffer), 0);
            if (bytes_received <= 0) {
                break;
            }

            buffer[bytes_received] = '\0';
            string message(buffer);

            if (message.find("send ") == 0) {
                routeMessage(message, campus_name);
            }
            else if (message.find("sendfile|") == 0) {
                routeFileTransfer(message, campus_name);
            }
        }

        {
            lock_guard<mutex> lock(clients_mutex);
            clients.erase(remove_if(clients.begin(), clients.end(),
                [client_socket](const CampusClient& c) { return c.socket_fd == client_socket; }),
                clients.end());
        }

        cout << "[" << getCurrentTime() << "] " << campus_name << " disconnected" << endl;
        close(client_socket);
    }

    void routeMessage(const string& message, const string& source_campus) {
        size_t first_space = message.find(' ');
        size_t second_space = message.find(' ', first_space + 1);
        size_t third_space = message.find(' ', second_space + 1);

        if (first_space == string::npos || second_space == string::npos ||
            third_space == string::npos) {
            return;
        }

        string target_campus = message.substr(first_space + 1, second_space - first_space - 1);
        string target_dept = message.substr(second_space + 1, third_space - second_space - 1);
        string actual_message = message.substr(third_space + 1);

        lock_guard<mutex> lock(clients_mutex);
        for (auto& client : clients) {
            if (client.campus_name == target_campus) {
                string formatted_message = "MSG from " + source_campus + " " +
                    target_dept + " " + actual_message;
                send(client.socket_fd, formatted_message.c_str(), formatted_message.length(), 0);

                cout << "[" << getCurrentTime() << "] Routed message from "
                    << source_campus << " " << " to " << target_campus << " " << target_dept << endl;
                break;
            }
        }
    }

    void routeFileTransfer(const string& message, const string& source_campus) {
        cout << "DEBUG: Routing file transfer from " << source_campus << endl;

        if (message.find("sendfile|") != 0) {
            cerr << "DEBUG: Invalid file transfer format - missing sendfile| prefix" << endl;
            return;
        }

        vector<string> parts;
        size_t start = 9;
        size_t end = message.find('|', start);

        while (end != string::npos) {
            parts.push_back(message.substr(start, end - start));
            start = end + 1;
            end = message.find('|', start);

            if (parts.size() >= 3) {
                break;
            }
        }

        if (start < message.length()) {
            parts.push_back(message.substr(start));
        }

        if (parts.size() < 4) {
            cerr << "DEBUG: Invalid file transfer format - not enough parts. Got " << parts.size() << " parts." << endl;
            return;
        }

        string target_campus = parts[0];
        string target_dept = parts[1];
        string filename = parts[2];
        string file_content = parts[3];

        cout << "DEBUG: File: " << filename << " to " << target_campus << " " << target_dept << endl;
        cout << "DEBUG: Content length: " << file_content.length() << " bytes" << endl;

        lock_guard<mutex> lock(clients_mutex);
        for (auto& client : clients) {
            if (client.campus_name == target_campus) {
                string file_transfer_msg = "FILE|" + filename + "|" +
                    source_campus + "|" + target_dept + "|" + file_content;

                int sent = send(client.socket_fd, file_transfer_msg.c_str(), file_transfer_msg.length(), 0);
                if (sent > 0) {
                    cout << "[" << getCurrentTime() << "] Routed file " << filename
                        << " (" << file_content.length() << " bytes) from " << source_campus
                        << " to " << target_campus << " " << target_dept << endl;
                }
                else {
                    cerr << "Error sending file to " << target_campus << endl;
                }
                break;
            }
        }
    }

    void handleUDPConnections() {
        char buffer[1024];
        sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        while (running) {
            int bytes_received = recvfrom(udp_socket, buffer, sizeof(buffer), 0,
                (sockaddr*)&client_addr, &client_len);

            if (bytes_received > 0) {
                buffer[bytes_received] = '\0';
                string heartbeat(buffer);

                if (heartbeat.find("HEARTBEAT:") == 0) {
                    string campus_name = heartbeat.substr(10);

                    lock_guard<mutex> lock(clients_mutex);
                    for (auto& client : clients) {
                        if (client.campus_name == campus_name) {
                            client.status = "Online";
                            client.udp_addr = client_addr;
                            updateLastSeen(client);
                            break;
                        }
                    }
                }
            }
        }
    }

    void adminInterface() {
        cout << "\nAdmin Interface Started!" << endl;
        cout << "Commands: " << endl;
        cout << "  list - Show connected campuses" << endl;
        cout << "  broadcast <message> - Broadcast message to all campuses" << endl;
        cout << "  exit - Shutdown server" << endl;

        string command;
        while (running) {
            cout << "\nadmin> ";
            getline(cin, command);

            if (command == "list") {
                listConnectedCampuses();
            }
            else if (command.find("broadcast ") == 0) {
                broadcastMessage(command.substr(10));
            }
            else if (command == "exit") {
                running = false;
                shutdown(tcp_socket, SHUT_RDWR);
                shutdown(udp_socket, SHUT_RDWR);
                close(tcp_socket);
                close(udp_socket);
                break;
            }
            else {
                cout << "Unknown command" << endl;
            }
        }
    }

    void listConnectedCampuses() {
        lock_guard<mutex> lock(clients_mutex);
        cout << "\nConnected Campuses:" << endl;
        cout << "-------------------" << endl;
        for (const auto& client : clients) {
            cout << "Campus: " << client.campus_name << endl;
            cout << "Status: " << client.status << endl;
            cout << "Last Seen: " << client.last_seen << endl;
            cout << "-------------------" << endl;
        }
    }

    void broadcastMessage(const string& message) {
        lock_guard<mutex> lock(clients_mutex);

        for (const auto& client : clients) {
            string broadcast_msg = "BROADCAST: " + message;
            sendto(udp_socket, broadcast_msg.c_str(), broadcast_msg.length(), 0,
                (sockaddr*)&client.udp_addr, sizeof(client.udp_addr));
        }

        cout << "[" << getCurrentTime() << "] Broadcast message sent: " << message << endl;
    }

    void updateLastSeen(CampusClient& client) {
        time_t now = time(0);
        client.last_seen = ctime(&now);
        client.last_seen.erase(client.last_seen.find('\n'));
    }

    string getCurrentTime() {
        time_t now = time(0);
        string time_str = ctime(&now);
        time_str.erase(time_str.find('\n'));
        return time_str;
    }
};

int main() {
    CentralServer server;

    if (!server.initialize()) {
        return -1;
    }

    server.start();
    return 0;
}