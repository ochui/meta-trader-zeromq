#pragma once

#include <stdint.h>

typedef intptr_t zmq_handle_t;

#ifdef __cplusplus
#define ZMQ_BIND_NOEXCEPT noexcept
#else
#define ZMQ_BIND_NOEXCEPT
#endif

#if defined(_WIN32)
#define ZMQ_BIND_CALL __stdcall
#if defined(ZMQ_BIND_EXPORTS)
#define ZMQ_BIND_API __declspec(dllexport)
#else
#define ZMQ_BIND_API __declspec(dllimport)
#endif
#else
#define ZMQ_BIND_CALL
#define ZMQ_BIND_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

ZMQ_BIND_API zmq_handle_t ZMQ_BIND_CALL zmqb_context_create(int io_threads) ZMQ_BIND_NOEXCEPT;
ZMQ_BIND_API int ZMQ_BIND_CALL zmqb_context_destroy(zmq_handle_t context) ZMQ_BIND_NOEXCEPT;

ZMQ_BIND_API zmq_handle_t ZMQ_BIND_CALL zmqb_socket_create(zmq_handle_t context,
                                                           int socket_type) ZMQ_BIND_NOEXCEPT;
ZMQ_BIND_API int ZMQ_BIND_CALL zmqb_socket_close(zmq_handle_t socket) ZMQ_BIND_NOEXCEPT;
ZMQ_BIND_API int ZMQ_BIND_CALL zmqb_bind(zmq_handle_t socket,
                                        const char *endpoint) ZMQ_BIND_NOEXCEPT;
ZMQ_BIND_API int ZMQ_BIND_CALL zmqb_connect(zmq_handle_t socket,
                                           const char *endpoint) ZMQ_BIND_NOEXCEPT;

ZMQ_BIND_API int ZMQ_BIND_CALL zmqb_send(zmq_handle_t socket,
                                         const unsigned char *data,
                                         int length,
                                         int flags) ZMQ_BIND_NOEXCEPT;
ZMQ_BIND_API int ZMQ_BIND_CALL zmqb_send_int_array(zmq_handle_t socket,
                                                   const int *data,
                                                   int count,
                                                   int flags) ZMQ_BIND_NOEXCEPT;
ZMQ_BIND_API int ZMQ_BIND_CALL zmqb_send_double_array(zmq_handle_t socket,
                                                      const double *data,
                                                      int count,
                                                      int flags) ZMQ_BIND_NOEXCEPT;

// If capacity is too small, the message is retained by the wrapper and its
// required size is returned. Call again with a large enough buffer to copy it.
ZMQ_BIND_API int ZMQ_BIND_CALL zmqb_receive(zmq_handle_t socket,
                                            unsigned char *buffer,
                                            int capacity,
                                            int flags) ZMQ_BIND_NOEXCEPT;

ZMQ_BIND_API int ZMQ_BIND_CALL zmqb_set_option_int(zmq_handle_t socket,
                                                   int option,
                                                   int value) ZMQ_BIND_NOEXCEPT;
ZMQ_BIND_API int ZMQ_BIND_CALL zmqb_set_option_int64(zmq_handle_t socket,
                                                     int option,
                                                     int64_t value) ZMQ_BIND_NOEXCEPT;
ZMQ_BIND_API int ZMQ_BIND_CALL zmqb_set_option_bytes(zmq_handle_t socket,
                                                     int option,
                                                     const unsigned char *value,
                                                     int length) ZMQ_BIND_NOEXCEPT;
ZMQ_BIND_API int ZMQ_BIND_CALL zmqb_get_option_int(zmq_handle_t socket,
                                                   int option,
                                                   int *value) ZMQ_BIND_NOEXCEPT;
ZMQ_BIND_API int ZMQ_BIND_CALL zmqb_get_option_int64(zmq_handle_t socket,
                                                     int option,
                                                     int64_t *value) ZMQ_BIND_NOEXCEPT;
ZMQ_BIND_API int ZMQ_BIND_CALL zmqb_poll(zmq_handle_t socket,
                                        int events,
                                        int timeout_ms) ZMQ_BIND_NOEXCEPT;

ZMQ_BIND_API int ZMQ_BIND_CALL zmqb_errno(void) ZMQ_BIND_NOEXCEPT;
ZMQ_BIND_API int ZMQ_BIND_CALL zmqb_is_would_block(int error_code) ZMQ_BIND_NOEXCEPT;
ZMQ_BIND_API int ZMQ_BIND_CALL zmqb_error_text(int error_code,
                                               unsigned char *buffer,
                                               int capacity) ZMQ_BIND_NOEXCEPT;
ZMQ_BIND_API int ZMQ_BIND_CALL zmqb_version_major(void) ZMQ_BIND_NOEXCEPT;
ZMQ_BIND_API int ZMQ_BIND_CALL zmqb_version_minor(void) ZMQ_BIND_NOEXCEPT;
ZMQ_BIND_API int ZMQ_BIND_CALL zmqb_version_patch(void) ZMQ_BIND_NOEXCEPT;

#ifdef __cplusplus
}
#endif
