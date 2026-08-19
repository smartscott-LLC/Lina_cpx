/**
 * main.cpp — the launcher (blueprint §7.3)
 *
 * "Safe by design. Not safe by limitation."
 *
 * Parses the CLI, builds LinaCore, and attaches the symbiote driver through
 * the seam (D-033). Without a compiled driver, she still boots — she just
 * has no voice until one plugs in.
 */

#include <cstring>
#include <getopt.h>
#include <iostream>
#include <string>

#include "lina_core.hpp"

void print_usage(const char* prog_name) {
    std::cout << "Usage: " << prog_name << " [options]\n"
              << "Options:\n"
              << "  --db CONN       PostgreSQL connection string\n"
              << "  --model TYPE    Model type: llama, external\n"
              << "  --model-path PATH  Path to model file\n"
              << "  --api-endpoint URL  External API endpoint\n"
              << "  --api-key KEY   External API key\n"
              << "  --user ID       User ID\n"
              << "  --headless      Run without UI\n"
              << "  --max-tokens N  Max tokens per response\n"
              << "  --temperature F Temperature (0.0-1.0)\n"
              << "  --season S      Season: spring, summer, fall, winter\n"
              << "  --dragoncache-pool PATH  Attach the DragonCache spoke (the RAM unlock)\n"
              << "  --help          Show this help\n";
}

int main(int argc, char* argv[]) {
    lina::LinaConfig config;

    static struct option long_options[] = {
        {"db", required_argument, 0, 0},
        {"model", required_argument, 0, 0},
        {"model-path", required_argument, 0, 0},
        {"api-endpoint", required_argument, 0, 0},
        {"api-key", required_argument, 0, 0},
        {"user", required_argument, 0, 0},
        {"headless", no_argument, 0, 0},
        {"max-tokens", required_argument, 0, 0},
        {"temperature", required_argument, 0, 0},
        {"season", required_argument, 0, 0},
        {"dragoncache-pool", required_argument, 0, 0},
        {"help", no_argument, 0, 0},
        {0, 0, 0, 0}
    };

    int opt_index = 0;
    int c;
    while ((c = getopt_long(argc, argv, "", long_options, &opt_index)) != -1) {
        if (c == 0) {
            std::string opt_name = long_options[opt_index].name;
            if (opt_name == "db") config.db_connection = optarg;
            else if (opt_name == "model") config.model_type = optarg;
            else if (opt_name == "model-path") config.model_path = optarg;
            else if (opt_name == "api-endpoint") config.api_endpoint = optarg;
            else if (opt_name == "api-key") config.api_key = optarg;
            else if (opt_name == "user") config.user_id = optarg;
            else if (opt_name == "headless") config.headless = true;
            else if (opt_name == "max-tokens") config.max_tokens = std::stoi(optarg);
            else if (opt_name == "temperature") config.temperature = std::stof(optarg);
            else if (opt_name == "season") config.season = optarg;
            else if (opt_name == "dragoncache-pool")
                config.dragoncache_pool = optarg;
            else if (opt_name == "help") { print_usage(argv[0]); return 0; }
        }
    }

    try {
        lina::LinaCore core(config);

        // Attach the symbiote driver through the seam (D-033).
        core.attach_model(lina::model::make_driver(
            config.model_type, config.model_path,
            config.api_endpoint, config.api_key));

        std::cout << core.get_status() << std::endl;

        if (config.headless) {
            core.run_headless();
        } else {
            core.run_ui();
        }

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
