# MetaTrader ZeroMQ

ZeroMQ integration for MetaTrader 4 and MetaTrader 5. The project provides
native Windows DLLs, MQL include files, a C header, and examples for common
messaging patterns.

The build uses the bundled, checksum-pinned libzmq 4.3.5 source.

## Documentation

- [Documentation home](docs/Home.md)
- [Installation](docs/Installation.md)
- [MQL API](docs/MQL-API.md)
- [Building and testing](docs/Building-and-Testing.md)

## Packages

| Platform | Architecture | Directory |
| --- | --- | --- |
| MetaTrader 4 | Win32 / x86 | `dist/mt4-win32` |
| MetaTrader 5 | x64 | `dist/mt5-x64` |

Each platform package contains:

```text
zmq_bind.dll
libzmq.dll
include/zmq_bind.mqh
include/zmq_native.mqh
include/zmq_bind.h
```

Use DLLs from the package matching the terminal architecture. MT4 requires the
Win32 package, while 64-bit MT5 requires the x64 package.

## Installation

Open **File > Open Data Folder** in MetaTrader.

For MT4, place the DLLs in:

```text
<MT4 data folder>\MQL4\Libraries\
```

Place the MQL include files in:

```text
<MT4 data folder>\MQL4\Include\
```

For MT5, place the DLLs in:

```text
<MT5 data folder>\MQL5\Libraries\
```

Place the MQL include files in:

```text
<MT5 data folder>\MQL5\Include\
```

Enable DLL imports in **Tools > Options > Expert Advisors** and in the Common
settings of the Expert Advisor.

Example Expert Advisors are available in `examples/mt4` and `examples/mt5`.

## Quick start

```mql
#include <zmq_bind.mqh>

ZMQ_HANDLE context = z_init(1);
ZMQ_HANDLE socket = z_socket(context, ZMQ_PUB);

z_bind(socket, "tcp://*:5556");
z_send(socket, "prices|symbol=EURUSD");

z_close(socket);
z_term(context);
```

Subscribers use `ZMQ_SUB`, `z_subscribe`, `z_connect`, and `z_recv`. For a
non-blocking loop, call `z_poll_socket(socket, 0)` before
`z_recv(socket, ZMQ_DONTWAIT)`.

## API

Common functions:

```text
z_init, z_socket, z_bind, z_connect, z_send, z_recv,
z_subscribe, z_unsubscribe, z_close, z_term,
z_set_option_int, z_set_option_long, z_set_option_bytes,
z_get_option_int, z_get_option_long, z_poll_socket
```

`ZMQ_HANDLE` maps to the correct handle width for each terminal: `int` on MT4
and `long` on MT5.

Receive results can be distinguished with `z_last_receive_size()`:

- `-1` means no message was available or the receive failed.
- `0` means an empty message was received.
- A positive value is the received message size.

Use `z_is_would_block_error(error_code)` to identify a non-blocking operation
that found no available message.

### Socket options

| Function | Value type | Options |
| --- | --- | --- |
| `z_set_option_int`, `z_get_option_int` | 32-bit `int` | `ZMQ_RATE`, `ZMQ_RECOVERY_IVL`, `ZMQ_SNDBUF`, `ZMQ_RCVBUF`, `ZMQ_RCVMORE` (get only), `ZMQ_EVENTS` (get only), `ZMQ_TYPE` (get only), `ZMQ_LINGER`, `ZMQ_RECONNECT_IVL`, `ZMQ_BACKLOG`, `ZMQ_RECONNECT_IVL_MAX`, `ZMQ_SNDHWM`, `ZMQ_RCVHWM`, `ZMQ_RCVTIMEO`, `ZMQ_SNDTIMEO`, `ZMQ_IMMEDIATE` |
| `z_set_option_long`, `z_get_option_long` | 64-bit `long` | `ZMQ_AFFINITY`, `ZMQ_MAXMSGSIZE` |
| `z_set_option_bytes` | UTF-8 string | `ZMQ_ROUTING_ID`, `ZMQ_IDENTITY`, `ZMQ_SUBSCRIBE`, `ZMQ_UNSUBSCRIBE` |

## Build

Requirements:

- Windows 10 or newer
- Visual Studio 2022 or Build Tools 2022 with Desktop development with C++
- MSVC x86 and x64 build tools
- Windows SDK
- CMake 3.24 or newer
- Internet access during the first configure

Build MT4 Win32 from a clean directory:

```powershell
cmake -S . -B build/win32 -G "Visual Studio 17 2022" -A Win32
cmake --build build/win32 --config Release
```

Build MT5 x64 from a clean directory:

```powershell
cmake -S . -B build/x64 -G "Visual Studio 17 2022" -A x64
cmake --build build/x64 --config Release
```

Successful builds generate the matching package under `dist/`.

## Testing

Run the native test suite for each architecture:

```powershell
ctest --test-dir build/win32 -C Release --output-on-failure
ctest --test-dir build/x64 -C Release --output-on-failure
```

Run the cross-architecture test after both builds complete:

```powershell
./scripts/run_cross_arch_test.ps1 `
  -Win32PeerDirectory build/win32/Release `
  -X64PeerDirectory build/x64/Release
```

## Releases

Tagged releases publish:

- MT4 Win32 package
- MT5 x64 package
- Headers-only package
- `SHA256SUMS.txt`
