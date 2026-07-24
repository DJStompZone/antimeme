<div align="center"><h1 width="20%">Antimeme</h1>
<span width="20%">A volatile, memory-only Docker credential helper for Windows designed to support headless SSH environments and eliminate the risks of storing persistent plaintext credentials on disk.</span><br><br><img src="https://img.shields.io/github/v/release/djstompzone/antimeme"> <img src="https://img.shields.io/github/actions/workflow/status/djstompzone/antimeme/build-release.yml"> <img src="https://img.shields.io/github/license/djstompzone/antimeme"> <img src="https://img.shields.io/badge/windows-supported-green?logo=data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHZpZXdCb3g9IjAgMCA0ODc1IDQ4NzUiPjxwYXRoIGZpbGw9IiMwMDc4ZDQiIGQ9Ik0wIDBoMjMxMXYyMzEwSDB6bTI1NjQgMGgyMzExdjIzMTBIMjU2NHpNMCAyNTY0aDIzMTF2MjMxMUgwem0yNTY0IDBoMjMxMXYyMzExSDI1NjQiLz48L3N2Zz4="><br><br></div>


<div align=center><h2>Synopsis</h2></div>

### The Problem

Operating a headless Windows environment (e.g., via SSH) introduces significant challenges when interacting with Docker Desktop. 

By default, Docker Desktop for Windows utilizes the `wincred` credential helper, which relies on the Windows Credential Manager and the DPAPI (Data Protection API) vault. DPAPI requires an active, interactive desktop session to decrypt the user's Master Key. When a user authenticates via SSH, the DPAPI vault remains locked. Consequently, any operations requiring image resolution or authentication (such as `docker pull` or `docker build`) will fail or cause BuildKit to hang indefinitely.

The traditional workaround is to store Docker credentials in a plaintext JSON file (`config.json`) on the host. This introduces the **"Secret Zero" paradox**: automating a secure CI/CD or remote development environment forces the user to leave a master credential exposed in plaintext storage.


### The Solution

`docker-credential-antimeme` is a custom C++ daemon that replaces Docker's standard Windows credential helper. 

Rather than writing to disk or interfacing with the locked Windows Credential Manager, Antimeme acts as a persistent Named Pipe server. It securely caches Docker credentials **exclusively in volatile RAM** utilizing Windows `CryptProtectMemory`. 

When the daemon process terminates or the system reboots, the RAM is cleared and the credentials are destroyed. This ensures zero persistence and completely mitigates the risk of plaintext credentials residing in cold storage.

Additionally, Antimeme is engineered to handle BuildKit's aggressive concurrent credential polling. When credentials are not found, Antimeme intercepts the failure and returns an empty JSON payload instead of an exit error, forcing Docker into a graceful fallback to anonymous image pulls.

<div align="center"><br><hr width="5%"><br></div>

## Installation

### Option 1: Download Pre-compiled Binary
 ![easy](https://progress-bar.xyz/1/?scale=5&style=plastic&show_text=true&suffix=%20(Easy)%20&title=Difficulty&color=555555&progress_color=22bb22&progress_background=131313)
- Download the latest `docker-credential-antimeme.exe` from the [GitHub Releases](../../releases) page.

  > Releases are built automatically via GitHub Actions.


### Option 2: Compile from Source
  ![medium](https://progress-bar.xyz/2?scale=5&style=plastic&show_text=true&suffix=%20(Med)%20&title=Difficulty&color=555555&progress_color=99bb22&progress_background=131313)

For environments requiring source verification, you can compile the helper manually.
- ### Prerequisites
  - [CMake](https://cmake.org/)
  - [MSVC](https://visualstudio.microsoft.com/vs/features/cplusplus/)

- ### Build
  ```powershell
  cmake -B build -DCMAKE_BUILD_TYPE=Release
  cmake --build build --config Release
  ```

   > Note: A [build script](build.ps1) has been included for convenience

<div align="center"><br><hr width="5%"><br></div>

## Setup & Usage

### 1. Configure Docker
Update your Docker client configuration (typically located at `~/.docker/config.json`) to utilize the Antimeme helper. Change the `credsStore` value:

```json
{
  "credsStore": "antimeme"
}
```

Ensure `docker-credential-antimeme.exe` is in your system's `PATH`, or place the executable directly alongside the Docker binaries.

### 2. Initialize the Daemon
Before initiating a headless build or pull, start the daemon in the background to initialize the Named Pipe and secure memory vault:

```powershell
Start-Process -NoNewWindow -FilePath "docker-credential-antimeme.exe" -ArgumentList "daemon"
```

You will see `[*] Antimeme daemon active.` indicating the secure vault is ready to receive and hold credentials for the duration of your session.

### 3. Authenticate
Run `docker login` as usual. The credentials will be intercepted by Antimeme and stored securely in RAM.

<div align="center"><br><hr width="5%"><br></div>

## Building a Release (Maintainers)

This repository includes a manual GitHub Actions workflow for generating release artifacts. 

1. Navigate to the **Actions** tab in GitHub.
2. Select the **Build and Release** workflow.
3. Click **Run workflow**.
4. Provide the **Tag name** (e.g., `v1.0.0`) and **Release Title**.
5. Provide optional release notes, or leave blank to auto-generate based on merged PRs.
6. The compiled `.exe` will be attached to the newly created GitHub Release automatically.

<div align="center"><br><hr width="5%"><br></div>

## License
This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.
