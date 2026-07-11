#include "zmq_bind.h"

#include <zmq.h>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

[[noreturn]] void die(const std::string &message) {
    std::cerr << "FAIL: " << message << " (zmq error " << zmqb_errno() << ")\n";
    std::exit(EXIT_FAILURE);
}

void require(const bool condition, const std::string &message) {
    if (!condition) {
        die(message);
    }
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
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    const std::string long_message =
        std::string("topic|key=value|UTF-8: Привет мир | こんにちは | ") + std::string(200000, 'x');
    send_text(publisher, long_message);
    require(zmqb_poll(subscriber, ZMQ_POLLIN, 1000) == 1, "long PUB/SUB message timed out");
    require(receive_text(subscriber) == long_message, "long UTF-8 message changed in transit");

    send_text(publisher, "");
    require(zmqb_poll(subscriber, ZMQ_POLLIN, 1000) == 1, "empty message timed out");
    unsigned char one_byte = 0;
    require(zmqb_receive(subscriber, &one_byte, 1, 0) == 0, "empty message was not preserved");

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

void test_push_pull_and_nonblocking() {
    const zmq_handle_t context = zmqb_context_create(1);
    const zmq_handle_t push = zmqb_socket_create(context, ZMQ_PUSH);
    const zmq_handle_t pull = zmqb_socket_create(context, ZMQ_PULL);
    require(context != 0 && push != 0 && pull != 0, "PUSH/PULL setup failed");
    require(zmqb_bind(push, "inproc://native-push-pull") == 0, "PUSH bind failed");
    require(zmqb_connect(pull, "inproc://native-push-pull") == 0, "PULL connect failed");

    unsigned char buffer[8] = {};
    require(zmqb_receive(pull, buffer, sizeof(buffer), ZMQ_DONTWAIT) == -1,
            "non-blocking receive should report no message");
    require(zmqb_errno() == EAGAIN, "non-blocking receive did not report EAGAIN");

    send_text(push, "a|b=c");
    require(receive_text(pull) == "a|b=c", "PUSH/PULL payload changed");
    require(zmqb_socket_close(pull) == 0, "PULL close failed");
    require(zmqb_socket_close(push) == 0, "PUSH close failed");
    require(zmqb_context_destroy(context) == 0, "PUSH/PULL context destroy failed");
}

void test_repeated_shutdown_and_leak_cleanup() {
    for (int i = 0; i < 25; ++i) {
        const zmq_handle_t context = zmqb_context_create(1);
        require(context != 0, "repeated context creation failed");
        const zmq_handle_t socket = zmqb_socket_create(context, ZMQ_PUSH);
        require(socket != 0, "repeated socket creation failed");
        // Deliberately leave the socket open. Context destruction must close it
        // with zero linger instead of hanging the host process.
        require(zmqb_context_destroy(context) == 0, "context-owned socket cleanup failed");
    }
}

} // namespace

int main() {
    require(sizeof(zmq_handle_t) == sizeof(void *), "handle type is not pointer-sized");
    require(zmqb_version_major() == 4 && zmqb_version_minor() == 3 &&
                zmqb_version_patch() == 5,
            "unexpected libzmq version");
    test_pub_sub();
    test_push_pull_and_nonblocking();
    test_repeated_shutdown_and_leak_cleanup();
    std::cout << "PASS: native wrapper smoke tests\n";
    return EXIT_SUCCESS;
}
