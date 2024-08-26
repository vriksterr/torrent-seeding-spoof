#include <iostream>
#include <filesystem>
#include <set>
#include <string>
#include <thread>
#include <chrono>
#include <map>
#include <atomic>
#include <windows.h>

namespace fs = std::filesystem;

std::atomic<bool> stopFlag(false);

void runCommand(const std::string& filePath) {
    std::string command = ".\\ratio-spoof.exe -t \"" + filePath + "\" -d 100% -ds 0kbps -u 0% -us 1kbps -c qbit-4.6.6";

    STARTUPINFO si = { sizeof(si) };
    PROCESS_INFORMATION pi;

    // Initialize PROCESS_INFORMATION
    ZeroMemory(&pi, sizeof(pi));

    // Create the process
    if (!CreateProcess(
        NULL,                 // No module name (use command line)
        const_cast<LPSTR>(command.c_str()), // Command line
        NULL,                 // Process handle not inheritable
        NULL,                 // Thread handle not inheritable
        FALSE,                // Set handle inheritance to FALSE
        0,                    // No creation flags
        NULL,                 // Use parent's environment block
        NULL,                 // Use parent's starting directory 
        &si,                  // Pointer to STARTUPINFO structure
        &pi                   // Pointer to PROCESS_INFORMATION structure
    )) {
        std::cerr << "CreateProcess failed for: " << filePath << std::endl;
        return;
    }

    // Close process and thread handles
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
}

void terminateProcess(DWORD processId) {
    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, processId);
    if (hProcess) {
        TerminateProcess(hProcess, 1);
        CloseHandle(hProcess);
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
    std::map<fs::path, DWORD> runningProcesses;
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

                    runCommand(entry.path().string());
                    
                    // Assuming the process ID is not tracked here, you might need additional tracking if you want to manage it
                    // For example, if you want to track the PID of the process created by CreateProcess, you need to handle it in the `runCommand` function.
                    // For simplicity, it's not handled here.
                    
                    printRunningFiles(knownFiles);
                }
            }
        }

        // Check for removed files and stop corresponding processes
        for (auto it = knownFiles.begin(); it != knownFiles.end();) {
            if (currentFiles.find(*it) == currentFiles.end()) {
                // Terminate associated process if needed
                // For simplicity, process termination is not handled in this example.
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
    for (const auto& [filePath, processId] : runningProcesses) {
        terminateProcess(processId);
    }
}

int main() {
    fs::path dirPath = "C:\\path\\to\\your\\torrents"; // Replace with your directory path
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
