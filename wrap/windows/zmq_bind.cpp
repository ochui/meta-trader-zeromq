#include "zmq_bind.h"

#include <zmq.h>

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

struct ContextState {
    void *native = nullptr;
    std::unordered_set<zmq_handle_t> sockets;
};

struct SocketState {
    void *native = nullptr;
    zmq_handle_t context = 0;
    std::vector<unsigned char> pending;
    bool has_pending = false;
};

std::mutex g_mutex;
std::unordered_map<zmq_handle_t, std::unique_ptr<ContextState>> g_contexts;
std::unordered_map<zmq_handle_t, std::unique_ptr<SocketState>> g_sockets;
zmq_handle_t g_next_handle = 1;
thread_local int g_last_error = 0;

void set_last_result(const int result) {
    g_last_error = result < 0 ? zmq_errno() : 0;
}

int fail(const int error_code = EINVAL) {
    g_last_error = error_code;
    return -1;
}

zmq_handle_t new_handle() {
    while (g_next_handle == 0 || g_contexts.count(g_next_handle) != 0 ||
           g_sockets.count(g_next_handle) != 0) {
        ++g_next_handle;
    }
    return g_next_handle++;
}

ContextState *find_context(const zmq_handle_t handle) {
    const auto it = g_contexts.find(handle);
    return it == g_contexts.end() ? nullptr : it->second.get();
}

SocketState *find_socket(const zmq_handle_t handle) {
    const auto it = g_sockets.find(handle);
    return it == g_sockets.end() ? nullptr : it->second.get();
}

int close_native_socket(SocketState &socket) {
    const int no_linger = 0;
    (void) zmq_setsockopt(socket.native, ZMQ_LINGER, &no_linger, sizeof(no_linger));
    return zmq_close(socket.native);
}

int send_bytes(SocketState &socket, const void *data, const size_t length, const int flags) {
    const int result = zmq_send(socket.native, data, length, flags);
    set_last_result(result);
    return result;
}

} // namespace

extern "C" {

zmq_handle_t ZMQ_BIND_CALL zmqb_context_create(const int io_threads) {
    if (io_threads <= 0) {
        fail();
        return 0;
    }

    void *native = zmq_ctx_new();
    if (native == nullptr) {
        g_last_error = zmq_errno();
        return 0;
    }
    if (zmq_ctx_set(native, ZMQ_IO_THREADS, io_threads) != 0) {
        g_last_error = zmq_errno();
        (void) zmq_ctx_term(native);
        return 0;
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    const zmq_handle_t handle = new_handle();
    auto state = std::make_unique<ContextState>();
    state->native = native;
    g_contexts.emplace(handle, std::move(state));
    g_last_error = 0;
    return handle;
}

int ZMQ_BIND_CALL zmqb_context_destroy(const zmq_handle_t context) {
    std::lock_guard<std::mutex> lock(g_mutex);
    ContextState *state = find_context(context);
    if (state == nullptr) {
        return fail();
    }

    for (const zmq_handle_t socket_handle : state->sockets) {
        const auto socket_it = g_sockets.find(socket_handle);
        if (socket_it != g_sockets.end()) {
            (void) close_native_socket(*socket_it->second);
            g_sockets.erase(socket_it);
        }
    }
    state->sockets.clear();

    void *native = state->native;
    g_contexts.erase(context);
    (void) zmq_ctx_shutdown(native);
    int result;
    do {
        result = zmq_ctx_term(native);
    } while (result < 0 && zmq_errno() == EINTR);
    set_last_result(result);
    return result;
}

zmq_handle_t ZMQ_BIND_CALL zmqb_socket_create(const zmq_handle_t context,
                                               const int socket_type) {
    std::lock_guard<std::mutex> lock(g_mutex);
    ContextState *context_state = find_context(context);
    if (context_state == nullptr) {
        fail();
        return 0;
    }

    void *native = zmq_socket(context_state->native, socket_type);
    if (native == nullptr) {
        g_last_error = zmq_errno();
        return 0;
    }

    const int no_linger = 0;
    if (zmq_setsockopt(native, ZMQ_LINGER, &no_linger, sizeof(no_linger)) != 0) {
        g_last_error = zmq_errno();
        (void) zmq_close(native);
        return 0;
    }

    const zmq_handle_t handle = new_handle();
    auto state = std::make_unique<SocketState>();
    state->native = native;
    state->context = context;
    g_sockets.emplace(handle, std::move(state));
    context_state->sockets.insert(handle);
    g_last_error = 0;
    return handle;
}

int ZMQ_BIND_CALL zmqb_socket_close(const zmq_handle_t socket) {
    std::lock_guard<std::mutex> lock(g_mutex);
    const auto it = g_sockets.find(socket);
    if (it == g_sockets.end()) {
        return fail();
    }

    SocketState *state = it->second.get();
    if (ContextState *context = find_context(state->context); context != nullptr) {
        context->sockets.erase(socket);
    }
    const int result = close_native_socket(*state);
    g_sockets.erase(it);
    set_last_result(result);
    return result;
}

int ZMQ_BIND_CALL zmqb_bind(const zmq_handle_t socket, const char *endpoint) {
    std::lock_guard<std::mutex> lock(g_mutex);
    SocketState *state = find_socket(socket);
    if (state == nullptr || endpoint == nullptr) {
        return fail();
    }
    const int result = zmq_bind(state->native, endpoint);
    set_last_result(result);
    return result;
}

int ZMQ_BIND_CALL zmqb_connect(const zmq_handle_t socket, const char *endpoint) {
    std::lock_guard<std::mutex> lock(g_mutex);
    SocketState *state = find_socket(socket);
    if (state == nullptr || endpoint == nullptr) {
        return fail();
    }
    const int result = zmq_connect(state->native, endpoint);
    set_last_result(result);
    return result;
}

int ZMQ_BIND_CALL zmqb_send(const zmq_handle_t socket,
                            const unsigned char *data,
                            const int length,
                            const int flags) {
    std::lock_guard<std::mutex> lock(g_mutex);
    SocketState *state = find_socket(socket);
    if (state == nullptr || length < 0 || (length > 0 && data == nullptr)) {
        return fail();
    }
    static const unsigned char empty = 0;
    return send_bytes(*state, length == 0 ? &empty : data, static_cast<size_t>(length), flags);
}

int ZMQ_BIND_CALL zmqb_send_int_array(const zmq_handle_t socket,
                                      const int *data,
                                      const int count,
                                      const int flags) {
    if (count < 0 || count > INT_MAX / static_cast<int>(sizeof(int))) {
        return fail();
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    SocketState *state = find_socket(socket);
    if (state == nullptr || (count > 0 && data == nullptr)) {
        return fail();
    }
    static const int empty = 0;
    return send_bytes(*state,
                      count == 0 ? &empty : data,
                      static_cast<size_t>(count) * sizeof(int),
                      flags);
}

int ZMQ_BIND_CALL zmqb_send_double_array(const zmq_handle_t socket,
                                         const double *data,
                                         const int count,
                                         const int flags) {
    if (count < 0 || count > INT_MAX / static_cast<int>(sizeof(double))) {
        return fail();
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    SocketState *state = find_socket(socket);
    if (state == nullptr || (count > 0 && data == nullptr)) {
        return fail();
    }
    static const double empty = 0.0;
    return send_bytes(*state,
                      count == 0 ? &empty : data,
                      static_cast<size_t>(count) * sizeof(double),
                      flags);
}

int ZMQ_BIND_CALL zmqb_receive(const zmq_handle_t socket,
                               unsigned char *buffer,
                               const int capacity,
                               const int flags) {
    std::lock_guard<std::mutex> lock(g_mutex);
    SocketState *state = find_socket(socket);
    if (state == nullptr || capacity < 0 || (capacity > 0 && buffer == nullptr)) {
        return fail();
    }

    if (!state->has_pending) {
        zmq_msg_t message;
        if (zmq_msg_init(&message) != 0) {
            g_last_error = zmq_errno();
            return -1;
        }
        const int received = zmq_msg_recv(&message, state->native, flags);
        if (received < 0) {
            g_last_error = zmq_errno();
            (void) zmq_msg_close(&message);
            return -1;
        }
        const size_t size = zmq_msg_size(&message);
        if (size > static_cast<size_t>(INT_MAX)) {
            (void) zmq_msg_close(&message);
            return fail(EMSGSIZE);
        }
        state->pending.clear();
        if (size > 0) {
            const auto *begin = static_cast<const unsigned char *>(zmq_msg_data(&message));
            state->pending.assign(begin, begin + size);
        }
        state->has_pending = true;
        (void) zmq_msg_close(&message);
    }

    const int required = static_cast<int>(state->pending.size());
    if (capacity < required) {
        g_last_error = 0;
        return required;
    }
    if (required > 0) {
        std::memcpy(buffer, state->pending.data(), static_cast<size_t>(required));
    }
    state->pending.clear();
    state->has_pending = false;
    g_last_error = 0;
    return required;
}

int ZMQ_BIND_CALL zmqb_set_option_int(const zmq_handle_t socket,
                                      const int option,
                                      const int value) {
    std::lock_guard<std::mutex> lock(g_mutex);
    SocketState *state = find_socket(socket);
    if (state == nullptr) {
        return fail();
    }

    int result;
    if (option == 1) { // Legacy ZMQ_HWM: apply to both modern watermarks.
        result = zmq_setsockopt(state->native, ZMQ_SNDHWM, &value, sizeof(value));
        if (result == 0) {
            result = zmq_setsockopt(state->native, ZMQ_RCVHWM, &value, sizeof(value));
        }
    } else {
        result = zmq_setsockopt(state->native, option, &value, sizeof(value));
    }
    set_last_result(result);
    return result;
}

int ZMQ_BIND_CALL zmqb_set_option_bytes(const zmq_handle_t socket,
                                        const int option,
                                        const unsigned char *value,
                                        const int length) {
    std::lock_guard<std::mutex> lock(g_mutex);
    SocketState *state = find_socket(socket);
    if (state == nullptr || length < 0 || (length > 0 && value == nullptr)) {
        return fail();
    }
    static const unsigned char empty = 0;
    const int result = zmq_setsockopt(state->native,
                                      option,
                                      length == 0 ? &empty : value,
                                      static_cast<size_t>(length));
    set_last_result(result);
    return result;
}

int ZMQ_BIND_CALL zmqb_get_option_int(const zmq_handle_t socket,
                                      const int option,
                                      int *value) {
    std::lock_guard<std::mutex> lock(g_mutex);
    SocketState *state = find_socket(socket);
    if (state == nullptr || value == nullptr) {
        return fail();
    }
    size_t size = sizeof(*value);
    const int result = zmq_getsockopt(state->native, option, value, &size);
    set_last_result(result);
    return result;
}

int ZMQ_BIND_CALL zmqb_poll(const zmq_handle_t socket,
                            const int events,
                            const int timeout_ms) {
    std::lock_guard<std::mutex> lock(g_mutex);
    SocketState *state = find_socket(socket);
    if (state == nullptr) {
        return fail();
    }
    zmq_pollitem_t item = {state->native, 0, static_cast<short>(events), 0};
    const int result = zmq_poll(&item, 1, static_cast<long>(timeout_ms));
    set_last_result(result);
    return result;
}

int ZMQ_BIND_CALL zmqb_errno(void) {
    return g_last_error;
}

int ZMQ_BIND_CALL zmqb_error_text(const int error_code,
                                  unsigned char *buffer,
                                  const int capacity) {
    if (capacity < 0 || (capacity > 0 && buffer == nullptr)) {
        return fail();
    }
    const char *text = zmq_strerror(error_code);
    if (text == nullptr) {
        text = "Unknown ZeroMQ error";
    }
    const size_t text_length = std::strlen(text);
    if (capacity == 0) {
        return static_cast<int>(std::min(text_length, static_cast<size_t>(INT_MAX)));
    }
    const size_t copied = std::min(text_length, static_cast<size_t>(capacity - 1));
    std::memcpy(buffer, text, copied);
    buffer[copied] = 0;
    return static_cast<int>(copied);
}

int ZMQ_BIND_CALL zmqb_version_major(void) {
    int major = 0, minor = 0, patch = 0;
    zmq_version(&major, &minor, &patch);
    return major;
}

int ZMQ_BIND_CALL zmqb_version_minor(void) {
    int major = 0, minor = 0, patch = 0;
    zmq_version(&major, &minor, &patch);
    return minor;
}

int ZMQ_BIND_CALL zmqb_version_patch(void) {
    int major = 0, minor = 0, patch = 0;
    zmq_version(&major, &minor, &patch);
    return patch;
}

} // extern "C"
