#include "zmq_bind.h"

#include <zmq.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <climits>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#if defined(ZMQ_BIND_TESTING)
#include "failure_injection.h"
#endif

namespace {

struct ContextState {
    void *native = nullptr;
    std::mutex mutex;
    std::atomic<bool> closing{false};
    // Protected by g_registry_mutex.
    std::unordered_set<zmq_handle_t> sockets;

    ~ContextState() noexcept;
};

struct SocketState {
    void *native = nullptr;
    zmq_handle_t context = 0;
    std::mutex mutex;
    std::atomic<bool> closing{false};
    // Protected by this socket's mutex.
    std::vector<unsigned char> pending;
    bool has_pending = false;

    ~SocketState() noexcept;
};

std::mutex g_registry_mutex;
std::unordered_map<zmq_handle_t, std::shared_ptr<ContextState>> g_contexts;
std::unordered_map<zmq_handle_t, std::shared_ptr<SocketState>> g_sockets;
zmq_handle_t g_next_handle = 1;
thread_local int g_last_error = 0;

enum class FailureStage : int {
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

#if defined(ZMQ_BIND_TESTING)
std::atomic<int> g_failure_stage{static_cast<int>(FailureStage::none)};
std::atomic<int> g_failure_exception{static_cast<int>(ZmqTestException::bad_alloc)};
std::atomic<int> g_live_context_count{0};
std::atomic<int> g_live_socket_count{0};
std::atomic<int> g_live_message_count{0};

void maybe_fail(const FailureStage stage) {
    int expected = static_cast<int>(stage);
    if (!g_failure_stage.compare_exchange_strong(
            expected, static_cast<int>(FailureStage::none))) {
        return;
    }
    switch (static_cast<ZmqTestException>(g_failure_exception.load())) {
    case ZmqTestException::bad_alloc:
        throw std::bad_alloc();
    case ZmqTestException::length_error:
        throw std::length_error("injected length error");
    case ZmqTestException::generic:
    default:
        throw std::runtime_error("injected wrapper error");
    }
}

void track_context_created() noexcept { ++g_live_context_count; }
void track_context_closed() noexcept { --g_live_context_count; }
void track_socket_created() noexcept { ++g_live_socket_count; }
void track_socket_closed() noexcept { --g_live_socket_count; }
void track_message_created() noexcept { ++g_live_message_count; }
void track_message_closed() noexcept { --g_live_message_count; }
#else
void maybe_fail(FailureStage) noexcept {}
void track_context_created() noexcept {}
void track_context_closed() noexcept {}
void track_socket_created() noexcept {}
void track_socket_closed() noexcept {}
void track_message_created() noexcept {}
void track_message_closed() noexcept {}
#endif

ContextState::~ContextState() noexcept {
    if (native == nullptr) {
        return;
    }
    (void) zmq_ctx_shutdown(native);
    int result;
    do {
        result = zmq_ctx_term(native);
    } while (result < 0 && zmq_errno() == EINTR);
    native = nullptr;
    track_context_closed();
}

SocketState::~SocketState() noexcept {
    if (native == nullptr) {
        return;
    }
    const int no_linger = 0;
    (void) zmq_setsockopt(native, ZMQ_LINGER, &no_linger, sizeof(no_linger));
    (void) zmq_close(native);
    native = nullptr;
    track_socket_closed();
}

void set_last_result(const int result) {
    g_last_error = result < 0 ? zmq_errno() : 0;
}

int fail(const int error_code = EINVAL) {
    g_last_error = error_code;
    return -1;
}

template <typename Function>
int invoke_int(Function &&function) noexcept {
    try {
        maybe_fail(FailureStage::export_entry);
        return std::forward<Function>(function)();
    } catch (const std::bad_alloc &) {
        return fail(ENOMEM);
    } catch (const std::length_error &) {
        return fail(EMSGSIZE);
    } catch (...) {
        return fail(EFAULT);
    }
}

template <typename Function>
zmq_handle_t invoke_handle(Function &&function) noexcept {
    try {
        maybe_fail(FailureStage::export_entry);
        return std::forward<Function>(function)();
    } catch (const std::bad_alloc &) {
        fail(ENOMEM);
    } catch (const std::length_error &) {
        fail(EMSGSIZE);
    } catch (...) {
        fail(EFAULT);
    }
    return 0;
}

class NativeContext final {
  public:
    explicit NativeContext(void *native = nullptr) noexcept : native_(native) {
        if (native_ != nullptr) {
            track_context_created();
        }
    }

    ~NativeContext() noexcept { reset(); }

    NativeContext(const NativeContext &) = delete;
    NativeContext &operator=(const NativeContext &) = delete;

    void *get() const noexcept { return native_; }

    void *release() noexcept {
        void *native = native_;
        native_ = nullptr;
        return native;
    }

  private:
    void reset() noexcept {
        if (native_ == nullptr) {
            return;
        }
        int result;
        do {
            result = zmq_ctx_term(native_);
        } while (result < 0 && zmq_errno() == EINTR);
        native_ = nullptr;
        track_context_closed();
    }

    void *native_;
};

class NativeSocket final {
  public:
    explicit NativeSocket(void *native = nullptr) noexcept : native_(native) {
        if (native_ != nullptr) {
            track_socket_created();
        }
    }

    ~NativeSocket() noexcept { reset(); }

    NativeSocket(const NativeSocket &) = delete;
    NativeSocket &operator=(const NativeSocket &) = delete;

    void *get() const noexcept { return native_; }

    void *release() noexcept {
        void *native = native_;
        native_ = nullptr;
        return native;
    }

  private:
    void reset() noexcept {
        if (native_ == nullptr) {
            return;
        }
        const int no_linger = 0;
        (void) zmq_setsockopt(native_, ZMQ_LINGER, &no_linger, sizeof(no_linger));
        (void) zmq_close(native_);
        native_ = nullptr;
        track_socket_closed();
    }

    void *native_;
};

class ZmqMessage final {
  public:
    ZmqMessage() noexcept : initialized_(zmq_msg_init(&message_) == 0) {
        if (initialized_) {
            track_message_created();
        }
    }

    ~ZmqMessage() noexcept {
        if (initialized_) {
            (void) zmq_msg_close(&message_);
            track_message_closed();
        }
    }

    ZmqMessage(const ZmqMessage &) = delete;
    ZmqMessage &operator=(const ZmqMessage &) = delete;

    bool initialized() const noexcept { return initialized_; }
    zmq_msg_t *get() noexcept { return &message_; }

  private:
    zmq_msg_t message_{};
    bool initialized_;
};

zmq_handle_t new_handle_locked() {
    while (g_next_handle == 0 || g_contexts.count(g_next_handle) != 0 ||
           g_sockets.count(g_next_handle) != 0) {
        ++g_next_handle;
    }
    return g_next_handle++;
}

std::shared_ptr<ContextState> find_context(const zmq_handle_t handle) {
    std::lock_guard<std::mutex> registry_lock(g_registry_mutex);
    const auto it = g_contexts.find(handle);
    if (it == g_contexts.end() || it->second->closing.load()) {
        return {};
    }
    return it->second;
}

std::shared_ptr<SocketState> find_socket(const zmq_handle_t handle) {
    std::lock_guard<std::mutex> registry_lock(g_registry_mutex);
    const auto it = g_sockets.find(handle);
    if (it == g_sockets.end() || it->second->closing.load()) {
        return {};
    }
    return it->second;
}

int close_native_socket(SocketState &socket) {
    if (socket.native == nullptr) {
        return fail();
    }
    const int no_linger = 0;
    (void) zmq_setsockopt(socket.native, ZMQ_LINGER, &no_linger, sizeof(no_linger));
    const int result = zmq_close(socket.native);
    socket.native = nullptr;
    track_socket_closed();
    return result;
}

int send_bytes(SocketState &socket, const void *data, const size_t length, const int flags) {
    const int result = zmq_send(socket.native, data, length, flags);
    set_last_result(result);
    return result;
}

bool is_int64_option(const int option) {
    return option == ZMQ_AFFINITY || option == ZMQ_MAXMSGSIZE;
}

} // namespace

extern "C" {

zmq_handle_t ZMQ_BIND_CALL zmqb_context_create(const int io_threads) noexcept {
    return invoke_handle([&]() -> zmq_handle_t {
        if (io_threads <= 0) {
            fail();
            return 0;
        }

        NativeContext native(zmq_ctx_new());
        if (native.get() == nullptr) {
            g_last_error = zmq_errno();
            return 0;
        }
        if (zmq_ctx_set(native.get(), ZMQ_IO_THREADS, io_threads) != 0) {
            g_last_error = zmq_errno();
            return 0;
        }

        maybe_fail(FailureStage::context_state_allocation);
        auto state = std::make_shared<ContextState>();
        {
            std::lock_guard<std::mutex> registry_lock(g_registry_mutex);
            const zmq_handle_t handle = new_handle_locked();
            maybe_fail(FailureStage::context_registry_insertion);
            g_contexts.emplace(handle, state);
            state->native = native.release();
            g_last_error = 0;
            return handle;
        }
    });
}

int ZMQ_BIND_CALL zmqb_context_destroy(const zmq_handle_t context) noexcept {
    return invoke_int([&]() -> int {
        std::shared_ptr<ContextState> state = find_context(context);
        if (!state) {
            return fail();
        }

        // Serializes destruction against socket creation for this context only.
        std::unique_lock<std::mutex> context_lock(state->mutex);
        std::vector<std::shared_ptr<SocketState>> owned_sockets;
        size_t owned_socket_count = 0;
        {
            std::lock_guard<std::mutex> registry_lock(g_registry_mutex);
            const auto context_it = g_contexts.find(context);
            if (context_it == g_contexts.end() || context_it->second != state ||
                state->closing.load()) {
                return fail();
            }
            owned_socket_count = state->sockets.size();
        }
        maybe_fail(FailureStage::context_destroy_storage);
        owned_sockets.reserve(owned_socket_count);
        {
            std::lock_guard<std::mutex> registry_lock(g_registry_mutex);
            const auto context_it = g_contexts.find(context);
            if (context_it == g_contexts.end() || context_it->second != state ||
                state->closing.load()) {
                return fail();
            }

            // All potentially throwing allocation completed before this point.
            for (const zmq_handle_t socket_handle : state->sockets) {
                const auto socket_it = g_sockets.find(socket_handle);
                if (socket_it != g_sockets.end()) {
                    owned_sockets.push_back(socket_it->second);
                }
            }

            state->closing.store(true);
            for (const auto &socket : owned_sockets) {
                socket->closing.store(true);
            }
            g_contexts.erase(context_it);
            for (const zmq_handle_t socket_handle : state->sockets) {
                g_sockets.erase(socket_handle);
            }
            state->sockets.clear();
        }

        // This is deliberately outside the registry mutex and before acquiring
        // any socket mutex: shutdown wakes blocking calls with ETERM.
        int first_error = 0;
        if (zmq_ctx_shutdown(state->native) != 0) {
            first_error = zmq_errno();
        }

        for (const auto &socket : owned_sockets) {
            std::lock_guard<std::mutex> socket_lock(socket->mutex);
            if (socket->native != nullptr && close_native_socket(*socket) != 0 &&
                first_error == 0) {
                first_error = zmq_errno();
            }
        }

        int result;
        do {
            result = zmq_ctx_term(state->native);
        } while (result < 0 && zmq_errno() == EINTR);
        if (result < 0 && first_error == 0) {
            first_error = zmq_errno();
        }
        state->native = nullptr;
        track_context_closed();

        if (first_error != 0) {
            return fail(first_error);
        }
        g_last_error = 0;
        return 0;
    });
}

zmq_handle_t ZMQ_BIND_CALL zmqb_socket_create(const zmq_handle_t context,
                                               const int socket_type) noexcept {
    return invoke_handle([&]() -> zmq_handle_t {
        std::shared_ptr<ContextState> context_state = find_context(context);
        if (!context_state) {
            fail();
            return 0;
        }

        // Context destruction takes this mutex before unregistering the context.
        std::lock_guard<std::mutex> context_lock(context_state->mutex);
        if (context_state->closing.load()) {
            fail();
            return 0;
        }

        NativeSocket native(zmq_socket(context_state->native, socket_type));
        if (native.get() == nullptr) {
            g_last_error = zmq_errno();
            return 0;
        }

        const int no_linger = 0;
        if (zmq_setsockopt(native.get(), ZMQ_LINGER, &no_linger, sizeof(no_linger)) != 0) {
            g_last_error = zmq_errno();
            return 0;
        }

        maybe_fail(FailureStage::socket_state_allocation);
        auto state = std::make_shared<SocketState>();
        state->context = context;

        zmq_handle_t handle = 0;
        {
            std::lock_guard<std::mutex> registry_lock(g_registry_mutex);
            const auto context_it = g_contexts.find(context);
            if (context_it == g_contexts.end() || context_it->second != context_state ||
                context_state->closing.load()) {
                fail();
                return 0;
            }

            handle = new_handle_locked();
            bool socket_registered = false;
            bool ownership_registered = false;
            try {
                maybe_fail(FailureStage::socket_registry_insertion);
                const auto socket_result = g_sockets.emplace(handle, state);
                if (!socket_result.second) {
                    throw std::runtime_error("duplicate socket handle");
                }
                socket_registered = true;

                maybe_fail(FailureStage::context_ownership_insertion);
                const auto ownership_result = context_state->sockets.insert(handle);
                if (!ownership_result.second) {
                    throw std::runtime_error("duplicate context socket handle");
                }
                ownership_registered = true;
                state->native = native.release();
            } catch (...) {
                if (ownership_registered) {
                    context_state->sockets.erase(handle);
                }
                if (socket_registered) {
                    g_sockets.erase(handle);
                }
                throw;
            }
        }
        g_last_error = 0;
        return handle;
    });
}

int ZMQ_BIND_CALL zmqb_socket_close(const zmq_handle_t socket) noexcept {
    return invoke_int([&]() -> int {
        std::shared_ptr<SocketState> state;
        {
            std::lock_guard<std::mutex> registry_lock(g_registry_mutex);
            const auto socket_it = g_sockets.find(socket);
            if (socket_it == g_sockets.end() || socket_it->second->closing.exchange(true)) {
                return fail();
            }
            state = socket_it->second;
            g_sockets.erase(socket_it);

            const auto context_it = g_contexts.find(state->context);
            if (context_it != g_contexts.end()) {
                context_it->second->sockets.erase(socket);
            }
        }

        // An in-flight operation owns state and this mutex. Close waits only for
        // that socket; it never prevents operations on unrelated sockets.
        std::lock_guard<std::mutex> socket_lock(state->mutex);
        const int result = close_native_socket(*state);
        set_last_result(result);
        return result;
    });
}

int ZMQ_BIND_CALL zmqb_bind(const zmq_handle_t socket, const char *endpoint) noexcept {
    return invoke_int([&]() -> int {
        std::shared_ptr<SocketState> state = find_socket(socket);
        if (!state || endpoint == nullptr) {
            return fail();
        }
        std::lock_guard<std::mutex> socket_lock(state->mutex);
        if (state->closing.load()) {
            return fail();
        }
        const int result = zmq_bind(state->native, endpoint);
        set_last_result(result);
        return result;
    });
}

int ZMQ_BIND_CALL zmqb_connect(const zmq_handle_t socket, const char *endpoint) noexcept {
    return invoke_int([&]() -> int {
        std::shared_ptr<SocketState> state = find_socket(socket);
        if (!state || endpoint == nullptr) {
            return fail();
        }
        std::lock_guard<std::mutex> socket_lock(state->mutex);
        if (state->closing.load()) {
            return fail();
        }
        const int result = zmq_connect(state->native, endpoint);
        set_last_result(result);
        return result;
    });
}

int ZMQ_BIND_CALL zmqb_send(const zmq_handle_t socket,
                            const unsigned char *data,
                            const int length,
                            const int flags) noexcept {
    return invoke_int([&]() -> int {
        if (length < 0 || (length > 0 && data == nullptr)) {
            return fail();
        }
        std::shared_ptr<SocketState> state = find_socket(socket);
        if (!state) {
            return fail();
        }
        std::lock_guard<std::mutex> socket_lock(state->mutex);
        if (state->closing.load()) {
            return fail();
        }
        static const unsigned char empty = 0;
        return send_bytes(
            *state, length == 0 ? &empty : data, static_cast<size_t>(length), flags);
    });
}

int ZMQ_BIND_CALL zmqb_send_int_array(const zmq_handle_t socket,
                                      const int *data,
                                      const int count,
                                      const int flags) noexcept {
    return invoke_int([&]() -> int {
        if (count < 0 || count > INT_MAX / static_cast<int>(sizeof(int)) ||
            (count > 0 && data == nullptr)) {
            return fail();
        }
        std::shared_ptr<SocketState> state = find_socket(socket);
        if (!state) {
            return fail();
        }
        std::lock_guard<std::mutex> socket_lock(state->mutex);
        if (state->closing.load()) {
            return fail();
        }
        static const int empty = 0;
        return send_bytes(*state,
                          count == 0 ? &empty : data,
                          static_cast<size_t>(count) * sizeof(int),
                          flags);
    });
}

int ZMQ_BIND_CALL zmqb_send_double_array(const zmq_handle_t socket,
                                         const double *data,
                                         const int count,
                                         const int flags) noexcept {
    return invoke_int([&]() -> int {
        if (count < 0 || count > INT_MAX / static_cast<int>(sizeof(double)) ||
            (count > 0 && data == nullptr)) {
            return fail();
        }
        std::shared_ptr<SocketState> state = find_socket(socket);
        if (!state) {
            return fail();
        }
        std::lock_guard<std::mutex> socket_lock(state->mutex);
        if (state->closing.load()) {
            return fail();
        }
        static const double empty = 0.0;
        return send_bytes(*state,
                          count == 0 ? &empty : data,
                          static_cast<size_t>(count) * sizeof(double),
                          flags);
    });
}

int ZMQ_BIND_CALL zmqb_receive(const zmq_handle_t socket,
                               unsigned char *buffer,
                               const int capacity,
                               const int flags) noexcept {
    return invoke_int([&]() -> int {
        if (capacity < 0 || (capacity > 0 && buffer == nullptr)) {
            return fail();
        }
        std::shared_ptr<SocketState> state = find_socket(socket);
        if (!state) {
            return fail();
        }
        std::lock_guard<std::mutex> socket_lock(state->mutex);
        if (state->closing.load()) {
            return fail();
        }

        if (!state->has_pending) {
            ZmqMessage message;
            if (!message.initialized()) {
                g_last_error = zmq_errno();
                return -1;
            }
            const int received = zmq_msg_recv(message.get(), state->native, flags);
            if (received < 0) {
                g_last_error = zmq_errno();
                return -1;
            }
            const size_t size = zmq_msg_size(message.get());
            if (size > static_cast<size_t>(INT_MAX)) {
                return fail(EMSGSIZE);
            }

            std::vector<unsigned char> received_data;
            maybe_fail(FailureStage::receive_pending_allocation);
            if (size > 0) {
                const auto *begin =
                    static_cast<const unsigned char *>(zmq_msg_data(message.get()));
                received_data.assign(begin, begin + size);
            }
            state->pending.swap(received_data);
            state->has_pending = true;
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
    });
}

int ZMQ_BIND_CALL zmqb_set_option_int(const zmq_handle_t socket,
                                      const int option,
                                      const int value) noexcept {
    return invoke_int([&]() -> int {
        if (is_int64_option(option)) {
            return fail();
        }
        std::shared_ptr<SocketState> state = find_socket(socket);
        if (!state) {
            return fail();
        }
        std::lock_guard<std::mutex> socket_lock(state->mutex);
        if (state->closing.load()) {
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
    });
}

int ZMQ_BIND_CALL zmqb_set_option_int64(const zmq_handle_t socket,
                                        const int option,
                                        const int64_t value) noexcept {
    return invoke_int([&]() -> int {
        if (!is_int64_option(option)) {
            return fail();
        }
        std::shared_ptr<SocketState> state = find_socket(socket);
        if (!state) {
            return fail();
        }
        std::lock_guard<std::mutex> socket_lock(state->mutex);
        if (state->closing.load()) {
            return fail();
        }
        const int result = zmq_setsockopt(state->native, option, &value, sizeof(value));
        set_last_result(result);
        return result;
    });
}

int ZMQ_BIND_CALL zmqb_set_option_bytes(const zmq_handle_t socket,
                                        const int option,
                                        const unsigned char *value,
                                        const int length) noexcept {
    return invoke_int([&]() -> int {
        if (length < 0 || (length > 0 && value == nullptr)) {
            return fail();
        }
        std::shared_ptr<SocketState> state = find_socket(socket);
        if (!state) {
            return fail();
        }
        std::lock_guard<std::mutex> socket_lock(state->mutex);
        if (state->closing.load()) {
            return fail();
        }
        static const unsigned char empty = 0;
        const int result = zmq_setsockopt(state->native,
                                          option,
                                          length == 0 ? &empty : value,
                                          static_cast<size_t>(length));
        set_last_result(result);
        return result;
    });
}

int ZMQ_BIND_CALL zmqb_get_option_int(const zmq_handle_t socket,
                                      const int option,
                                      int *value) noexcept {
    return invoke_int([&]() -> int {
        if (value == nullptr || is_int64_option(option)) {
            return fail();
        }
        std::shared_ptr<SocketState> state = find_socket(socket);
        if (!state) {
            return fail();
        }
        std::lock_guard<std::mutex> socket_lock(state->mutex);
        if (state->closing.load()) {
            return fail();
        }
        size_t size = sizeof(*value);
        const int result = zmq_getsockopt(state->native, option, value, &size);
        set_last_result(result);
        return result;
    });
}

int ZMQ_BIND_CALL zmqb_get_option_int64(const zmq_handle_t socket,
                                        const int option,
                                        int64_t *value) noexcept {
    return invoke_int([&]() -> int {
        if (value == nullptr || !is_int64_option(option)) {
            return fail();
        }
        std::shared_ptr<SocketState> state = find_socket(socket);
        if (!state) {
            return fail();
        }
        std::lock_guard<std::mutex> socket_lock(state->mutex);
        if (state->closing.load()) {
            return fail();
        }
        size_t size = sizeof(*value);
        const int result = zmq_getsockopt(state->native, option, value, &size);
        set_last_result(result);
        return result;
    });
}

int ZMQ_BIND_CALL zmqb_poll(const zmq_handle_t socket,
                            const int events,
                            const int timeout_ms) noexcept {
    return invoke_int([&]() -> int {
        std::shared_ptr<SocketState> state = find_socket(socket);
        if (!state) {
            return fail();
        }
        std::lock_guard<std::mutex> socket_lock(state->mutex);
        if (state->closing.load()) {
            return fail();
        }
        if (state->has_pending && (events & ZMQ_POLLIN) != 0) {
            g_last_error = 0;
            return 1;
        }
        zmq_pollitem_t item = {state->native, 0, static_cast<short>(events), 0};
        const int result = zmq_poll(&item, 1, static_cast<long>(timeout_ms));
        set_last_result(result);
        return result;
    });
}

int ZMQ_BIND_CALL zmqb_errno(void) noexcept {
    return invoke_int([&]() -> int { return g_last_error; });
}

int ZMQ_BIND_CALL zmqb_is_would_block(const int error_code) noexcept {
    return invoke_int([&]() -> int { return error_code == EAGAIN ? 1 : 0; });
}

int ZMQ_BIND_CALL zmqb_error_text(const int error_code,
                                  unsigned char *buffer,
                                  const int capacity) noexcept {
    return invoke_int([&]() -> int {
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
    });
}

int ZMQ_BIND_CALL zmqb_version_major(void) noexcept {
    return invoke_int([&]() -> int {
        int major = 0, minor = 0, patch = 0;
        zmq_version(&major, &minor, &patch);
        return major;
    });
}

int ZMQ_BIND_CALL zmqb_version_minor(void) noexcept {
    return invoke_int([&]() -> int {
        int major = 0, minor = 0, patch = 0;
        zmq_version(&major, &minor, &patch);
        return minor;
    });
}

int ZMQ_BIND_CALL zmqb_version_patch(void) noexcept {
    return invoke_int([&]() -> int {
        int major = 0, minor = 0, patch = 0;
        zmq_version(&major, &minor, &patch);
        return patch;
    });
}

} // extern "C"

#if defined(ZMQ_BIND_TESTING)
extern "C" {

void ZMQ_BIND_CALL zmqb_test_fail_once(const ZmqTestFailurePoint point,
                                       const ZmqTestException exception) noexcept {
    g_failure_exception.store(static_cast<int>(exception));
    g_failure_stage.store(static_cast<int>(point));
}

void ZMQ_BIND_CALL zmqb_test_clear_failure() noexcept {
    g_failure_stage.store(static_cast<int>(FailureStage::none));
}

int ZMQ_BIND_CALL zmqb_test_context_registry_size() noexcept {
    try {
        std::lock_guard<std::mutex> registry_lock(g_registry_mutex);
        return static_cast<int>(g_contexts.size());
    } catch (...) {
        return -1;
    }
}

int ZMQ_BIND_CALL zmqb_test_socket_registry_size() noexcept {
    try {
        std::lock_guard<std::mutex> registry_lock(g_registry_mutex);
        return static_cast<int>(g_sockets.size());
    } catch (...) {
        return -1;
    }
}

int ZMQ_BIND_CALL zmqb_test_live_contexts() noexcept {
    return g_live_context_count.load();
}

int ZMQ_BIND_CALL zmqb_test_live_sockets() noexcept {
    return g_live_socket_count.load();
}

int ZMQ_BIND_CALL zmqb_test_live_messages() noexcept {
    return g_live_message_count.load();
}

} // extern "C"
#endif
