/**
 * stream_parser_tests.cpp — the turn classifier (D-041)
 *
 * Separates the three stream channels: flagged thoughts (private, streamed
 * live), tool calls (executed with the door open), and the response itself.
 */

#include "stream_parser.hpp"

#include <iostream>
#include <string>
#include <vector>

using namespace lina::stream;

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            ++g_failures;                                                    \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__              \
                      << "  " #cond << "\n";                                 \
        }                                                                    \
    } while (0)

static void test_plain_text() {
    StreamParser parser;
    parser.feed("Hello there. ");
    parser.feed("This is a plain response.");
    CHECK(!parser.tool_call_complete());
    const auto parsed = parser.result();
    CHECK(!parsed.has_tool_call);
    CHECK(parsed.thoughts.empty());
    CHECK(parsed.response == "Hello there. This is a plain response.");
}

static void test_thought_live_and_stripped() {
    StreamParser parser;
    parser.feed("[thought]");
    parser.feed("should I be direct? ");
    parser.feed("[/thought]");
    auto thoughts = parser.take_completed_thoughts();
    CHECK(thoughts.size() == 1);
    CHECK(thoughts[0] == "should I be direct? ");
    // The response never contains the thought.
    const auto parsed = parser.result();
    CHECK(parsed.thoughts.empty());
    CHECK(parsed.response.find("should I be direct") == std::string::npos);
}

static void test_tool_call_detection() {
    StreamParser parser;
    const std::string call =
        "<tool_call>{\"name\":\"terminal.run\","
        "\"arguments\":{\"command\":\"echo hi\"}}</tool_call>";
    parser.feed(call.substr(0, 20));
    CHECK(!parser.tool_call_complete());
    parser.feed(call.substr(20));
    CHECK(parser.tool_call_complete());

    const auto parsed = parser.result();
    CHECK(parsed.has_tool_call);
    CHECK(parsed.tool_call_json.find("\"terminal.run\"") != std::string::npos);
    CHECK(parsed.response.find("tool_call") == std::string::npos);
}

static void test_mixed_stream() {
    StreamParser parser;
    parser.feed("[thought]careful[/thought]");
    parser.feed("I will check first. ");
    parser.feed("<tool_call>{\"name\":\"file.read\","
                "\"arguments\":{\"path\":\"a.txt\"}}</tool_call>");
    parser.feed("Then I will report.");
    CHECK(parser.tool_call_complete());

    const auto parsed = parser.result();
    CHECK(parsed.has_tool_call);
    CHECK(parsed.tool_call_json.find("\"file.read\"") != std::string::npos);
    CHECK(parsed.response.find("I will check first") != std::string::npos);
    CHECK(parsed.response.find("Then I will report") != std::string::npos);
    CHECK(parsed.response.find("tool_call") == std::string::npos);
    CHECK(parsed.response.find("careful") == std::string::npos);
}

static void test_empty_thought_and_reset() {
    StreamParser parser;
    parser.feed("[thought][/thought]remaining");
    const auto parsed = parser.result();
    CHECK(parsed.response == "remaining");

    parser.reset();
    parser.feed("fresh");
    const auto again = parser.result();
    CHECK(!again.has_tool_call);
    CHECK(again.response == "fresh");
    CHECK(parser.take_completed_thoughts().empty());
}

int main() {
    test_plain_text();
    test_thought_live_and_stripped();
    test_tool_call_detection();
    test_mixed_stream();
    test_empty_thought_and_reset();

    std::cout << "stream_parser_tests: " << g_checks << " checks, "
              << g_failures << " failures\n";
    return g_failures == 0 ? 0 : 1;
}
