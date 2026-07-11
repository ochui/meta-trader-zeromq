#ifndef META_TRADER_ZMQ_BIND_MQH
#define META_TRADER_ZMQ_BIND_MQH

#include <zmq_native.mqh>

#define ZMQ_UTF8_CODEPAGE 65001
#define ZMQ_INITIAL_RECEIVE_CAPACITY 4096

int Z_DEBUG = 0;
int ZMQ_LAST_RECEIVE_SIZE = -1;

void z_trace(string text)
{
   if(Z_DEBUG == 1)
      Print(text);
}

int z_error()
{
   int error_code = zmqb_errno();
   uchar text[];
   ArrayResize(text, 512);
   int length = zmqb_error_text(error_code, text, ArraySize(text));
   string description = "Unknown ZeroMQ error";
   if(length >= 0)
      description = CharArrayToString(text, 0, length, ZMQ_UTF8_CODEPAGE);
   PrintFormat("ZeroMQ error %d: %s", error_code, description);
   return error_code;
}

bool z_is_would_block_error(int error_code)
{
   return zmqb_is_would_block(error_code) != 0;
}

string z_version_string()
{
   return IntegerToString(zmqb_version_major()) + "." +
          IntegerToString(zmqb_version_minor()) + "." +
          IntegerToString(zmqb_version_patch());
}

void z_version(int &version[])
{
   ArrayResize(version, 3);
   version[0] = zmqb_version_major();
   version[1] = zmqb_version_minor();
   version[2] = zmqb_version_patch();
}

int z_poll_socket(ZMQ_HANDLE socket, int timeout, int events = ZMQ_POLLIN)
{
   int result = zmqb_poll(socket, events, timeout);
   if(result < 0)
      z_error();
   return result;
}

// Compatibility aliases: a poller is now just its socket handle; no native
// poll-item pointer crosses the DLL boundary.
ZMQ_HANDLE z_new_poll(ZMQ_HANDLE socket)
{
   return socket;
}

int z_poll(ZMQ_HANDLE poller, int timeout)
{
   return z_poll_socket(poller, timeout, ZMQ_POLLIN);
}

ZMQ_HANDLE z_init(int io_threads)
{
   ZMQ_HANDLE context = zmqb_context_create(io_threads);
   if(context == 0)
      z_error();
   return context;
}

ZMQ_HANDLE z_socket(ZMQ_HANDLE context, int type)
{
   ZMQ_HANDLE socket = zmqb_socket_create(context, type);
   if(socket == 0)
      z_error();
   return socket;
}

int z_bind(ZMQ_HANDLE socket, string endpoint)
{
   uchar bytes[];
   StringToCharArray(endpoint, bytes, 0, -1, ZMQ_UTF8_CODEPAGE);
   int result = zmqb_bind(socket, bytes);
   if(result < 0)
      z_error();
   return result;
}

int z_connect(ZMQ_HANDLE socket, string endpoint)
{
   uchar bytes[];
   StringToCharArray(endpoint, bytes, 0, -1, ZMQ_UTF8_CODEPAGE);
   int result = zmqb_connect(socket, bytes);
   if(result < 0)
      z_error();
   return result;
}

int z_send_bytes(ZMQ_HANDLE socket, uchar &data[], int length, int flags = 0)
{
   int available = ArraySize(data);
   if(length < 0 || length > available)
   {
      PrintFormat("ZeroMQ send error: length %d is outside byte array size %d",
                  length,
                  available);
      return -1;
   }

   int result = zmqb_send(socket, data, length, flags);
   if(result < 0)
      z_error();
   return result;
}

int z_send(ZMQ_HANDLE socket, string message, int flags = 0)
{
   uchar bytes[];
   int with_terminator = StringToCharArray(message, bytes, 0, -1, ZMQ_UTF8_CODEPAGE);
   int length = with_terminator > 0 ? with_terminator - 1 : 0;
   return z_send_bytes(socket, bytes, length, flags);
}

int z_send_int_array(ZMQ_HANDLE socket, int &data[], int flags = 0)
{
   int result = zmqb_send_int_array(socket, data, ArraySize(data), flags);
   if(result < 0)
      z_error();
   return result;
}

int z_send_double_array(ZMQ_HANDLE socket, double &data[], int flags = 0)
{
   int result = zmqb_send_double_array(socket, data, ArraySize(data), flags);
   if(result < 0)
      z_error();
   return result;
}

string z_recv(ZMQ_HANDLE socket, int flags = 0)
{
   uchar bytes[];
   ArrayResize(bytes, ZMQ_INITIAL_RECEIVE_CAPACITY);
   int received = zmqb_receive(socket, bytes, ArraySize(bytes), flags);
   if(received > ArraySize(bytes))
   {
      ArrayResize(bytes, received);
      received = zmqb_receive(socket, bytes, ArraySize(bytes), flags);
   }

   ZMQ_LAST_RECEIVE_SIZE = received;
   if(received < 0)
   {
      int error_code = zmqb_errno();
      if(!z_is_would_block_error(error_code))
         z_error();
      return "";
   }
   if(received == 0)
      return "";
   return CharArrayToString(bytes, 0, received, ZMQ_UTF8_CODEPAGE);
}

int z_last_receive_size()
{
   return ZMQ_LAST_RECEIVE_SIZE;
}

int z_set_option_int(ZMQ_HANDLE socket, int option, int value)
{
   int result = zmqb_set_option_int(socket, option, value);
   if(result < 0)
      z_error();
   return result;
}

int z_get_option_int(ZMQ_HANDLE socket, int option, int &value[])
{
   ArrayResize(value, 1);
   int result = zmqb_get_option_int(socket, option, value);
   if(result < 0)
      z_error();
   return result;
}

int z_set_option_long(ZMQ_HANDLE socket, int option, long value)
{
   int result = zmqb_set_option_int64(socket, option, value);
   if(result < 0)
      z_error();
   return result;
}

int z_get_option_long(ZMQ_HANDLE socket, int option, long &value[])
{
   ArrayResize(value, 1);
   int result = zmqb_get_option_int64(socket, option, value);
   if(result < 0)
      z_error();
   return result;
}

int z_set_option_bytes(ZMQ_HANDLE socket, int option, string value)
{
   uchar bytes[];
   int with_terminator = StringToCharArray(value, bytes, 0, -1, ZMQ_UTF8_CODEPAGE);
   int length = with_terminator > 0 ? with_terminator - 1 : 0;
   int result = zmqb_set_option_bytes(socket, option, bytes, length);
   if(result < 0)
      z_error();
   return result;
}

// Legacy string option helper. Integer options should migrate to
// z_set_option_int; subscriptions and identities remain byte options.
int z_set_sockopt(ZMQ_HANDLE socket, int option, string value)
{
   return z_set_option_bytes(socket, option, value);
}

int z_set_hwm(ZMQ_HANDLE socket, int value)
{
   return z_set_option_int(socket, ZMQ_HWM, value);
}

int z_subscribe(ZMQ_HANDLE socket, string topic)
{
   return z_set_option_bytes(socket, ZMQ_SUBSCRIBE, topic);
}

int z_unsubscribe(ZMQ_HANDLE socket, string topic)
{
   return z_set_option_bytes(socket, ZMQ_UNSUBSCRIBE, topic);
}

int z_more(ZMQ_HANDLE socket)
{
   int value[];
   ArrayResize(value, 1);
   if(zmqb_get_option_int(socket, ZMQ_RCVMORE, value) < 0)
   {
      z_error();
      return -1;
   }
   return value[0];
}

int z_close(ZMQ_HANDLE socket)
{
   if(socket == 0)
      return 0;
   int result = zmqb_socket_close(socket);
   if(result < 0)
      z_error();
   return result;
}

int z_term(ZMQ_HANDLE context)
{
   if(context == 0)
      return 0;
   int result = zmqb_context_destroy(context);
   if(result < 0)
      z_error();
   return result;
}

#endif
