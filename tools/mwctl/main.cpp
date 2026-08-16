#include <mw/config.hpp>
#include <mw/result.hpp>

#include "detail/registry_client.hpp"

#include <iostream>
#include <string>

namespace {

void printUsage(std::ostream& output) {
    output << "usage: mwctl [--registry PATH] node list\n"
              "       mwctl [--registry PATH] topic list\n"
              "       mwctl [--registry PATH] topic info TOPIC\n"
              "       mwctl [--registry PATH] stats\n";
}

int run(int argc, char** argv) {
    mw::RegistryConfig registry_config;
    int argument = 1;
    if (argument < argc && std::string{argv[argument]} == "--registry") {
        if (argument + 1 >= argc) {
            printUsage(std::cerr);
            return 2;
        }
        registry_config.socket_path = argv[argument + 1];
        argument += 2;
    }

    if (argument >= argc) {
        printUsage(std::cerr);
        return 2;
    }

    if (std::string{argv[argument]} == "stats" && argument + 1 == argc) {
        mw::detail::RegistryClient client{registry_config};
        const auto stats = client.queryStats();
        std::cout << "nodes: " << stats.node_count << '\n'
                  << "topics: " << stats.topic_count << '\n'
                  << "publishers: " << stats.publisher_count << '\n'
                  << "subscribers: " << stats.subscriber_count << '\n'
                  << "endpoints: " << stats.publisher_count + stats.subscriber_count << '\n'
                  << "heartbeats_received: " << stats.heartbeat_received << '\n'
                  << "suspected_transitions: " << stats.suspected_count << '\n'
                  << "dead_nodes: " << stats.dead_node_count << '\n';
        return 0;
    }

    if (argument + 1 >= argc) {
        printUsage(std::cerr);
        return 2;
    }

    const std::string resource = argv[argument++];
    const std::string command = argv[argument++];
    mw::detail::RegistryClient client{registry_config};

    if (resource == "node" && command == "list" && argument == argc) {
        const auto nodes = client.listNodes();
        std::cout << "NODE_ID\tNODE_NAME\tSTATE\n";
        for (const auto& node : nodes) {
            std::cout << node.node_id << '\t' << node.node_name << '\t'
                      << mw::livenessStateName(node.liveness) << '\n';
        }
        return 0;
    }

    if (resource == "topic" && command == "list" && argument == argc) {
        const auto topics = client.listTopics();
        std::cout << "TOPIC_ID\tTOPIC_NAME\n";
        for (const auto& topic : topics) {
            std::cout << topic.topic_id << '\t' << topic.topic_name << '\n';
        }
        return 0;
    }

    if (resource == "topic" && command == "info" && argument + 1 == argc) {
        const auto topic = client.queryTopic(argv[argument]);
        std::cout << "topic_id: " << topic.topic_id << '\n'
                  << "topic_name: " << topic.topic_name << '\n'
                  << "type_name: " << topic.type_name << '\n'
                  << "type_hash: " << topic.type_hash << '\n'
                  << "transport: " << mw::transportTypeName(topic.transport) << '\n'
                  << "max_message_size: " << topic.max_message_size << '\n'
                  << "publishers: " << topic.publisher_count << '\n'
                  << "subscribers: " << topic.subscriber_count << '\n';
        if (!topic.pool.shm_name.empty()) {
            std::cout << "pool_name: " << topic.pool.shm_name << '\n'
                      << "pool_id: " << topic.pool.pool_id << '\n'
                      << "pool_segment_size: " << topic.pool.segment_size << '\n'
                      << "pool_layout_version: " << topic.pool.layout_version << '\n';
        }
        return 0;
    }

    printUsage(std::cerr);
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const mw::MiddlewareError& error) {
        std::cerr << "mwctl: " << error.what() << " (" << mw::errorMessage(error.code()) << ")\n";
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "mwctl: " << error.what() << '\n';
        return 1;
    }
}
