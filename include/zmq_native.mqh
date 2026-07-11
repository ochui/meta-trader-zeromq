#ifndef META_TRADER_ZMQ_NATIVE_MQH
#define META_TRADER_ZMQ_NATIVE_MQH

#ifdef __MQL5__
   #define ZMQ_HANDLE long
#else
   #define ZMQ_HANDLE int
#endif

#import "zmq_bind.dll"
ZMQ_HANDLE zmqb_context_create(int io_threads);
int zmqb_context_destroy(ZMQ_HANDLE context);
ZMQ_HANDLE zmqb_socket_create(ZMQ_HANDLE context, int socket_type);
int zmqb_socket_close(ZMQ_HANDLE socket);
int zmqb_bind(ZMQ_HANDLE socket, uchar &endpoint[]);
int zmqb_connect(ZMQ_HANDLE socket, uchar &endpoint[]);
int zmqb_send(ZMQ_HANDLE socket, uchar &data[], int length, int flags);
int zmqb_send_int_array(ZMQ_HANDLE socket, int &data[], int count, int flags);
int zmqb_send_double_array(ZMQ_HANDLE socket, double &data[], int count, int flags);
int zmqb_receive(ZMQ_HANDLE socket, uchar &buffer[], int capacity, int flags);
int zmqb_set_option_int(ZMQ_HANDLE socket, int option, int value);
int zmqb_set_option_bytes(ZMQ_HANDLE socket, int option, uchar &value[], int length);
int zmqb_get_option_int(ZMQ_HANDLE socket, int option, int &value[]);
int zmqb_poll(ZMQ_HANDLE socket, int events, int timeout_ms);
int zmqb_errno();
int zmqb_error_text(int error_code, uchar &buffer[], int capacity);
int zmqb_version_major();
int zmqb_version_minor();
int zmqb_version_patch();
#import

// Socket types (stable since ZeroMQ 2.x).
#define ZMQ_PAIR    0
#define ZMQ_PUB     1
#define ZMQ_SUB     2
#define ZMQ_REQ     3
#define ZMQ_REP     4
#define ZMQ_DEALER  5
#define ZMQ_ROUTER  6
#define ZMQ_PULL    7
#define ZMQ_PUSH    8
#define ZMQ_XPUB    9
#define ZMQ_XSUB   10
#define ZMQ_STREAM 11
#define ZMQ_XREQ ZMQ_DEALER
#define ZMQ_XREP ZMQ_ROUTER

// Common socket options from libzmq 4.3.5.
#define ZMQ_HWM                 1 // compatibility: wrapper sets SNDHWM + RCVHWM
#define ZMQ_AFFINITY            4
#define ZMQ_ROUTING_ID          5
#define ZMQ_IDENTITY            5
#define ZMQ_SUBSCRIBE           6
#define ZMQ_UNSUBSCRIBE         7
#define ZMQ_RATE                8
#define ZMQ_RECOVERY_IVL        9
#define ZMQ_SNDBUF             11
#define ZMQ_RCVBUF             12
#define ZMQ_RCVMORE            13
#define ZMQ_FD                 14
#define ZMQ_EVENTS             15
#define ZMQ_TYPE               16
#define ZMQ_LINGER             17
#define ZMQ_RECONNECT_IVL      18
#define ZMQ_BACKLOG            19
#define ZMQ_RECONNECT_IVL_MAX  21
#define ZMQ_MAXMSGSIZE         22
#define ZMQ_SNDHWM             23
#define ZMQ_RCVHWM             24
#define ZMQ_RCVTIMEO           27
#define ZMQ_SNDTIMEO           28
#define ZMQ_IMMEDIATE          39

#define ZMQ_DONTWAIT 1
#define ZMQ_NOBLOCK  ZMQ_DONTWAIT
#define ZMQ_SNDMORE  2

#define ZMQ_POLLIN  1
#define ZMQ_POLLOUT 2
#define ZMQ_POLLERR 4

#endif
