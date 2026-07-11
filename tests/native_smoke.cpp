#include "zmq_bind.h"
#include "failure_injection.h"

#include <zmq.h>

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

struct CallResult {
    int result;
    int error;
};

[[noreturn]] void die(const std::string &message) {
    std::cerr << "FAIL: " << message << " (zmq error " << zmqb_errno() << ")\n";
    std::exit(EXIT_FAILURE);
}

void require(const bool condition, const std::string &message) {
    if (!condition) {
        die(message);
    }
}

void require_no_test_leaks(const std::string &stage) {
    require(zmqb_test_context_registry_size() == 0, stage + ": stale context handle");
    require(zmqb_test_socket_registry_size() == 0, stage + ": stale socket handle");
    require(zmqb_test_live_contexts() == 0, stage + ": native context leak");
    require(zmqb_test_live_sockets() == 0, stage + ": native socket leak");
    require(zmqb_test_live_messages() == 0, stage + ": native message leak");
}

template <typename Function>
int call_without_escape(Function &&function, const std::string &stage) {
    try {
        return function();
    } catch (...) {
        die(stage + ": C++ exception escaped the C ABI");
    }
}

template <typename Function>
void expect_int_boundary(Function &&function, const std::string &name) {
    zmqb_test_fail_once(ZmqTestFailurePoint::export_entry, ZmqTestException::generic);
    const int result = call_without_escape(std::forward<Function>(function), name);
    zmqb_test_clear_failure();
    require(result == -1, name + ": exception boundary did not return -1");
    require(zmqb_errno() == EFAULT, name + ": exception boundary did not report EFAULT");
}

template <typename Function>
void expect_handle_boundary(Function &&function, const std::string &name) {
    zmqb_test_fail_once(ZmqTestFailurePoint::export_entry, ZmqTestException::generic);
    zmq_handle_t result = 0;
    try {
        result = function();
    } catch (...) {
        die(name + ": C++ exception escaped the handle C ABI");
    }
    zmqb_test_clear_failure();
    require(result == 0, name + ": exception boundary did not return 0");
    require(zmqb_errno() == EFAULT, name + ": exception boundary did not report EFAULT");
}

void send_text(const zmq_handle_t socket, const std::string &message) {
    const auto *data = reinterpret_cast<const unsigned char *>(message.data());
    require(zmqb_send(socket, data, static_cast<int>(message.size()), 0) ==
                static_cast<int>(message.size()),
            "send failed");
}

std::string receive_text(const zmq_handle_t socket, const int flags = 0) {
    std::vector<unsigned char> buffer(7);
    int received = zmqb_receive(socket, buffer.data(), static_cast<int>(buffer.size()), flags);
    if (received < 0) {
        return {};
    }
    if (received > static_cast<int>(buffer.size())) {
        buffer.resize(static_cast<size_t>(received));
        received = zmqb_receive(socket, buffer.data(), static_cast<int>(buffer.size()), flags);
    }
    require(received >= 0, "receive retry failed");
    return {reinterpret_cast<const char *>(buffer.data()), static_cast<size_t>(received)};
}

std::future<CallResult> start_receive(const zmq_handle_t socket,
                                      std::promise<void> started,
                                      const int flags = 0) {
    return std::async(std::launch::async,
                      [socket, started = std::move(started), flags]() mutable {
                          unsigned char byte = 0;
                          started.set_value();
                          const int result = zmqb_receive(socket, &byte, 1, flags);
                          return CallResult{result, zmqb_errno()};
                      });
}

void test_pub_sub() {
    const zmq_handle_t context = zmqb_context_create(1);
    require(context != 0, "context creation failed");
    const zmq_handle_t publisher = zmqb_socket_create(context, ZMQ_PUB);
    const zmq_handle_t subscriber = zmqb_socket_create(context, ZMQ_SUB);
    require(publisher != 0 && subscriber != 0, "PUB/SUB socket creation failed");

    const unsigned char empty_topic = 0;
    require(zmqb_set_option_bytes(subscriber, ZMQ_SUBSCRIBE, &empty_topic, 0) == 0,
            "subscription failed");
    require(zmqb_bind(publisher, "inproc://native-pub-sub") == 0, "PUB bind failed");
    require(zmqb_connect(subscriber, "inproc://native-pub-sub") == 0, "SUB connect failed");
    std::this_thread::sleep_for(30ms);

    const std::string long_message =
        std::string("topic|key=value|UTF-8: Привет мир | こんにちは | ") +
        std::string(200000, 'x');
    send_text(publisher, long_message);
    require(zmqb_poll(subscriber, ZMQ_POLLIN, 1000) == 1,
            "long PUB/SUB message timed out");
    require(receive_text(subscriber) == long_message, "long UTF-8 message changed in transit");

    for (int i = 0; i < 20; ++i) {
        send_text(publisher, "quick=" + std::to_string(i));
    }
    for (int i = 0; i < 20; ++i) {
        require(zmqb_poll(subscriber, ZMQ_POLLIN, 1000) == 1, "quick message timed out");
        require(receive_text(subscriber) == "quick=" + std::to_string(i),
                "quick message order changed");
    }

    require(zmqb_socket_close(subscriber) == 0, "SUB close failed");
    require(zmqb_socket_close(publisher) == 0, "PUB close failed");
    require(zmqb_context_destroy(context) == 0, "PUB/SUB context destroy failed");
}

void test_nonblocking_and_empty_receive() {
    const zmq_handle_t context = zmqb_context_create(1);
    const zmq_handle_t push = zmqb_socket_create(context, ZMQ_PUSH);
    const zmq_handle_t pull = zmqb_socket_create(context, ZMQ_PULL);
    require(context != 0 && push != 0 && pull != 0, "PUSH/PULL setup failed");
    require(zmqb_bind(push, "inproc://native-receive-semantics") == 0, "PUSH bind failed");
    require(zmqb_connect(pull, "inproc://native-receive-semantics") == 0,
            "PULL connect failed");

    unsigned char buffer[8] = {};
    require(zmqb_receive(pull, buffer, sizeof(buffer), ZMQ_DONTWAIT) == -1,
            "non-blocking receive should report no message");
    const int would_block_error = zmqb_errno();
    require(zmqb_is_would_block(would_block_error) == 1,
            "non-blocking receive error was not classified as would-block");

    send_text(push, "");
    require(zmqb_poll(pull, ZMQ_POLLIN, 1000) == 1, "empty message timed out");
    require(zmqb_receive(pull, buffer, sizeof(buffer), ZMQ_DONTWAIT) == 0,
            "empty message was not distinguishable from would-block");

    require(zmqb_socket_close(pull) == 0, "PULL close failed");
    require(zmqb_receive(pull, buffer, sizeof(buffer), ZMQ_DONTWAIT) == -1,
            "receive after close should fail");
    require(zmqb_is_would_block(zmqb_errno()) == 0,
            "a real receive error was incorrectly classified as would-block");

    require(zmqb_socket_close(push) == 0, "PUSH close failed");
    require(zmqb_context_destroy(context) == 0, "receive semantics context destroy failed");
}

void test_retained_message_poll() {
    const zmq_handle_t context = zmqb_context_create(1);
    const zmq_handle_t push = zmqb_socket_create(context, ZMQ_PUSH);
    const zmq_handle_t pull = zmqb_socket_create(context, ZMQ_PULL);
    require(context != 0 && push != 0 && pull != 0, "retained-message setup failed");
    require(zmqb_bind(push, "inproc://native-retained-poll") == 0,
            "retained-message PUSH bind failed");
    require(zmqb_connect(pull, "inproc://native-retained-poll") == 0,
            "retained-message PULL connect failed");

    const std::string original = std::string("retained|key=value|") + std::string(8192, 'r');
    send_text(push, original);
    require(zmqb_poll(pull, ZMQ_POLLIN, 1000) == 1, "retained message did not arrive");

    unsigned char small[8] = {};
    const int required = zmqb_receive(pull, small, sizeof(small), 0);
    require(required == static_cast<int>(original.size()),
            "undersized receive did not return the required size");
    require(zmqb_poll(pull, ZMQ_POLLIN, 0) == 1,
            "poll did not report the retained message as readable");

    std::vector<unsigned char> full(static_cast<size_t>(required));
    require(zmqb_receive(pull, full.data(), required, 0) == required,
            "retained message retry failed");
    require(std::string(reinterpret_cast<const char *>(full.data()), full.size()) == original,
            "retained message changed before retry");
    require(zmqb_poll(pull, ZMQ_POLLIN, 0) == 0,
            "poll reported a retained message after it was consumed");

    require(zmqb_socket_close(pull) == 0, "retained-message PULL close failed");
    require(zmqb_socket_close(push) == 0, "retained-message PUSH close failed");
    require(zmqb_context_destroy(context) == 0,
            "retained-message context destroy failed");
}

void test_typed_socket_options() {
    const zmq_handle_t context = zmqb_context_create(1);
    const zmq_handle_t socket = zmqb_socket_create(context, ZMQ_PULL);
    require(context != 0 && socket != 0, "socket-option setup failed");

    const int64_t affinity = INT64_C(0x100000001);
    require(zmqb_set_option_int64(socket, ZMQ_AFFINITY, affinity) == 0,
            "64-bit ZMQ_AFFINITY set failed");
    int64_t affinity_read = 0;
    require(zmqb_get_option_int64(socket, ZMQ_AFFINITY, &affinity_read) == 0 &&
                affinity_read == affinity,
            "64-bit ZMQ_AFFINITY was truncated");

    const int64_t max_message_size = INT64_C(5368709120);
    require(zmqb_set_option_int64(socket, ZMQ_MAXMSGSIZE, max_message_size) == 0,
            "64-bit ZMQ_MAXMSGSIZE set failed");
    int64_t max_message_size_read = 0;
    require(zmqb_get_option_int64(socket, ZMQ_MAXMSGSIZE, &max_message_size_read) == 0 &&
                max_message_size_read == max_message_size,
            "64-bit ZMQ_MAXMSGSIZE was truncated");

    require(zmqb_set_option_int(socket, ZMQ_AFFINITY, 1) == -1,
            "32-bit API accepted a 64-bit option");
    int wrong_width = 0;
    require(zmqb_get_option_int(socket, ZMQ_MAXMSGSIZE, &wrong_width) == -1,
            "32-bit getter accepted a 64-bit option");
    require(zmqb_set_option_int64(socket, ZMQ_LINGER, 0) == -1,
            "64-bit API accepted a 32-bit option");

    require(zmqb_set_option_int(socket, 1, 321) == 0, "legacy ZMQ_HWM set failed");
    int send_hwm = 0;
    int receive_hwm = 0;
    require(zmqb_get_option_int(socket, ZMQ_SNDHWM, &send_hwm) == 0 && send_hwm == 321,
            "legacy ZMQ_HWM did not set ZMQ_SNDHWM");
    require(zmqb_get_option_int(socket, ZMQ_RCVHWM, &receive_hwm) == 0 &&
                receive_hwm == 321,
            "legacy ZMQ_HWM did not set ZMQ_RCVHWM");

    require(zmqb_socket_close(socket) == 0, "socket-option close failed");
    require(zmqb_context_destroy(context) == 0, "socket-option context destroy failed");
}

void test_independent_contexts() {
    const zmq_handle_t context_a = zmqb_context_create(1);
    const zmq_handle_t wait_socket = zmqb_socket_create(context_a, ZMQ_PULL);
    require(context_a != 0 && wait_socket != 0, "independent context A setup failed");
    require(zmqb_set_option_int(wait_socket, ZMQ_RCVTIMEO, 1200) == 0,
            "context A receive timeout failed");

    std::promise<void> started;
    auto started_future = started.get_future();
    auto waiting_receive = start_receive(wait_socket, std::move(started));
    started_future.wait();
    std::this_thread::sleep_for(50ms);

    const auto begin = Clock::now();
    const zmq_handle_t context_b = zmqb_context_create(1);
    const zmq_handle_t push_b = zmqb_socket_create(context_b, ZMQ_PUSH);
    const zmq_handle_t pull_b = zmqb_socket_create(context_b, ZMQ_PULL);
    require(context_b != 0 && push_b != 0 && pull_b != 0,
            "independent context B setup failed");
    require(zmqb_bind(push_b, "inproc://independent-context-b") == 0,
            "context B bind failed");
    require(zmqb_connect(pull_b, "inproc://independent-context-b") == 0,
            "context B connect failed");
    send_text(push_b, "context-b");
    require(zmqb_poll(pull_b, ZMQ_POLLIN, 500) == 1, "context B poll failed");
    require(receive_text(pull_b) == "context-b", "context B receive failed");
    require(zmqb_socket_close(pull_b) == 0 && zmqb_socket_close(push_b) == 0,
            "context B close failed");
    require(zmqb_context_destroy(context_b) == 0, "context B destroy failed");
    const auto elapsed = Clock::now() - begin;
    require(elapsed < 800ms, "context B was blocked by context A receive");

    require(waiting_receive.wait_for(2s) == std::future_status::ready,
            "context A timed receive hung");
    const CallResult wait_result = waiting_receive.get();
    require(wait_result.result == -1 && zmqb_is_would_block(wait_result.error) == 1,
            "context A timed receive returned an unexpected result");
    require(zmqb_socket_close(wait_socket) == 0, "context A socket close failed");
    require(zmqb_context_destroy(context_a) == 0, "context A destroy failed");
}

void test_independent_sockets_one_context() {
    const zmq_handle_t context = zmqb_context_create(1);
    const zmq_handle_t wait_socket = zmqb_socket_create(context, ZMQ_PULL);
    const zmq_handle_t push = zmqb_socket_create(context, ZMQ_PUSH);
    const zmq_handle_t pull = zmqb_socket_create(context, ZMQ_PULL);
    require(context != 0 && wait_socket != 0 && push != 0 && pull != 0,
            "independent sockets setup failed");
    require(zmqb_set_option_int(wait_socket, ZMQ_RCVTIMEO, 1200) == 0,
            "independent socket timeout failed");
    require(zmqb_bind(push, "inproc://independent-sockets") == 0,
            "independent PUSH bind failed");
    require(zmqb_connect(pull, "inproc://independent-sockets") == 0,
            "independent PULL connect failed");

    std::promise<void> started;
    auto started_future = started.get_future();
    auto waiting_receive = start_receive(wait_socket, std::move(started));
    started_future.wait();
    std::this_thread::sleep_for(50ms);

    const auto begin = Clock::now();
    send_text(push, "other-socket");
    require(zmqb_poll(pull, ZMQ_POLLIN, 500) == 1, "other socket poll failed");
    require(receive_text(pull) == "other-socket", "other socket receive failed");
    require(Clock::now() - begin < 800ms, "one socket blocked another socket");

    require(waiting_receive.wait_for(2s) == std::future_status::ready,
            "independent socket timed receive hung");
    const CallResult wait_result = waiting_receive.get();
    require(wait_result.result == -1 && zmqb_is_would_block(wait_result.error) == 1,
            "independent socket timed receive returned an unexpected result");

    require(zmqb_socket_close(wait_socket) == 0, "wait socket close failed");
    require(zmqb_socket_close(pull) == 0 && zmqb_socket_close(push) == 0,
            "independent data socket close failed");
    require(zmqb_context_destroy(context) == 0, "independent sockets context destroy failed");
}

void test_poll_close_race() {
    const zmq_handle_t context = zmqb_context_create(1);
    const zmq_handle_t socket = zmqb_socket_create(context, ZMQ_PULL);
    require(context != 0 && socket != 0, "poll-close setup failed");

    std::promise<void> started;
    auto started_future = started.get_future();
    auto polling = std::async(std::launch::async,
                              [socket, started = std::move(started)]() mutable {
                                  started.set_value();
                                  const int result = zmqb_poll(socket, ZMQ_POLLIN, 250);
                                  return CallResult{result, zmqb_errno()};
                              });
    started_future.wait();
    std::this_thread::sleep_for(30ms);

    const auto begin = Clock::now();
    require(zmqb_socket_close(socket) == 0, "close racing poll failed");
    require(Clock::now() - begin < 1500ms, "close racing poll was not bounded");
    require(polling.wait_for(1s) == std::future_status::ready, "poll-close race hung");
    const CallResult poll_result = polling.get();
    require(poll_result.result == 0 || poll_result.result == -1,
            "poll-close race returned an invalid result");

    require(zmqb_socket_close(socket) == -1, "repeated socket close should fail");
    require(zmqb_poll(socket, ZMQ_POLLIN, 0) == -1, "poll after close should fail");
    require(zmqb_context_destroy(context) == 0, "poll-close context destroy failed");
}

void test_send_close_race() {
    const zmq_handle_t context = zmqb_context_create(1);
    const zmq_handle_t socket = zmqb_socket_create(context, ZMQ_PUSH);
    require(context != 0 && socket != 0, "send-close setup failed");
    require(zmqb_set_option_int(socket, ZMQ_SNDTIMEO, 250) == 0,
            "send-close timeout setup failed");

    std::promise<void> started;
    auto started_future = started.get_future();
    auto sending = std::async(std::launch::async,
                              [socket, started = std::move(started)]() mutable {
                                  const unsigned char byte = 7;
                                  started.set_value();
                                  const int result = zmqb_send(socket, &byte, 1, 0);
                                  return CallResult{result, zmqb_errno()};
                              });
    started_future.wait();
    std::this_thread::sleep_for(30ms);

    const auto begin = Clock::now();
    require(zmqb_socket_close(socket) == 0, "close racing send failed");
    require(Clock::now() - begin < 1500ms, "close racing send was not bounded");
    require(sending.wait_for(1s) == std::future_status::ready, "send-close race hung");
    const CallResult send_result = sending.get();
    require(send_result.result == -1 && zmqb_is_would_block(send_result.error) == 1,
            "send-close race returned an unexpected result");
    require(zmqb_context_destroy(context) == 0, "send-close context destroy failed");
}

void test_receive_context_destroy_race() {
    const zmq_handle_t context = zmqb_context_create(1);
    const zmq_handle_t socket = zmqb_socket_create(context, ZMQ_PULL);
    require(context != 0 && socket != 0, "receive-destroy setup failed");
    require(zmqb_set_option_int(socket, ZMQ_RCVTIMEO, 5000) == 0,
            "receive-destroy timeout setup failed");

    std::promise<void> started;
    auto started_future = started.get_future();
    auto receiving = start_receive(socket, std::move(started));
    started_future.wait();
    std::this_thread::sleep_for(50ms);

    const auto begin = Clock::now();
    require(zmqb_context_destroy(context) == 0, "destroy racing receive failed");
    require(Clock::now() - begin < 1500ms, "context destroy did not interrupt receive");
    require(receiving.wait_for(1s) == std::future_status::ready,
            "receive did not finish after context shutdown");
    const CallResult receive_result = receiving.get();
    require(receive_result.result == -1 &&
                (receive_result.error == ETERM || receive_result.error == EINVAL),
            "receive-destroy race returned an unexpected result");

    unsigned char byte = 0;
    require(zmqb_receive(socket, &byte, 1, ZMQ_DONTWAIT) == -1,
            "receive after context destroy should fail");
    require(zmqb_send(socket, &byte, 1, ZMQ_DONTWAIT) == -1,
            "send after context destroy should fail");
    require(zmqb_socket_close(socket) == -1,
            "socket close after context-owned cleanup should fail");
    require(zmqb_context_destroy(context) == -1,
            "repeated context destruction should fail");
}

void test_explicit_close_context_destroy_race() {
    const zmq_handle_t context = zmqb_context_create(1);
    const zmq_handle_t socket = zmqb_socket_create(context, ZMQ_PUSH);
    require(context != 0 && socket != 0, "close-destroy setup failed");

    std::promise<void> release;
    std::shared_future<void> start = release.get_future().share();
    auto closing = std::async(std::launch::async, [socket, start]() {
        start.wait();
        const int result = zmqb_socket_close(socket);
        return CallResult{result, zmqb_errno()};
    });
    auto destroying = std::async(std::launch::async, [context, start]() {
        start.wait();
        const int result = zmqb_context_destroy(context);
        return CallResult{result, zmqb_errno()};
    });
    release.set_value();

    require(closing.wait_for(2s) == std::future_status::ready,
            "explicit close racing context destroy hung");
    require(destroying.wait_for(2s) == std::future_status::ready,
            "context destroy racing explicit close hung");
    const CallResult close_result = closing.get();
    const CallResult destroy_result = destroying.get();
    require(close_result.result == 0 || close_result.result == -1,
            "close-destroy race returned an invalid close result");
    require(destroy_result.result == 0, "close-destroy race failed context destruction");
    require(zmqb_socket_close(socket) == -1, "close-destroy race left socket registered");
    require(zmqb_context_destroy(context) == -1,
            "close-destroy race left context registered");
}

void test_context_destroy_forces_zero_linger() {
    const zmq_handle_t context = zmqb_context_create(1);
    const zmq_handle_t socket = zmqb_socket_create(context, ZMQ_PUSH);
    require(context != 0 && socket != 0, "zero-linger setup failed");
    require(zmqb_set_option_int(socket, ZMQ_LINGER, 5000) == 0,
            "zero-linger test could not set nonzero linger");
    require(zmqb_set_option_int(socket, ZMQ_SNDTIMEO, 250) == 0,
            "zero-linger send timeout setup failed");
    require(zmqb_connect(socket, "tcp://127.0.0.1:59997") == 0,
            "zero-linger connect failed");
    const unsigned char byte = 42;
    require(zmqb_send(socket, &byte, 1, 0) == 1, "zero-linger queued send failed");

    const auto begin = Clock::now();
    require(zmqb_context_destroy(context) == 0, "zero-linger context destroy failed");
    require(Clock::now() - begin < 1500ms,
            "context destroy honored a stale nonzero linger value");
}

void expect_context_creation_allocation_failure(const ZmqTestFailurePoint point,
                                                const std::string &name) {
    zmqb_test_fail_once(point, ZmqTestException::bad_alloc);
    zmq_handle_t context = 0;
    try {
        context = zmqb_context_create(1);
    } catch (...) {
        die(name + ": exception escaped context creation");
    }
    zmqb_test_clear_failure();
    require(context == 0, name + ": context creation did not fail");
    require(zmqb_errno() == ENOMEM, name + ": context creation did not report ENOMEM");
    require_no_test_leaks(name);
}

void expect_socket_creation_allocation_failure(const zmq_handle_t context,
                                               const ZmqTestFailurePoint point,
                                               const std::string &name) {
    zmqb_test_fail_once(point, ZmqTestException::bad_alloc);
    zmq_handle_t socket = 0;
    try {
        socket = zmqb_socket_create(context, ZMQ_PAIR);
    } catch (...) {
        die(name + ": exception escaped socket creation");
    }
    zmqb_test_clear_failure();
    require(socket == 0, name + ": socket creation did not fail");
    require(zmqb_errno() == ENOMEM, name + ": socket creation did not report ENOMEM");
    require(zmqb_test_context_registry_size() == 1,
            name + ": valid context was unregistered");
    require(zmqb_test_socket_registry_size() == 0, name + ": stale socket handle");
    require(zmqb_test_live_contexts() == 1, name + ": valid native context was lost");
    require(zmqb_test_live_sockets() == 0, name + ": native socket leak");

    const zmq_handle_t probe = zmqb_socket_create(context, ZMQ_PAIR);
    require(probe != 0, name + ": context was unusable after socket failure");
    require(zmqb_socket_close(probe) == 0, name + ": probe socket close failed");
}

void test_allocation_failure_guarantees() {
    require_no_test_leaks("allocation-test initial state");

    expect_context_creation_allocation_failure(
        ZmqTestFailurePoint::context_state_allocation, "context state allocation");
    expect_context_creation_allocation_failure(
        ZmqTestFailurePoint::context_registry_insertion, "context registry insertion");

    const zmq_handle_t context = zmqb_context_create(1);
    require(context != 0, "socket allocation test context creation failed");
    expect_socket_creation_allocation_failure(
        context, ZmqTestFailurePoint::socket_state_allocation, "socket state allocation");
    expect_socket_creation_allocation_failure(
        context, ZmqTestFailurePoint::socket_registry_insertion, "socket registry insertion");
    expect_socket_creation_allocation_failure(context,
                                              ZmqTestFailurePoint::context_ownership_insertion,
                                              "context ownership insertion");
    require(zmqb_context_destroy(context) == 0, "socket allocation test cleanup failed");
    require_no_test_leaks("socket allocation cleanup");

    const zmq_handle_t destroy_context = zmqb_context_create(1);
    const zmq_handle_t destroy_socket = zmqb_socket_create(destroy_context, ZMQ_PAIR);
    require(destroy_context != 0 && destroy_socket != 0,
            "context destroy allocation setup failed");
    zmqb_test_fail_once(ZmqTestFailurePoint::context_destroy_storage,
                        ZmqTestException::bad_alloc);
    const int destroy_result = call_without_escape(
        [&]() { return zmqb_context_destroy(destroy_context); },
        "context destroy temporary allocation");
    zmqb_test_clear_failure();
    require(destroy_result == -1, "context destroy allocation did not fail");
    require(zmqb_errno() == ENOMEM, "context destroy allocation did not report ENOMEM");
    require(zmqb_test_context_registry_size() == 1 &&
                zmqb_test_socket_registry_size() == 1,
            "context destroy allocation mutated registries");
    require(zmqb_test_live_contexts() == 1 && zmqb_test_live_sockets() == 1,
            "context destroy allocation released native resources");
    require(zmqb_set_option_int(destroy_socket, ZMQ_LINGER, 0) == 0,
            "socket unusable after context destroy allocation failure");
    require(zmqb_context_destroy(destroy_context) == 0,
            "context destroy retry after allocation failure failed");
    require_no_test_leaks("context destroy allocation cleanup");

    const zmq_handle_t receive_context = zmqb_context_create(1);
    const zmq_handle_t push = zmqb_socket_create(receive_context, ZMQ_PUSH);
    const zmq_handle_t pull = zmqb_socket_create(receive_context, ZMQ_PULL);
    require(receive_context != 0 && push != 0 && pull != 0,
            "receive allocation setup failed");
    require(zmqb_bind(push, "inproc://receive-allocation-failure") == 0,
            "receive allocation bind failed");
    require(zmqb_connect(pull, "inproc://receive-allocation-failure") == 0,
            "receive allocation connect failed");
    send_text(push, "message-that-requires-a-pending-buffer");
    require(zmqb_poll(pull, ZMQ_POLLIN, 1000) == 1,
            "receive allocation message did not arrive");
    unsigned char small[4] = {};
    zmqb_test_fail_once(ZmqTestFailurePoint::receive_pending_allocation,
                        ZmqTestException::bad_alloc);
    const int receive_result = call_without_escape(
        [&]() { return zmqb_receive(pull, small, sizeof(small), 0); },
        "receive pending allocation");
    zmqb_test_clear_failure();
    require(receive_result == -1, "receive pending allocation did not fail");
    require(zmqb_errno() == ENOMEM, "receive pending allocation did not report ENOMEM");
    require(zmqb_test_live_messages() == 0, "receive pending allocation leaked zmq_msg_t");
    require(zmqb_test_context_registry_size() == 1 &&
                zmqb_test_socket_registry_size() == 2,
            "receive pending allocation corrupted registries");
    send_text(push, "after-failure");
    require(receive_text(pull) == "after-failure",
            "socket unusable after receive pending allocation failure");
    require(zmqb_context_destroy(receive_context) == 0,
            "receive allocation cleanup failed");
    require_no_test_leaks("receive allocation cleanup");
}

void test_exception_error_mapping() {
    unsigned char text[32] = {};
    zmqb_test_fail_once(ZmqTestFailurePoint::export_entry, ZmqTestException::bad_alloc);
    require(call_without_escape([&]() { return zmqb_error_text(0, text, sizeof(text)); },
                                "bad_alloc mapping") == -1,
            "bad_alloc boundary did not fail");
    zmqb_test_clear_failure();
    require(zmqb_errno() == ENOMEM, "bad_alloc boundary did not report ENOMEM");

    zmqb_test_fail_once(ZmqTestFailurePoint::export_entry, ZmqTestException::length_error);
    require(call_without_escape([&]() { return zmqb_error_text(0, text, sizeof(text)); },
                                "length_error mapping") == -1,
            "length_error boundary did not fail");
    zmqb_test_clear_failure();
    require(zmqb_errno() == EMSGSIZE, "length_error boundary did not report EMSGSIZE");
}

void test_every_export_exception_boundary() {
    require_no_test_leaks("boundary-test initial state");
    const zmq_handle_t context = zmqb_context_create(1);
    const zmq_handle_t socket = zmqb_socket_create(context, ZMQ_PAIR);
    require(context != 0 && socket != 0, "boundary-test setup failed");

    unsigned char bytes[8] = {'t', 'e', 's', 't'};
    int integers[1] = {1};
    double doubles[1] = {1.0};
    int int_value = 0;
    int64_t int64_value = 0;

    expect_handle_boundary([&]() { return zmqb_context_create(1); }, "zmqb_context_create");
    expect_int_boundary([&]() { return zmqb_context_destroy(context); },
                        "zmqb_context_destroy");
    expect_handle_boundary([&]() { return zmqb_socket_create(context, ZMQ_PAIR); },
                           "zmqb_socket_create");
    expect_int_boundary([&]() { return zmqb_socket_close(socket); }, "zmqb_socket_close");
    expect_int_boundary([&]() { return zmqb_bind(socket, "inproc://boundary-bind"); },
                        "zmqb_bind");
    expect_int_boundary([&]() { return zmqb_connect(socket, "inproc://boundary-connect"); },
                        "zmqb_connect");
    expect_int_boundary([&]() { return zmqb_send(socket, bytes, 4, ZMQ_DONTWAIT); },
                        "zmqb_send");
    expect_int_boundary(
        [&]() { return zmqb_send_int_array(socket, integers, 1, ZMQ_DONTWAIT); },
        "zmqb_send_int_array");
    expect_int_boundary(
        [&]() { return zmqb_send_double_array(socket, doubles, 1, ZMQ_DONTWAIT); },
        "zmqb_send_double_array");
    expect_int_boundary([&]() { return zmqb_receive(socket, bytes, sizeof(bytes), ZMQ_DONTWAIT); },
                        "zmqb_receive");
    expect_int_boundary([&]() { return zmqb_set_option_int(socket, ZMQ_LINGER, 0); },
                        "zmqb_set_option_int");
    expect_int_boundary([&]() { return zmqb_set_option_int64(socket, ZMQ_AFFINITY, 0); },
                        "zmqb_set_option_int64");
    expect_int_boundary(
        [&]() { return zmqb_set_option_bytes(socket, ZMQ_ROUTING_ID, bytes, 4); },
        "zmqb_set_option_bytes");
    expect_int_boundary([&]() { return zmqb_get_option_int(socket, ZMQ_LINGER, &int_value); },
                        "zmqb_get_option_int");
    expect_int_boundary(
        [&]() { return zmqb_get_option_int64(socket, ZMQ_AFFINITY, &int64_value); },
        "zmqb_get_option_int64");
    expect_int_boundary([&]() { return zmqb_poll(socket, ZMQ_POLLIN, 0); }, "zmqb_poll");
    expect_int_boundary([&]() { return zmqb_errno(); }, "zmqb_errno");
    expect_int_boundary([&]() { return zmqb_is_would_block(EAGAIN); },
                        "zmqb_is_would_block");
    expect_int_boundary([&]() { return zmqb_error_text(0, bytes, sizeof(bytes)); },
                        "zmqb_error_text");
    expect_int_boundary([&]() { return zmqb_version_major(); }, "zmqb_version_major");
    expect_int_boundary([&]() { return zmqb_version_minor(); }, "zmqb_version_minor");
    expect_int_boundary([&]() { return zmqb_version_patch(); }, "zmqb_version_patch");

    require(zmqb_socket_close(socket) == 0, "boundary-test socket cleanup failed");
    require(zmqb_context_destroy(context) == 0, "boundary-test context cleanup failed");
    require_no_test_leaks("boundary-test cleanup");
}

void test_repeated_shutdown_and_leak_cleanup() {
    for (int i = 0; i < 25; ++i) {
        const zmq_handle_t context = zmqb_context_create(1);
        require(context != 0, "repeated context creation failed");
        const zmq_handle_t socket = zmqb_socket_create(context, ZMQ_PUSH);
        require(socket != 0, "repeated socket creation failed");
        require(zmqb_context_destroy(context) == 0, "context-owned socket cleanup failed");
        require(zmqb_socket_close(socket) == -1,
                "context-owned socket remained registered after cleanup");
    }
}

} // namespace

int main() {
    require(sizeof(zmq_handle_t) == sizeof(void *), "handle type is not pointer-sized");
    require(zmqb_version_major() == 4 && zmqb_version_minor() == 3 &&
                zmqb_version_patch() == 5,
            "unexpected libzmq version");
    test_pub_sub();
    test_nonblocking_and_empty_receive();
    test_retained_message_poll();
    test_typed_socket_options();
    test_independent_contexts();
    test_independent_sockets_one_context();
    test_poll_close_race();
    test_send_close_race();
    test_receive_context_destroy_race();
    test_explicit_close_context_destroy_race();
    test_context_destroy_forces_zero_linger();
    test_repeated_shutdown_and_leak_cleanup();
    test_allocation_failure_guarantees();
    test_exception_error_mapping();
    test_every_export_exception_boundary();
    std::cout << "PASS: native wrapper smoke tests\n";
    return EXIT_SUCCESS;
}
