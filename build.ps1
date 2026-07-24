$ErrorActionPreference = "Stop"

Push-Location
try {
    New-Item -ItemType Directory -Force -Path build | Out-Null
    Set-Location build
    cmake ..
    cmake --build . --config Release
    Write-Host "[*] Build successful." -ForegroundColor Green
} catch {
    Write-Host "[!] Build failed: $_" -ForegroundColor Red
    exit 1
} finally {
    Pop-Location
}
