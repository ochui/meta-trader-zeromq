#property strict

#include <zmq_bind.mqh>

ZMQ_HANDLE g_context = 0;
ZMQ_HANDLE g_subscriber = 0;

int OnInit()
{
   g_context = z_init(1);
   if(g_context == 0)
      return INIT_FAILED;
   g_subscriber = z_socket(g_context, ZMQ_SUB);
   if(g_subscriber == 0 || z_subscribe(g_subscriber, "prices|") < 0 ||
      z_connect(g_subscriber, "tcp://127.0.0.1:5556") < 0)
      return INIT_FAILED;
   EventSetMillisecondTimer(100);
   Print("MT4 subscriber ready; libzmq ", z_version_string());
   return INIT_SUCCEEDED;
}

void OnTimer()
{
   while(z_poll_socket(g_subscriber, 0) > 0)
   {
      string message = z_recv(g_subscriber, ZMQ_DONTWAIT);
      if(z_last_receive_size() >= 0)
         Print("Received: ", message);
   }
}

void OnDeinit(const int reason)
{
   EventKillTimer();
   z_close(g_subscriber);
   z_term(g_context);
}

void OnTick() {}
