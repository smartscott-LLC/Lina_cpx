#ifndef LINA_APPROVAL_GATE_HPP
#define LINA_APPROVAL_GATE_HPP

/**
 * approval_gate.hpp — the human-in-the-loop gate (D-038 / D-040)
 *
 * "Safe by design. Not safe by limitation."
 *
 * The approval engine is the ONLY gate on LiNa's actions (D-040): every tool
 * execution passes through request_approval() before it touches the machine.
 * The polytope gates her responses; the approval engine gates her actions;
 * nothing else stands between her and the system.
 */

#include <functional>
#include <string>

namespace lina {

struct ApprovalRequest {
    std::string action_id;
    std::string tool_name;
    std::string description;
    int64_t timeout_ms{30000};
};

enum class ApprovalDecision { Approved, Denied, TimedOut };

using ApprovalHandler =
    std::function<ApprovalDecision(const ApprovalRequest&)>;

} // namespace lina

#endif // LINA_APPROVAL_GATE_HPP
