# Installation

## Select a package

Use the MT4 Win32 package for MetaTrader 4 and the MT5 x64 package for
MetaTrader 5. Do not mix DLLs from different architectures.

Each binary package contains:

```text
zmq_bind.dll
libzmq.dll
include/zmq_bind.mqh
include/zmq_native.mqh
include/zmq_bind.h
```

## MetaTrader 4

Open **File > Open Data Folder** in MetaTrader 4.

Place both DLLs in:

```text
<MT4 data folder>\MQL4\Libraries\
```

Place `zmq_bind.mqh` and `zmq_native.mqh` in:

```text
<MT4 data folder>\MQL4\Include\
```

Place the files under `examples/mt4` in `MQL4\Experts` when using the supplied
examples.

## MetaTrader 5

Open **File > Open Data Folder** in MetaTrader 5.

Place both DLLs in:

```text
<MT5 data folder>\MQL5\Libraries\
```

Place `zmq_bind.mqh` and `zmq_native.mqh` in:

```text
<MT5 data folder>\MQL5\Include\
```

Place the files under `examples/mt5` in `MQL5\Experts` when using the supplied
examples.

## Enable DLL imports

In MetaTrader:

1. Open **Tools > Options > Expert Advisors**.
2. Enable DLL imports.
3. Compile the Expert Advisor in MetaEditor.
4. Attach it to a chart.
5. Enable **Allow DLL imports** in the Expert Advisor's Common settings.

## Verify the installation

Compile and run the publisher and subscriber examples for the selected
terminal. The publisher binds to TCP port 5556 and the subscriber connects to
`tcp://127.0.0.1:5556` by default.

Change the endpoint in both examples if port 5556 is already in use.
