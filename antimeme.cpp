/**
 * @file antimeme.cpp
 * @brief Volatile, memory-only Docker credential helper using Windows DPAPI.
 * 
 * This tool securely caches Docker credentials exclusively in RAM using 
 * CryptProtectMemory. It implements a Named Pipe daemon to hold the state 
 * and handles standard Docker credential helper commands (get, store, erase).
 */

#include <windows.h>
#include <dpapi.h>
#include <iostream>
#include <string>
#include <map>
#include <vector>

#pragma comment(lib, "Crypt32.lib")

#define PIPE_NAME "\\\\.\\pipe\\docker_antimeme_pipe"
#define BUFFER_SIZE 4096

/**
 * @brief Secures a string in volatile memory using Windows DPAPI.
 * 
 * CryptProtectMemory requires the data buffer to be a multiple of 
 * CRYPTPROTECTMEMORY_BLOCK_SIZE (16 bytes). This function copies the string
 * into an appropriately sized vector and encrypts it in-place.
 * 
 * @param plaintext The raw string data to encrypt.
 * @return std::vector<BYTE> The encrypted memory blob.
 */
std::vector<BYTE> ProtectMemory(const std::string& plaintext) {
    size_t dataLen = plaintext.length();
    size_t padLen = CRYPTPROTECTMEMORY_BLOCK_SIZE - (dataLen % CRYPTPROTECTMEMORY_BLOCK_SIZE);
    size_t totalLen = dataLen + padLen;

    std::vector<BYTE> buffer(totalLen, 0);
    memcpy(buffer.data(), plaintext.c_str(), dataLen);

    if (!CryptProtectMemory(buffer.data(), (DWORD)totalLen, CRYPTPROTECTMEMORY_SAME_PROCESS)) {
        std::cerr << "CryptProtectMemory failed: " << GetLastError() << std::endl;
        exit(1);
    }
    return buffer;
}

/**
 * @brief Decrypts a protected memory blob back into a plaintext string.
 * 
 * @param protectedData The vector containing the encrypted bytes.
 * @return std::string The decrypted plaintext string.
 */
std::string UnprotectMemory(std::vector<BYTE> protectedData) {
    if (protectedData.empty()) return "";

    if (!CryptUnprotectMemory(protectedData.data(), (DWORD)protectedData.size(), CRYPTPROTECTMEMORY_SAME_PROCESS)) {
        std::cerr << "CryptUnprotectMemory failed: " << GetLastError() << std::endl;
        return "";
    }
    return std::string(reinterpret_cast<char*>(protectedData.data()));
}

/**
 * @brief Runs the persistent named pipe server to hold credentials in RAM.
 * 
 * Loops indefinitely, processing STORE, GET, and ERASE commands from client instances.
 */
void RunDaemon() {
    std::map<std::string, std::vector<BYTE>> volatileVault;
    std::cout << "[*] Antimeme daemon active." << std::endl;

    while (true) {
        HANDLE hPipe = CreateNamedPipeA(
            PIPE_NAME,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            1,
            BUFFER_SIZE,
            BUFFER_SIZE,
            0,
            NULL
        );

        if (hPipe == INVALID_HANDLE_VALUE) {
            std::cerr << "Failed to create pipe." << std::endl;
            Sleep(1000);
            continue;
        }

        if (ConnectNamedPipe(hPipe, NULL) != FALSE) {
            char buffer[BUFFER_SIZE] = { 0 };
            DWORD bytesRead;

            if (ReadFile(hPipe, buffer, BUFFER_SIZE - 1, &bytesRead, NULL)) {
                std::string request(buffer);
                
                if (request.rfind("STORE ", 0) == 0) {
                    // Format: "STORE <url>|<json_payload>"
                    size_t split = request.find('|');
                    if (split != std::string::npos) {
                        std::string url = request.substr(6, split - 6);
                        std::string payload = request.substr(split + 1);
                        volatileVault[url] = ProtectMemory(payload);
                    }
                } 
                else if (request.rfind("GET ", 0) == 0) {
                    // Format: "GET <url>"
                    std::string url = request.substr(4);
                    url.erase(url.find_last_not_of(" \n\r\t") + 1); 
                    
                    if (volatileVault.find(url) != volatileVault.end()) {
                        std::string plaintext = UnprotectMemory(volatileVault[url]);
                        DWORD bytesWritten;
                        WriteFile(hPipe, plaintext.c_str(), (DWORD)plaintext.length(), &bytesWritten, NULL);
                        
                        SecureZeroMemory(&plaintext[0], plaintext.length());
                    }
                }
                else if (request.rfind("ERASE ", 0) == 0) {
                    std::string url = request.substr(6);
                    url.erase(url.find_last_not_of(" \n\r\t") + 1);
                    volatileVault.erase(url);
                }
            }
        }
        DisconnectNamedPipe(hPipe);
        CloseHandle(hPipe);
    }
}

/**
 * @brief Sends a command to the background daemon via Named Pipe.
 * 
 * @param message The command string to send.
 * @return std::string The response from the daemon, if any.
 */
std::string SendToDaemon(const std::string& message) {
    HANDLE hPipe = CreateFileA(
        PIPE_NAME,
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
    );

    if (hPipe == INVALID_HANDLE_VALUE) {
        std::cerr << "Daemon not running. Start it with: docker-credential-antimeme daemon" << std::endl;
        exit(1);
    }

    DWORD bytesWritten;
    WriteFile(hPipe, message.c_str(), (DWORD)message.length(), &bytesWritten, NULL);

    char buffer[BUFFER_SIZE] = { 0 };
    DWORD bytesRead;
    if (ReadFile(hPipe, buffer, BUFFER_SIZE - 1, &bytesRead, NULL)) {
        CloseHandle(hPipe);
        return std::string(buffer);
    }

    CloseHandle(hPipe);
    return "";
}

/**
 * @brief Main entry point matching Docker's credential helper protocol.
 */
int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: docker-credential-antimeme <daemon|store|get|erase>" << std::endl;
        return 1;
    }

    std::string command = argv[1];

    if (command == "daemon") {
        RunDaemon();
        return 0;
    }

    std::string input;
    std::string line;
    while (std::getline(std::cin, line)) {
        input += line;
    }
    input.erase(input.find_last_not_of(" \n\r\t") + 1);

    if (command == "store") {
        // Caveman impl; looks for ServerURL in the JSON to map it
        // TODO: Better parser
        std::string urlMarker = "\"ServerURL\":";
        size_t pos = input.find(urlMarker);
        std::string url = "default";
        if (pos != std::string::npos) {
            size_t start = input.find("\"", pos + urlMarker.length()) + 1;
            size_t end = input.find("\"", start);
            url = input.substr(start, end - start);
        }
        SendToDaemon("STORE " + url + "|" + input);
    } 
    else if (command == "get") {
        std::string response = SendToDaemon("GET " + input);
        if (!response.empty()) {
            std::cout << response;
        } else {
            // No credential, write to stderr to keep docker frm throwing a fit
            std::cerr << "credentials not found in cache" << std::endl;
            return 1;
        }
    } 
    else if (command == "erase") {
        SendToDaemon("ERASE " + input);
    } 
    else {
        std::cerr << "Unknown command: " << command << std::endl;
        return 1;
    }

    return 0;
}
