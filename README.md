
# Antimeme

A volatile, memory-only Docker credential helper for Windows designed to support headless SSH environments and eliminate the risks of storing persistent plaintext credentials on disk.

## The Problem

Operating a headless Windows environment (e.g., via SSH) introduces significant challenges when interacting with Docker Desktop. 

By default, Docker Desktop for Windows utilizes the `wincred` credential helper, which relies on the Windows Credential Manager and the DPAPI (Data Protection API) vault. DPAPI requires an active, interactive desktop session to decrypt the user's Master Key. When a user authenticates via SSH, the DPAPI vault remains locked. Consequently, any operations requiring image resolution or authentication (such as `docker pull` or `docker build`) will fail or cause BuildKit to hang indefinitely.

The traditional workaround is to store Docker credentials in a plaintext JSON file (`config.json`) on the host. This introduces the **"Secret Zero" paradox**: automating a secure CI/CD or remote development environment forces the user to leave a master credential exposed in plaintext storage.

## The Solution

`docker-credential-antimeme` is a custom C++ daemon that replaces Docker's standard Windows credential helper. 

Rather than writing to disk or interfacing with the locked Windows Credential Manager, Antimeme acts as a persistent Named Pipe server. It securely caches Docker credentials **exclusively in volatile RAM** utilizing Windows `CryptProtectMemory`. 

When the daemon process terminates or the system reboots, the RAM is cleared and the credentials are destroyed. This ensures zero persistence and completely mitigates the risk of plaintext credentials residing in cold storage.

Additionally, Antimeme is engineered to handle BuildKit's aggressive concurrent credential polling. When credentials are not found, Antimeme intercepts the failure and returns an empty JSON payload instead of an exit error, forcing Docker into a graceful fallback to anonymous image pulls.

## Prerequisites

* Docker Desktop (Windows)
* CMake and MSVC (to compile the helper from source)

## Setup & Usage

### 1. Compile the Helper
Compile `antimeme.cpp` using MSVC or your preferred C++ compiler to generate `docker-credential-antimeme.exe`. 

### 2. Configure Docker
Update your Docker client configuration (typically located at `~/.docker/config.json`) to utilize the Antimeme helper. Change the `credsStore` value:

```json
{
  "credsStore": "antimeme"
}

```
Ensure docker-credential-antimeme.exe is in your system's PATH, or place the executable directly alongside the Docker binaries.
### 3. Initialize the Daemon
Before initiating a headless build or pull, start the daemon in the background to initialize the Named Pipe and secure memory vault:
```powershell
Start-Process -NoNewWindow -FilePath "docker-credential-antimeme.exe" -ArgumentList "daemon"

```
You will see [*] Antimeme daemon active. indicating the secure vault is ready to receive and hold credentials for the duration of your session.
### 4. Authenticate
Run docker login as usual. The credentials will be intercepted by Antimeme and stored securely in RAM.
## License
This project is licensed under the MIT License - see the LICENSE file for details.