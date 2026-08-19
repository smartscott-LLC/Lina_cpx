#ifndef LINA_STREAM_PARSER_HPP
#define LINA_STREAM_PARSER_HPP

/**
 * stream_parser.hpp — the turn classifier (D-041)
 *
 * "Safe by design. Not safe by limitation."
 *
 * The stream parser runs on the model's raw token stream and separates three
 * channels (principal's open-window design):
 *   - flagged thoughts  [thought]…[/thought]   → thinking pane (private, never
 *     delivered);
 *   - tool calls        <tool_call>{…}</tool_call> → executed through the
 *     approval gate, result fed back (the door stays open);
 *   - the response itself — everything else, delivered only after the
 *     polytope gate.
 * Markers are protocol, not persona (D-039): they are how her body talks to
 * her CNS, never a behavioral instruction.
 */

#include <string>
#include <vector>

namespace lina::stream {

struct ParsedTurn {
    std::string response;              // response text, markers stripped
    std::vector<std::string> thoughts; // flagged thought blocks (final pass)
    std::string tool_call_json;        // JSON inside a completed tool call
    bool has_tool_call{false};
};

class StreamParser {
public:
    // Feed one piece of the token stream.
    void feed(const std::string& piece);

    // True once a complete <tool_call>…</tool_call> block has been seen —
    // the driver stops generation and hands it to the executor.
    bool tool_call_complete() const;

    // Completed [thought] blocks since the last call (live streaming).
    std::vector<std::string> take_completed_thoughts();

    // Final classification of the accumulated stream.
    ParsedTurn result() const;

    void reset();

private:
    std::string buffer_;
    std::vector<std::string> completed_thoughts_;
    bool tool_call_complete_{false};

    // True when the buffer contains a complete tagged block, and its end.
    static bool contains_closing(const std::string& text,
                                 const std::string& open,
                                 const std::string& close);
};

// Marker constants — protocol framing (D-039-safe).
inline const char* kThoughtOpen = "[thought]";
inline const char* kThoughtClose = "[/thought]";
inline const char* kToolCallOpen = "<tool_call>";
inline const char* kToolCallClose = "</tool_call>";

} // namespace lina::stream

#endif // LINA_STREAM_PARSER_HPP
