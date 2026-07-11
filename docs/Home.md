# MetaTrader ZeroMQ documentation

MetaTrader ZeroMQ provides native Windows DLLs and MQL include files for using
ZeroMQ from MetaTrader 4 and MetaTrader 5.

## Start here

- [Installation](Installation.md) explains package selection, terminal paths,
  and DLL import settings.
- [MQL API](MQL-API.md) describes handles, socket lifecycle, messaging,
  polling, errors, and socket options.
- [Building and testing](Building-and-Testing.md) contains the supported build
  commands and generic validation workflow.

## Platform mapping

| Terminal | Architecture | Package |
| --- | --- | --- |
| MetaTrader 4 | Win32 / x86 | `mt4-win32` |
| MetaTrader 5 | x64 | `mt5-x64` |

Both `zmq_bind.dll` and `libzmq.dll` must match the terminal architecture and
must remain together in the terminal's `Libraries` directory.

## Source layout

| Path | Contents |
| --- | --- |
| `include/` | Public MQL include files |
| `wrap/windows/` | Native C interface and DLL implementation |
| `examples/mt4/` | MetaTrader 4 examples |
| `examples/mt5/` | MetaTrader 5 examples |
| `tests/` | Native validation programs |
| `scripts/` | Architecture, interoperability, and packaging commands |
