#include "zmq_bind.h"

#ifdef __cplusplus
#error "c_header_smoke.c must compile as C"
#endif

_Static_assert(sizeof(zmq_handle_t) == sizeof(void *),
               "zmq_handle_t must remain pointer-sized");

int zmq_bind_c_header_smoke(void)
{
    (void) &zmqb_context_create;
    (void) &zmqb_context_destroy;
    (void) &zmqb_socket_create;
    (void) &zmqb_send;
    (void) &zmqb_receive;
    return 0;
}
