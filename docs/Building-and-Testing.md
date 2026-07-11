# Building and testing

## Requirements

- Windows 10 or newer
- Visual Studio 2022 or Build Tools 2022
- Desktop development with C++
- MSVC x86 and x64 build tools
- Windows SDK
- CMake 3.24 or newer
- Internet access during the first configure

The build downloads the checksum-pinned libzmq 4.3.5 source. A separate ZeroMQ
SDK is not required.

## Build MT4 Win32

Configure from a clean build directory:

```powershell
cmake -S . -B build/win32 -G "Visual Studio 17 2022" -A Win32
cmake --build build/win32 --config Release
```

The package is generated in `dist/mt4-win32`.

## Build MT5 x64

Configure from a clean build directory:

```powershell
cmake -S . -B build/x64 -G "Visual Studio 17 2022" -A x64
cmake --build build/x64 --config Release
```

The package is generated in `dist/mt5-x64`.

## Run tests

Run the native test suite for each architecture:

```powershell
ctest --test-dir build/win32 -C Release --output-on-failure
ctest --test-dir build/x64 -C Release --output-on-failure
```

After both builds complete, run the generic cross-architecture validation:

```powershell
./scripts/run_cross_arch_test.ps1 `
  -Win32PeerDirectory build/win32/Release `
  -X64PeerDirectory build/x64/Release
```

## Validate package contents

Each generated package must contain:

```text
zmq_bind.dll
libzmq.dll
include/zmq_bind.mqh
include/zmq_native.mqh
include/zmq_bind.h
```

Use `scripts/verify_pe_architecture.ps1` to confirm that each DLL matches its
target architecture.
