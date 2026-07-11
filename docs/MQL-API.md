# MQL API

Include the high-level MQL interface:

```mql
#include <zmq_bind.mqh>
```

## Handles

`ZMQ_HANDLE` uses the correct width for the terminal:

| Terminal | MQL type |
| --- | --- |
| MetaTrader 4 | `int` |
| MetaTrader 5 | `long` |

Treat handles as opaque values. A zero handle indicates that context or socket
creation failed.

## Context and socket lifecycle

```mql
ZMQ_HANDLE context = z_init(1);
ZMQ_HANDLE socket = z_socket(context, ZMQ_PUB);

z_bind(socket, "tcp://*:5556");
z_send(socket, "prices|symbol=EURUSD");

z_close(socket);
z_term(context);
```

Create a context before creating sockets. Close each socket and then terminate
its context during Expert Advisor shutdown.

## Messaging

| Function | Purpose |
| --- | --- |
| `z_send` | Send an MQL string as UTF-8 |
| `z_send_bytes` | Send a byte array |
| `z_send_int_array` | Send an integer array |
| `z_send_double_array` | Send a double array |
| `z_recv` | Receive a message as an MQL string |

For PUB/SUB, subscribe before connecting or receiving:

```mql
ZMQ_HANDLE subscriber = z_socket(context, ZMQ_SUB);
z_subscribe(subscriber, "prices|");
z_connect(subscriber, "tcp://127.0.0.1:5556");
```

## Polling and non-blocking receive

```mql
if(z_poll_socket(socket, 0) > 0)
{
   string message = z_recv(socket, ZMQ_DONTWAIT);
   if(z_last_receive_size() >= 0)
      Print(message);
}
```

`z_last_receive_size()` reports the last receive result:

- `-1`: no message was available or the receive failed
- `0`: an empty message was received
- positive value: received message size in bytes

Use `z_error()` to obtain the latest error code and
`z_is_would_block_error(error_code)` to identify the normal non-blocking
no-message result.

## Socket options

Use the function matching the native option value width.

| Function | Value type | Options |
| --- | --- | --- |
| `z_set_option_int`, `z_get_option_int` | 32-bit `int` | `ZMQ_RATE`, `ZMQ_RECOVERY_IVL`, `ZMQ_SNDBUF`, `ZMQ_RCVBUF`, `ZMQ_RCVMORE` (get only), `ZMQ_EVENTS` (get only), `ZMQ_TYPE` (get only), `ZMQ_LINGER`, `ZMQ_RECONNECT_IVL`, `ZMQ_BACKLOG`, `ZMQ_RECONNECT_IVL_MAX`, `ZMQ_SNDHWM`, `ZMQ_RCVHWM`, `ZMQ_RCVTIMEO`, `ZMQ_SNDTIMEO`, `ZMQ_IMMEDIATE` |
| `z_set_option_long`, `z_get_option_long` | 64-bit `long` | `ZMQ_AFFINITY`, `ZMQ_MAXMSGSIZE` |
| `z_set_option_bytes` | UTF-8 string | `ZMQ_ROUTING_ID`, `ZMQ_IDENTITY`, `ZMQ_SUBSCRIBE`, `ZMQ_UNSUBSCRIBE` |

## Common functions

```text
z_init, z_socket, z_bind, z_connect, z_send, z_recv,
z_subscribe, z_unsubscribe, z_close, z_term,
z_set_option_int, z_set_option_long, z_set_option_bytes,
z_get_option_int, z_get_option_long, z_poll_socket,
z_error, z_is_would_block_error, z_version_string
```
