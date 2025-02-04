#define CROW_MAIN
#include "common.hpp"
#include "git.h"
#include "lint_track_one/lint.hpp"
#include "lint_track_two/lint.hpp"
#include "thread_safe_queue/tsq.hpp"

#include <argparse/argparse.hpp>
#include <crow/app.h>

#include <algorithm>
#include <iostream>
#include <string>
#include <thread>
#include <tuple>

static std::tuple<uint8_t>
process_arguments(int argc, const char** argv)
{
    argparse::ArgumentParser program(
        "NUTC Linter", NUTC_VERSION, argparse::default_arguments::help
    );

    program.add_argument("-V", "--version")
        .help("prints version information and exits")
        .action([&](const auto& /* unused */) {
            fmt::println("NUTC Linter v{}", NUTC_VERSION);
            exit(0); // NOLINT(concurrency-*)
        })
        .default_value(false)
        .implicit_value(true)
        .nargs(0);

    uint8_t verbosity = 0;
    program.add_argument("-v", "--verbose")
        .help("increase output verbosity")
        .action([&](const auto& /* unused */) { ++verbosity; })
        .append()
        .default_value(false)
        .implicit_value(true)
        .nargs(0);

    try {
        program.parse_args(argc, argv);
    } catch (const std::runtime_error& err) {
        std::cerr << err.what() << std::endl;
        std::cerr << program;
        exit(1); // NOLINT(concurrency-*)
    }

    return std::make_tuple(verbosity);
}

static void
log_build_info()
{
    log_i(main, "NUTC Linter: Linter for NUTC user-submitted algorithms");

    // Git info
    log_i(main, "Built from {} on {}", git_Describe(), git_Branch());
    log_d(main, "Commit: \"{}\" at {}", git_CommitSubject(), git_CommitDate());
    log_d(main, "Author: {} <{}>", git_AuthorName(), git_AuthorEmail());

    if (git_AnyUncommittedChanges())
        log_w(main, "Built from dirty commit!");
}

int
main(int argc, const char** argv)
{
    // Parse args
    auto [verbosity] = process_arguments(argc, argv);

    // Start logging and print build info
    nutc::logging::init(verbosity);
    log_build_info();
    log_i(main, "Starting NUTC Linter");

    nutc::tsq::ThreadSafeQueue<std::tuple<std::string, std::string, int>> queue;

    std::thread server_thread([&queue]() {
        crow::SimpleApp app;
        CROW_ROUTE(app, "/")
        ([&](const crow::request& req) {
            log_i(main, "Registered");

            if (!req.url_params.get("uid")) {
                log_e(main, "No uid provided");
                return crow::response(400);
            };
            if (!req.url_params.get("algo_id")) {
                log_e(main, "No algo_id provided");
                return crow::response(400);
            }

            if (!req.url_params.get("task")) {
                log_e(main, "No task provided");
                return crow::response(400);
            }
            int task;
            try {
                task = std::stoi(req.url_params.get("task"));
            } catch (std::exception& e) {
                log_e(main, "task is malformed, got {}", req.url_params.get("task"));
                return crow::response(400);
            }

            if (task != 1 && task != 2) {
                log_e(main, "task must be either 1 or 2, got {}", task);
                return crow::response(400);
            }

            std::string uid = req.url_params.get("uid");
            std::string algo_id = req.url_params.get("algo_id");
            queue.push(std::make_tuple(uid, algo_id, task));

            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            return crow::response();
        });
        app.port(8080).run();
    });

    pybind11::initialize_interpreter();

    while (true) {
        std::optional<std::tuple<std::string, std::string, int>> submission = queue.pop();
        if (!submission) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        pybind11::exec("locals().clear()");

        std::string uid = std::get<0>(submission.value());
        std::string algo_id = std::get<1>(submission.value());
        int task = std::get<2>(submission.value());
        log_i(main, "Linting algo_id: {} for user: {} on task {}", algo_id, uid, task);
        std::string response;
        if (task == 1) {
            log_i(main, "linting for track one");
            response = nutc::lint_track_one::lint(uid, algo_id);
        } else {
            log_i(main, "linting for track two");
            response = nutc::lint_track_two::lint(uid, algo_id);
        }

        nutc::client::set_lint_success(uid, algo_id, response + "\n");
    }
    pybind11::finalize_interpreter();

    server_thread.join();

    return 0;
}
