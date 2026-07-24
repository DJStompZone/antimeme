/**
 * @file antimeme.cpp
 * @brief Volatile, memory-only Docker credential helper using Windows DPAPI.
 * 
 * This tool securely caches Docker credentials exclusively in RAM using 
 * CryptProtectMemory. It implements a Named Pipe daemon to hold the state 
 * and handles standard Docker credential helper commands (get, store, erase, list).
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
 * @brief Extracts a specific string field value from a JSON payload.
 * 
 * Uses rudimentary string searching. 
 * TODO: Implement a robust JSON parser for better reliability.
 * 
 * @param jsonPayload The raw JSON string.
 * @param field The key to search for (e.g., "ServerURL" or "Username").
 * @return std::string The extracted value, or empty string if not found.
 */
std::string ExtractJsonField(const std::string& jsonPayload, const std::string& field) {
    std::string marker = "\"" + field + "\":";
    size_t pos = jsonPayload.find(marker);
    
    if (pos != std::string::npos) {
        size_t start = jsonPayload.find("\"", pos + marker.length());
        if (start != std::string::npos) {
            start++; 
            size_t end = jsonPayload.find("\"", start);
            if (end != std::string::npos) {
                return jsonPayload.substr(start, end - start);
            }
        }
    }
    return "";
}

/**
 * @brief Runs the persistent named pipe server to hold credentials in RAM.
 * 
 * Loops indefinitely, processing commands from client instances.
 * Supported pipe commands:
 * - STORE <url>|<json_payload>
 * - GET <url>
 * - ERASE <url>
 * - LIST
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
                    size_t split = request.find('|');
                    if (split != std::string::npos) {
                        std::string url = request.substr(6, split - 6);
                        std::string payload = request.substr(split + 1);
                        volatileVault[url] = ProtectMemory(payload);
                    }
                } 
                else if (request.rfind("GET ", 0) == 0) {
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
                else if (request == "LIST") {
                    std::string jsonResponse = "{";
                    bool first = true;
                    
                    for (auto const& [url, encryptedPayload] : volatileVault) {
                        std::string plaintext = UnprotectMemory(encryptedPayload);
                        std::string username = ExtractJsonField(plaintext, "Username");
                        SecureZeroMemory(&plaintext[0], plaintext.length());
                        
                        if (!first) jsonResponse += ",";
                        jsonResponse += "\"" + url + "\":\"" + username + "\"";
                        first = false;
                    }
                    jsonResponse += "}\n";
                    
                    DWORD bytesWritten;
                    WriteFile(hPipe, jsonResponse.c_str(), (DWORD)jsonResponse.length(), &bytesWritten, NULL);
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
 * Safely queues connection attempts to handle concurrent blasts from BuildKit.
 * 
 * @param message The command string to send.
 * @return std::string The response from the daemon, if any.
 */
std::string SendToDaemon(const std::string& message) {
    HANDLE hPipe;
    while (true) {
        hPipe = CreateFileA(
            PIPE_NAME,
            GENERIC_READ | GENERIC_WRITE,
            0,
            NULL,
            OPEN_EXISTING,
            0,
            NULL
        );

        if (hPipe != INVALID_HANDLE_VALUE) {
            break;
        }

        if (GetLastError() != ERROR_PIPE_BUSY) {
            std::cerr << "Daemon not running." << std::endl;
            exit(1);
        }

        // Wait up to 5 seconds for the pipe to become available
        if (!WaitNamedPipeA(PIPE_NAME, 5000)) {
            std::cerr << "Pipe timeout." << std::endl;
            exit(1);
        }
    }

    DWORD bytesWritten;
    WriteFile(hPipe, message.c_str(), (DWORD)message.length(), &bytesWritten, NULL);

    char buffer[BUFFER_SIZE] = { 0 };
    DWORD bytesRead;
    if (ReadFile(hPipe, buffer, BUFFER_SIZE - 1, &bytesRead, NULL) && bytesRead > 0) {
        CloseHandle(hPipe);
        return std::string(buffer, bytesRead);
    }

    CloseHandle(hPipe);
    return "";
}

/**
 * @brief Main entry point matching Docker's credential helper protocol.
 */
int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: docker-credential-antimeme <daemon|store|get|erase|list>" << std::endl;
        return 1;
    }

    std::string command = argv[1];

    if (command == "daemon") {
        RunDaemon();
        return 0;
    }

    std::string input;
    
    // Crucial: Only attempt to read STDIN for commands that actually expect it.
    // Reading STDIN on 'list' will cause a permanent hang.
    if (command == "store" || command == "get" || command == "erase") {
        std::string line;
        while (std::getline(std::cin, line)) {
            input += line;
        }
        if (!input.empty()) {
            input.erase(input.find_last_not_of(" \n\r\t") + 1);
        }
    }

    if (command == "store") {
        std::string url = ExtractJsonField(input, "ServerURL");
        if (url.empty()) url = "default";
        SendToDaemon("STORE " + url + "|" + input);
    } 
    else if (command == "get") {
        std::string response = SendToDaemon("GET " + input);
        if (!response.empty()) {
            std::cout << response;
        } else {
            // Exploit: BuildKit violently aborts on standard error exits.
            // Returning empty auth forces an immediate, native anonymous fallback.
            std::cout << "{\"Username\":\"\",\"Secret\":\"\"}\n";
        }
        return 0; 
    } 
    else if (command == "erase") {
        SendToDaemon("ERASE " + input);
    } 
    else if (command == "list") {
        std::string response = SendToDaemon("LIST");
        std::cout << response;
        return 0;
    }
    else {
        std::cerr << "Unknown command: " << command << std::endl;
        return 1;
    }

    return 0;
}
