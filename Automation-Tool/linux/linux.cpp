#include <iostream>
#include <filesystem>
#include <fstream>
#include <string>
#include <chrono>
#include <thread>
#include <unordered_map>
#include <iomanip>
#include <sstream>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <libtorrent/torrent_info.hpp>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;
using json = nlohmann::json;

const std::string TORRENT_DIR = "/torrent_files";
const std::string QBITTORRENT_API_URL = "http://10.10.10.27:20022/api/v2/torrents/info?hashes=";
const std::string RATIO_SPOOF_CMD = "./ratio-spoof -t ";

// Function to convert hash to a hex string
std::string hashToHexString(const libtorrent::sha1_hash& hash) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (auto c : hash) {
        ss << std::setw(2) << static_cast<int>(c);
    }
    return ss.str();
}

// Function to get the hash from the torrent file
std::string getHashFromTorrentFile(const std::string& filepath) {
    try {
        libtorrent::torrent_info info(filepath);
        std::string hash = hashToHexString(info.info_hash());
        std::cout << "Extracted hash: " << hash << std::endl;
        return hash;
    } catch (const std::exception& e) {
        std::cerr << "Error extracting hash from torrent file: " << e.what() << std::endl;
        return "";
    }
}

// Callback function for CURL to write response data
size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

// Function to check the torrent status
std::string getTorrentStatus(const std::string& hash) {
    CURL* curl;
    CURLcode res;
    std::string readBuffer;

    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();

    if(curl) {
        curl_easy_setopt(curl, CURLOPT_URL, (QBITTORRENT_API_URL + hash).c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        std::cout << "Sending request to URL: " << QBITTORRENT_API_URL + hash << std::endl;
        res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            std::cerr << "CURL Error: " << curl_easy_strerror(res) << std::endl;
            curl_easy_cleanup(curl);
            curl_global_cleanup();
            return "";
        }
        curl_easy_cleanup(curl);
    } else {
        std::cerr << "CURL initialization failed" << std::endl;
        curl_global_cleanup();
        return "";
    }
    curl_global_cleanup();

    std::cout << "Received response: " << readBuffer << std::endl;

    try {
        json jsonResponse = json::parse(readBuffer);
        if (jsonResponse.empty()) {
            std::cerr << "No data received from API" << std::endl;
            return "";
        }
        return jsonResponse[0]["state"];
    } catch (const json::exception& e) {
        std::cerr << "JSON Parsing Error: " << e.what() << std::endl;
        return "";
    }
}

// Function to start the ratio-spoof command
pid_t startRatioSpoof(const std::string& torrentFile) {
    std::string command = RATIO_SPOOF_CMD + "\"" + torrentFile + "\" -d 100% -ds 0kbps -u 0% -us 1kbps -c qbit-4.6.6";
    std::cout << "Running command: " << command << std::endl;

    pid_t pid = fork();
    if (pid == 0) {
        // Child process
        execl("/bin/sh", "sh", "-c", command.c_str(), nullptr);
        std::cerr << "Failed to execute command" << std::endl;
        _exit(1);
    } else if (pid < 0) {
        std::cerr << "Failed to fork process" << std::endl;
    }
    return pid;
}

// Function to terminate the ratio-spoof command
void terminateRatioSpoof(pid_t pid) {
    std::cout << "Terminating process with PID: " << pid << std::endl;
    if (kill(pid, SIGTERM) == -1) {
        std::cerr << "Failed to terminate process" << std::endl;
    }
    waitpid(pid, nullptr, 0); // Wait for the process to terminate
}

// Function to delete the torrent file
void deleteTorrentFile(const std::string& filepath) {
    if (fs::remove(filepath)) {
        std::cout << "Deleted torrent file: " << filepath << std::endl;
    } else {
        std::cerr << "Failed to delete torrent file: " << filepath << std::endl;
    }
}

// Main function to monitor torrent directory and execute commands
void monitorTorrents() {
    std::unordered_map<std::string, std::pair<pid_t, std::string>> activeProcesses; // hash -> (pid, torrentFile)

    while (true) {
        std::cout << "Checking folder: " << TORRENT_DIR << std::endl;
        for (const auto& entry : fs::directory_iterator(TORRENT_DIR)) {
            if (entry.path().extension() == ".torrent") {
                std::string torrentFile = entry.path().string();
                std::cout << "Found torrent file: " << torrentFile << std::endl;

                std::string hash = getHashFromTorrentFile(torrentFile);
                if (hash.empty()) {
                    std::cerr << "Failed to extract hash for file: " << torrentFile << std::endl;
                    continue;
                }

                std::string status = getTorrentStatus(hash);
                if (status == "downloading") {
                    std::cout << "Torrent " << hash << " is downloading" << std::endl;

                    if (activeProcesses.find(hash) == activeProcesses.end()) {
                        pid_t pid = startRatioSpoof(torrentFile);
                        if (pid > 0) {
                            activeProcesses[hash] = {pid, torrentFile};
                        }
                    }
                } else {
                    if (activeProcesses.find(hash) != activeProcesses.end()) {
                        terminateRatioSpoof(activeProcesses[hash].first);
                        deleteTorrentFile(activeProcesses[hash].second); // Delete the torrent file
                        activeProcesses.erase(hash);
                    }
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::seconds(1)); // Check every 1 second
    }
}

int main() {
    std::cout << "Starting torrent monitor" << std::endl;
    monitorTorrents();
    return 0;
}
