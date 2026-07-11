#pragma once

#if defined(_WIN32)
#if defined(ZMQ_BIND_TEST_EXPORTS)
#define ZMQ_BIND_TEST_API __declspec(dllexport)
#else
#define ZMQ_BIND_TEST_API __declspec(dllimport)
#endif
#else
#define ZMQ_BIND_TEST_API
#endif

enum class ZmqTestFailurePoint : int {
    none = 0,
    export_entry,
    context_state_allocation,
    context_registry_insertion,
    socket_state_allocation,
    socket_registry_insertion,
    context_ownership_insertion,
    context_destroy_storage,
    receive_pending_allocation
};

enum class ZmqTestException : int {
    bad_alloc = 1,
    length_error,
    generic
};

extern "C" {

ZMQ_BIND_TEST_API void __stdcall zmqb_test_fail_once(ZmqTestFailurePoint point,
                                                      ZmqTestException exception) noexcept;
ZMQ_BIND_TEST_API void __stdcall zmqb_test_clear_failure() noexcept;
ZMQ_BIND_TEST_API int __stdcall zmqb_test_context_registry_size() noexcept;
ZMQ_BIND_TEST_API int __stdcall zmqb_test_socket_registry_size() noexcept;
ZMQ_BIND_TEST_API int __stdcall zmqb_test_live_contexts() noexcept;
ZMQ_BIND_TEST_API int __stdcall zmqb_test_live_sockets() noexcept;
ZMQ_BIND_TEST_API int __stdcall zmqb_test_live_messages() noexcept;

}
