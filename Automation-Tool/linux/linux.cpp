#include <iostream>
#include <filesystem>
#include <set>
#include <string>
#include <thread>
#include <chrono>
#include <map>
#include <atomic>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>

namespace fs = std::filesystem;

std::atomic<bool> stopFlag(false);

pid_t runCommand(const std::string& filePath) {
    pid_t pid = fork();

    if (pid == -1) {
        std::cerr << "Failed to fork process for: " << filePath << std::endl;
        return -1;
    }

    if (pid == 0) { // Child process
        // Redirect stdout and stderr to /dev/null
        freopen("/dev/null", "w", stdout);
        freopen("/dev/null", "w", stderr);

        std::string command = "./ratio-spoof -t \"" + filePath + "\" -d 100% -ds 0kbps -u 0% -us 1kbps -c qbit-4.6.6";
        execl("/bin/sh", "sh", "-c", command.c_str(), (char *)NULL);

        _exit(127); // Should not reach here if exec is successful
    }

    return pid; // Parent process returns child's PID
}

void terminateProcess(pid_t pid) {
    if (pid > 0) {
        kill(pid, SIGTERM); // Send termination signal
        waitpid(pid, NULL, 0); // Wait for the child process to terminate
    }
}

void printRunningFiles(const std::set<fs::path>& runningFiles) {
    std::cout << "\033[2J\033[1;1H";  // Clear console
    std::cout << "Currently running commands for the following files:\n";
    for (const auto& file : runningFiles) {
        std::cout << " - " << file.string() << std::endl;
    }
}

void monitorDirectory(const fs::path& dirPath) {
    std::map<fs::path, pid_t> runningProcesses;
    std::set<fs::path> knownFiles;

    while (!stopFlag) {
        std::set<fs::path> currentFiles;

        // Scan directory for .torrent files
        for (const auto& entry : fs::directory_iterator(dirPath)) {
            if (entry.is_regular_file() && entry.path().extension() == ".torrent") {
                currentFiles.insert(entry.path());

                // If file is new, schedule the command to be run after a delay
                if (knownFiles.find(entry.path()) == knownFiles.end()) {
                    knownFiles.insert(entry.path());

                    // Delay for 5 seconds before executing the command
                    std::this_thread::sleep_for(std::chrono::seconds(4));

                    pid_t processPid = runCommand(entry.path().string());
                    runningProcesses[entry.path()] = processPid;
                    printRunningFiles(knownFiles);
                }
            }
        }

        // Check for removed files and stop corresponding processes
        for (auto it = knownFiles.begin(); it != knownFiles.end();) {
            if (currentFiles.find(*it) == currentFiles.end()) {
                terminateProcess(runningProcesses[*it]);
                runningProcesses.erase(*it);
                it = knownFiles.erase(it);
                printRunningFiles(knownFiles);
            } else {
                ++it;
            }
        }

        // Wait for a second before checking again
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // Clean up all processes on exit
    for (const auto& [filePath, processPid] : runningProcesses) {
        terminateProcess(processPid);
    }
}

int main() {
    fs::path dirPath = "/path/to/your/torrents"; // Replace with your directory path
    std::thread monitorThread(monitorDirectory, dirPath);

    // Run until user stops it
    std::cout << "Press Enter to stop monitoring..." << std::endl;
    std::cin.get();
    stopFlag = true;

    if (monitorThread.joinable()) {
        monitorThread.join();
    }

    return 0;
}
