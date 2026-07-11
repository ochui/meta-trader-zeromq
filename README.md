# MetaTrader ZeroMQ wrapper

This repository provides a small, pointer-safe Windows DLL and shared MQL API
for using ZeroMQ from MetaTrader 4 and MetaTrader 5. It builds the pinned
libzmq **4.3.5** source as part of the CMake project.

> **MT4 requires Win32 DLLs. MT5 requires x64 DLLs.**

## Supported platforms

| Terminal | Architecture | Package |
| --- | --- | --- |
| MetaTrader 4, build 600 or newer | Win32 / x86 | `dist/mt4-win32` |
| MetaTrader 5 | x64 | `dist/mt5-x64` |

The wrapper uses a stable C ABI with `__stdcall` imports. Context and socket
handles are `intptr_t` in C++ and map to `int` in MQL4 or `long` in MQL5. No
`zmq_msg_t`, poll-item, message-data, or allocated-memory pointer is exposed to
MQL. The wrapper copies received bytes into an MQL array and explicitly converts
MQL strings to and from UTF-8.

Sockets default to zero linger. Socket close and context-owned cleanup enforce
zero linger so removing an EA or shutting down a terminal cannot wait forever
for an unreachable peer. Destroying a context also closes sockets the EA forgot
to close.

## Concurrency and lifecycle

The handle registry stores shared ownership of context and socket state. Its
mutex is held only while looking up, adding, removing, or updating ownership of
handles; it is never held during ZeroMQ I/O, polling, bind/connect, close, or
context shutdown.

Each socket has its own mutex because a native ZeroMQ socket must not be used by
multiple threads simultaneously. Operations on different sockets, including
sockets in the same context, can proceed concurrently. Removing a handle marks
its state as closing, while `shared_ptr` ownership keeps that state alive until
an already-started operation finishes. Repeated close and stale handles fail
safely.

Context destruction unregisters the context and its remaining sockets first,
then calls `zmq_ctx_shutdown` outside the registry lock. This interrupts
blocking operations before context cleanup waits for each socket mutex, applies
zero linger, closes the sockets, and terminates the context.

## Requirements

- Windows 10 or newer
- Visual Studio 2022 or Build Tools 2022 with **Desktop development with C++**,
  including both MSVC x86 and x64 tools and a Windows SDK
- CMake 3.24 or newer
- Internet access on the first configure, so CMake can fetch the checksum-pinned
  libzmq 4.3.5 source archive

No separately installed ZeroMQ SDK is used.

## Build

From a PowerShell prompt at the repository root, build Win32 for MT4:

```powershell
cmake -S . -B build/win32 `
  -G "Visual Studio 17 2022" `
  -A Win32 `
  -DCMAKE_BUILD_TYPE=Release

cmake --build build/win32 --config Release
ctest --test-dir build/win32 -C Release --output-on-failure
```

Build x64 for MT5:

```powershell
cmake -S . -B build/x64 `
  -G "Visual Studio 17 2022" `
  -A x64 `
  -DCMAKE_BUILD_TYPE=Release

cmake --build build/x64 --config Release
ctest --test-dir build/x64 -C Release --output-on-failure
```

CMake derives the package from the generator architecture and will only accept
32-bit or 64-bit pointer sizes. Each build compiles its own matching libzmq, so
an x64 wrapper cannot accidentally link a Win32 import library, or vice versa.

Successful builds create:

```text
dist/
├── mt4-win32/
│   ├── zmq_bind.dll
│   ├── libzmq.dll
│   └── include/
│       ├── zmq_bind.mqh
│       └── zmq_native.mqh
└── mt5-x64/
    ├── zmq_bind.dll
    ├── libzmq.dll
    └── include/
        ├── zmq_bind.mqh
        └── zmq_native.mqh
```

The DLLs use the Visual C++ runtime. If a clean machine reports a missing runtime
dependency, install the current Microsoft Visual C++ 2015–2022 Redistributable
matching the terminal architecture (x86 for MT4, x64 for MT5).

## Install

Open **File → Open Data Folder** in each terminal.

For MT4, copy both DLLs from `dist/mt4-win32` to:

```text
<MT4 data folder>\MQL4\Libraries\
```

Copy the package's `include` files to:

```text
<MT4 data folder>\MQL4\Include\
```

For MT5, copy both DLLs from `dist/mt5-x64` to:

```text
<MT5 data folder>\MQL5\Libraries\
```

Copy its `include` files to:

```text
<MT5 data folder>\MQL5\Include\
```

Copy the corresponding files under `examples/mt4` or `examples/mt5` into the
terminal's `MQL4\Experts` or `MQL5\Experts` directory and compile them in
MetaEditor. In **Tools → Options → Expert Advisors**, enable DLL imports. Also
check **Allow DLL imports** in the EA's Common settings when attaching it.

## MQL API

```mql
#include <zmq_bind.mqh>

ZMQ_HANDLE context = z_init(1);
ZMQ_HANDLE socket = z_socket(context, ZMQ_PUB);
z_bind(socket, "tcp://*:5556");
z_send(socket, "prices|symbol=EURUSD|text=Héllo 世界");
z_close(socket);
z_term(context);
```

Subscribers use `ZMQ_SUB`, `z_subscribe`, `z_connect`, and `z_recv`. For a
non-blocking event loop, call `z_poll_socket(socket, 0)` and then
`z_recv(socket, ZMQ_DONTWAIT)`.

Receive results are distinguishable:

- No non-blocking message returns `""`, sets `z_last_receive_size()` to `-1`,
  and does not log the expected would-block result.
- A real receive failure returns `""`, sets the size to `-1`, and logs the
  ZeroMQ error.
- An empty ZeroMQ message returns `""` and sets the size to `0` without logging
  an error.

Use `z_is_would_block_error(error_code)` when handling lower-level calls. The
native DLL performs the platform-correct comparison; MQL does not hardcode an
error number.

Receive buffers grow and retry inside the high-level wrapper, so messages are
not truncated. If the first buffer is too small, the native wrapper retains the
message until the retry. While a message is retained, `z_poll_socket` immediately
reports `ZMQ_POLLIN`; after the message is copied, polling returns to the native
socket state.

The common helpers are:

```text
z_init, z_socket, z_bind, z_connect, z_send, z_recv,
z_subscribe, z_unsubscribe, z_close, z_term,
z_set_option_int, z_set_option_long, z_set_option_bytes,
z_get_option_int, z_get_option_long, z_poll_socket
```

### Socket option types

The option helpers enforce the value width expected by libzmq 4.3.5:

| MQL helper | Value width | Supported public options |
| --- | --- | --- |
| `z_set_option_int`, `z_get_option_int` | 32-bit `int` | `ZMQ_RATE`, `ZMQ_RECOVERY_IVL`, `ZMQ_SNDBUF`, `ZMQ_RCVBUF`, `ZMQ_RCVMORE` (get only), `ZMQ_EVENTS` (get only), `ZMQ_TYPE` (get only), `ZMQ_LINGER`, `ZMQ_RECONNECT_IVL`, `ZMQ_BACKLOG`, `ZMQ_RECONNECT_IVL_MAX`, `ZMQ_SNDHWM`, `ZMQ_RCVHWM`, `ZMQ_RCVTIMEO`, `ZMQ_SNDTIMEO`, `ZMQ_IMMEDIATE` |
| `z_set_option_long`, `z_get_option_long` | 64-bit `long` | `ZMQ_AFFINITY`, `ZMQ_MAXMSGSIZE` |
| `z_set_option_bytes` | UTF-8 bytes | `ZMQ_ROUTING_ID` / `ZMQ_IDENTITY`, `ZMQ_SUBSCRIBE`, `ZMQ_UNSUBSCRIBE` |

Passing a 64-bit option through the 32-bit functions, or a 32-bit option through
the 64-bit functions, fails instead of silently truncating or using the wrong
ABI width. `ZMQ_FD` is intentionally not exposed because its native width is not
portable between Win32 and x64. Legacy `z_set_hwm` remains set-only and applies
its 32-bit value to both `ZMQ_SNDHWM` and `ZMQ_RCVHWM`.

## Tests

`tests/native_smoke.cpp` runs for both architectures through CTest. It covers
the native ABI, exact libzmq version, pointer-sized handles, PUB/SUB, PUSH/PULL,
non-blocking receive, empty and long messages, UTF-8, `|` and `=`, message bursts,
retained-message polling, typed 64-bit options, independent sockets and contexts,
close/shutdown races, stale handles, and repeated cleanup including a
deliberately unclosed socket. Every blocking regression uses a bounded timeout,
and CTest enforces a 30-second limit for the suite.

The build also produces `zmq_cross_arch_peer` in each build's `Release`
directory. It verifies Win32↔x64 PUB/SUB wire
interoperability over TCP in both directions.

Actual MT4/MT5 loading, EA removal, terminal shutdown, and the four cross-version
combinations require installed terminals; native CTest does not replace those
terminal checks.

## Migration from the ZeroMQ 2.0.10 wrapper

Most high-level MT4 source can keep `z_init`, `z_socket`, `z_bind`, `z_connect`,
`z_send`, `z_recv`, `z_close`, and `z_term`. Change handle declarations from
plain `int` to `ZMQ_HANDLE`; on MT4 that remains an `int`, while MT5 compiles it
as a 64-bit `long`.

The unsafe `_zmsg_new`, `_zmq_msg_data`, `_ptr2str`, raw poll-item, and Win32
`lstrlenA` APIs were removed intentionally. Use `z_send` / `z_recv` or
`z_send_bytes`; use `z_poll_socket` instead of managing a native poll-item.
Legacy `z_new_poll` and `z_poll` remain safe compatibility aliases and store no
poller pointer.

`ZMQ_HWM` is no longer a native 4.x option. Calling `z_set_hwm` or setting
`ZMQ_HWM` through `z_set_option_int` applies the value to both `ZMQ_SNDHWM` and
`ZMQ_RCVHWM`. Use `z_set_option_int` for options such as `ZMQ_LINGER` and
`ZMQ_RCVTIMEO`, `z_set_option_long` for `ZMQ_AFFINITY` or `ZMQ_MAXMSGSIZE`, and
`z_set_option_bytes` for subscriptions or routing IDs.

## Limitations

- Operations on one socket are intentionally serialized. Explicit socket close
  waits for an in-flight operation on that same socket; use `ZMQ_DONTWAIT`,
  `ZMQ_RCVTIMEO`, `ZMQ_SNDTIMEO`, or bounded polling when another thread may
  close it. Destroying the owning context can interrupt a blocking operation.
- The public byte-option helper sets byte values but does not currently retrieve
  byte-valued options.
- Actual EA removal and terminal shutdown behavior must still be validated in
  the target MT4 and MT5 terminal builds; native CTest cannot automate terminal
  UI lifecycle events.

## Troubleshooting

- **“not a valid Win32 application”, error 193, or the DLL will not load:** the
  architecture is wrong. MT4 needs both Win32 DLLs; MT5 needs both x64 DLLs.
- **`libzmq.dll` is missing:** place it next to `zmq_bind.dll` in the terminal's
  `Libraries` directory. Never mix files from the two packages.
- **DLL calls are blocked:** enable DLL imports globally and for the attached EA.
- **Port 5556 is already in use:** change the endpoint in both example EAs, or
  stop the process already bound to it.
- **A subscriber sees no first message:** PUB/SUB subscriptions are asynchronous.
  Keep the publisher running; the timer examples send repeatedly after the
  connection and subscription have propagated.
- **Configure cannot download libzmq:** restore access to GitHub or provide an
  extracted 4.3.5 tree with
  `-DFETCHCONTENT_SOURCE_DIR_LIBZMQ=C:\path\to\libzmq-4.3.5`.
