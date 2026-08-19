#ifndef LINA_DRAGONCACHE_HPP
#define LINA_DRAGONCACHE_HPP

/**
 * dragoncache.hpp — her spoke on the DragonCache (the RAM unlock)
 *
 * "Safe by design. Not safe by limitation."
 *
 * The unified header (dragon_map.h) + ring contract (dragon_ring.h) give every
 * spoke zero-latency, constant state awareness: one mmap, one atomic heartbeat,
 * lock-free TX/RX rings. LinaCore attaches as a spoke when configured — it
 * registers its health bits, ticks the clock, and mirrors telemetry into the
 * RX ring (technical bus — Invariant 6: never the cognitive bus).
 */

#include <cstdint>
#include <mutex>
#include <string>

#include "dragon_map.h"
#include "dragon_ring.h"

namespace lina::dragoncache {

class Hub {
public:
    Hub() = default;
    ~Hub() { detach(); }

    Hub(const Hub&) = delete;
    Hub& operator=(const Hub&) = delete;

    // mmaps the pool and validates the magic. Returns false when the pool is
    // absent or foreign (the carve must run first).
    bool attach(const std::string& pool_path);

    void detach();

    bool attached() const { return base_ != nullptr; }

    void set_status(uint32_t status);
    void spoke_ready(uint32_t bit);
    void spoke_offline(uint32_t bit);

    uint64_t clock() const;
    uint32_t status() const;
    uint32_t spokes() const;

    // Ring frames (thread-safe). tx=true → TX ring (LINA → spokes); tx=false
    // → RX ring (spokes → LINA). push returns false when the ring is full.
    bool push_frame(bool tx, uint8_t type, const void* payload,
                    uint32_t payload_len);
    uint32_t pop_frame(bool tx, uint8_t* type_out, void* payload_out,
                       uint32_t payload_cap, uint32_t* payload_len_out);

private:
    int fd_{-1};
    void* base_{nullptr};
    RingBuffer* tx_{nullptr};
    RingBuffer* rx_{nullptr};
    mutable std::mutex mutex_;
};

} // namespace lina::dragoncache

#endif // LINA_DRAGONCACHE_HPP
