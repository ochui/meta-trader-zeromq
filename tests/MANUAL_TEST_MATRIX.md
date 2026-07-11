# MetaTrader interoperability test matrix

Automated CTest coverage validates the native ABI, handle width, libzmq version,
PUB/SUB, PUSH/PULL, non-blocking receive, empty and long messages, UTF-8,
delimiter-heavy payloads, bursts, repeated startup/shutdown, and cleanup of a
socket left open by its EA. `zmq_cross_arch_peer` additionally allows the Win32
and x64 output stacks to be run against one another over TCP in both directions.

The terminal-specific cases below require installed MT4 and MT5 terminals and
cannot be automated by the native build. Use the publisher and subscriber EAs
from `examples/`, and record terminal build numbers with the result.

| Publisher | Subscriber | Required package(s) | Result |
| --- | --- | --- | --- |
| MT4 | MT4 | `mt4-win32` in both MT4 terminals | Not run |
| MT5 | MT5 | `mt5-x64` in both MT5 terminals | Not run |
| MT4 | MT5 | `mt4-win32` publisher, `mt5-x64` subscriber | Not run |
| MT5 | MT4 | `mt5-x64` publisher, `mt4-win32` subscriber | Not run |

For each row:

1. Start the subscriber, then publisher; confirm UTF-8 and `|` / `=` survive.
2. Remove and reattach the publisher; confirm the subscriber reconnects.
3. Remove and reattach the subscriber; confirm it resumes receiving.
4. Send bursts and a long message using the high-level API.
5. Remove each EA while connected; confirm the terminal remains responsive.
6. Close each terminal while connected; confirm shutdown does not hang.
7. Repeat initialization and shutdown at least 25 times.
