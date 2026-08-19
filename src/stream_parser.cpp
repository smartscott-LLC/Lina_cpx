/**
 * stream_parser.cpp — the turn classifier (Implementation, D-041)
 */

#include "stream_parser.hpp"

#include <cstddef>

namespace lina::stream {

namespace {

constexpr std::size_t kThoughtOpenLen = 9;   // strlen("[thought]")
constexpr std::size_t kThoughtCloseLen = 10;  // strlen("[/thought]")
constexpr std::size_t kToolCallOpenLen = 11;  // strlen("<tool_call>")
constexpr std::size_t kToolCallCloseLen = 12; // strlen("</tool_call>")

} // namespace

bool StreamParser::contains_closing(const std::string& text,
                                    const std::string& open,
                                    const std::string& close) {
    const auto open_pos = text.find(open);
    if (open_pos == std::string::npos) return false;
    return text.find(close, open_pos + open.size()) != std::string::npos;
}

void StreamParser::feed(const std::string& piece) {
    buffer_ += piece;
    if (contains_closing(buffer_, kToolCallOpen, kToolCallClose)) {
        tool_call_complete_ = true;
    }
    // Sweep completed thought blocks for live streaming.
    for (;;) {
        const auto open_pos = buffer_.find(kThoughtOpen);
        if (open_pos == std::string::npos) break;
        const auto close_pos =
            buffer_.find(kThoughtClose, open_pos + kThoughtOpenLen);
        if (close_pos == std::string::npos) break;
        completed_thoughts_.push_back(buffer_.substr(
            open_pos + kThoughtOpenLen,
            close_pos - open_pos - kThoughtOpenLen));
        buffer_.erase(open_pos, close_pos + kThoughtCloseLen - open_pos);
    }
}

bool StreamParser::tool_call_complete() const {
    return tool_call_complete_;
}

std::vector<std::string> StreamParser::take_completed_thoughts() {
    std::vector<std::string> taken;
    taken.swap(completed_thoughts_);
    return taken;
}

ParsedTurn StreamParser::result() const {
    ParsedTurn parsed;
    std::string text = buffer_;

    if (contains_closing(text, kToolCallOpen, kToolCallClose)) {
        const auto open_pos = text.find(kToolCallOpen);
        const auto close_pos =
            text.find(kToolCallClose, open_pos + kToolCallOpenLen);
        parsed.tool_call_json = text.substr(
            open_pos + kToolCallOpenLen,
            close_pos - open_pos - kToolCallOpenLen);
        parsed.has_tool_call = true;
        text.erase(open_pos, close_pos + kToolCallCloseLen - open_pos);
    }

    // Any thoughts still open in the final pass are collected too.
    for (;;) {
        const auto open_pos = text.find(kThoughtOpen);
        if (open_pos == std::string::npos) break;
        const auto close_pos =
            text.find(kThoughtClose, open_pos + kThoughtOpenLen);
        if (close_pos == std::string::npos) break;
        parsed.thoughts.push_back(text.substr(
            open_pos + kThoughtOpenLen,
            close_pos - open_pos - kThoughtOpenLen));
        text.erase(open_pos, close_pos + kThoughtCloseLen - open_pos);
    }

    parsed.response = text;
    return parsed;
}

void StreamParser::reset() {
    buffer_.clear();
    completed_thoughts_.clear();
    tool_call_complete_ = false;
}

} // namespace lina::stream
