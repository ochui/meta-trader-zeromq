#include "zmq_bind.h"

#include <zmq.h>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

int fail(const std::string &message) {
    std::cerr << "FAIL: " << message << " (zmq error " << zmqb_errno() << ")\n";
    return EXIT_FAILURE;
}

int run_subscriber(const std::string &endpoint, const std::string &expected) {
    const zmq_handle_t context = zmqb_context_create(1);
    const zmq_handle_t subscriber = zmqb_socket_create(context, ZMQ_SUB);
    const unsigned char empty_topic = 0;
    if (context == 0 || subscriber == 0 ||
        zmqb_set_option_bytes(subscriber, ZMQ_SUBSCRIBE, &empty_topic, 0) != 0 ||
        zmqb_bind(subscriber, endpoint.c_str()) != 0) {
        return fail("subscriber setup");
    }

    std::cout << "READY\n" << std::flush;
    if (zmqb_poll(subscriber, ZMQ_POLLIN, 5000) != 1) {
        return fail("subscriber timeout");
    }

    std::vector<unsigned char> bytes(16);
    int received = zmqb_receive(subscriber, bytes.data(), static_cast<int>(bytes.size()), 0);
    if (received > static_cast<int>(bytes.size())) {
        bytes.resize(static_cast<size_t>(received));
        received = zmqb_receive(subscriber, bytes.data(), static_cast<int>(bytes.size()), 0);
    }
    const std::string actual(reinterpret_cast<const char *>(bytes.data()),
                             received < 0 ? 0U : static_cast<size_t>(received));
    const bool matches = received >= 0 && actual == expected;
    (void) zmqb_socket_close(subscriber);
    (void) zmqb_context_destroy(context);
    if (!matches) {
        return fail("payload mismatch");
    }
    std::cout << "PASS: " << actual << '\n';
    return EXIT_SUCCESS;
}

int run_publisher(const std::string &endpoint, const std::string &message) {
    const zmq_handle_t context = zmqb_context_create(1);
    const zmq_handle_t publisher = zmqb_socket_create(context, ZMQ_PUB);
    if (context == 0 || publisher == 0 || zmqb_connect(publisher, endpoint.c_str()) != 0) {
        return fail("publisher setup");
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    const auto *bytes = reinterpret_cast<const unsigned char *>(message.data());
    for (int i = 0; i < 3; ++i) {
        if (zmqb_send(publisher, bytes, static_cast<int>(message.size()), 0) < 0) {
            return fail("publisher send");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    (void) zmqb_socket_close(publisher);
    (void) zmqb_context_destroy(context);
    return EXIT_SUCCESS;
}

} // namespace

int main(const int argc, char **argv) {
    if (argc != 4) {
        std::cerr << "usage: zmq_cross_arch_peer subscriber|publisher endpoint message\n";
        return EXIT_FAILURE;
    }
    const std::string mode = argv[1];
    if (mode == "subscriber") {
        return run_subscriber(argv[2], argv[3]);
    }
    if (mode == "publisher") {
        return run_publisher(argv[2], argv[3]);
    }
    return fail("unknown mode");
}
