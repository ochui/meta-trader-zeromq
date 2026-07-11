#property strict

#include <zmq_bind.mqh>

ZMQ_HANDLE g_context = 0;
ZMQ_HANDLE g_publisher = 0;
int sequence = 0;

int OnInit()
{
   g_context = z_init(1);
   if(g_context == 0)
      return INIT_FAILED;
   g_publisher = z_socket(g_context, ZMQ_PUB);
   if(g_publisher == 0 || z_bind(g_publisher, "tcp://*:5556") < 0)
      return INIT_FAILED;
   EventSetTimer(1);
   Print("MT5 publisher ready; libzmq ", z_version_string());
   return INIT_SUCCEEDED;
}

void OnTimer()
{
   string message = "prices|source=MT5|sequence=" + IntegerToString(sequence++) + "|text=Héllo 世界";
   if(z_send(g_publisher, message) >= 0)
      Print("Sent: ", message);
}

void OnDeinit(const int reason)
{
   EventKillTimer();
   z_close(g_publisher);
   z_term(g_context);
}

void OnTick() {}
