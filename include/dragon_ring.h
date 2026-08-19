#pragma once
#include <atomic>
#include <cstdint>
#include <cstring>

// ═══════════════════════════════════════════════════════════════════════════
//  dragon_ring.h — The DragonCache TX/RX ring contract.
//
//  Two lock-free SPSC rings live in Chamber A (256 MiB each):
//    - TX ring:  LINA pushes commands/payloads; spokes pop.
//    - RX ring:  spokes push results/streams; LINA pops.
//
//  Every spoke includes this header. Every spoke mmaps the same physical
//  frames at /mnt/huge/lina_pool and casts the ring offsets from
//  dragon_map.h to RingBuffer*.
//
//  Discipline: one producer, one consumer, monotonic head/tail counters,
//  wrap-around payload, u32-LE length-prefixed frames. No locks, no
//  kernel round-trips, no servers.
//
//  Python mirror: backend/lina/dragon_ring.py
// ═══════════════════════════════════════════════════════════════════════════

// ── Ring geometry ─────────────────────────────────────────────────────────
//  Each ring region is 256 MiB = 268,435,456 bytes.
//  The first 128 bytes hold head + tail (each on their own cache line).
//  The remaining 256 MiB - 128 B is the payload ring.
constexpr uint64_t RING_HEAD_OFFSET  = 0ULL;        // head at ring base + 0
constexpr uint64_t RING_TAIL_OFFSET  = 64ULL;       // tail at ring base + 64
constexpr uint64_t RING_DATA_OFFSET  = 128ULL;      // payload at ring base + 128
constexpr uint64_t RING_PAYLOAD_SIZE = 256ULL * 1024 * 1024 - RING_DATA_OFFSET;

constexpr uint32_t RING_LEN_PREFIX   = 4;            // u32 LE frame length prefix
constexpr uint32_t RING_FRAME_MAX    = 64ULL * 1024 * 1024;  // max single frame payload

// ── Frame format ──────────────────────────────────────────────────────────
//  [0..4)   u32 LE  total frame length (type byte + payload, excluding prefix)
//  [4..5)   u8      message type
//  [5..5+N) bytes   payload
// ───────────────────────────────────────────────────────────────────────────

enum DragonMsgType : uint8_t {
    MSG_COMMAND    = 0x01,   // LINA → spoke: do this
    MSG_RESPONSE   = 0x02,   // spoke → LINA: result
    MSG_STREAM     = 0x03,   // streaming chunk (tokens, telemetry)
    MSG_STREAM_END = 0x04,   // end of stream
    MSG_CONTROL    = 0x05,   // control / status / heartbeat
    MSG_DATA       = 0x06,   // raw data payload (file, upload)
    MSG_EVENT      = 0x07,   // telemetry event
    MSG_ERROR      = 0x08,   // error
};

// ── RingBuffer struct ─────────────────────────────────────────────────────
//  Exactly 256 MiB. head and tail are on separate cache lines to avoid
//  false sharing between the producer and consumer.
struct alignas(64) RingBuffer {
    // +0:   producer write position (monotonic, wraps in payload space)
    std::atomic<uint64_t> head;
    uint8_t _pad1[56];             // +8  → fills cache line to 64

    // +64:  consumer read position (monotonic, wraps in payload space)
    std::atomic<uint64_t> tail;
    uint8_t _pad2[56];             // +72 → fills cache line to 128

    // +128: wrap-around payload ring
    uint8_t payload[RING_PAYLOAD_SIZE];
};
static_assert(sizeof(RingBuffer) == 256ULL * 1024 * 1024,
              "RingBuffer must be exactly 256 MiB");

// ═══════════════════════════════════════════════════════════════════════════
//  Wrap-aware byte helpers
//  All positions are monotonically-increasing byte indices. The ring offset
//  is computed as (pos % RING_PAYLOAD_SIZE). Reads/writes that cross the
//  ring boundary are split into two operations.
// ═══════════════════════════════════════════════════════════════════════════

/// Read a u32 LE from the ring at the given monotonic position.
inline uint32_t ring_read_u32(const RingBuffer* rb, uint64_t pos) noexcept {
    uint64_t off = pos % RING_PAYLOAD_SIZE;
    uint8_t b[4];
    for (int i = 0; i < 4; ++i)
        b[i] = rb->payload[(off + i) % RING_PAYLOAD_SIZE];
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8)
         | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

/// Write N bytes to the ring at the given monotonic position (wrap-aware).
inline void ring_write_bytes(RingBuffer* rb, uint64_t pos,
                             const void* src, uint32_t n) noexcept {
    uint64_t off = pos % RING_PAYLOAD_SIZE;
    const uint8_t* s = static_cast<const uint8_t*>(src);
    for (uint32_t i = 0; i < n; ++i)
        rb->payload[(off + i) % RING_PAYLOAD_SIZE] = s[i];
}

/// Read N bytes from the ring at the given monotonic position (wrap-aware).
inline void ring_read_bytes(const RingBuffer* rb, uint64_t pos,
                            void* dst, uint32_t n) noexcept {
    uint64_t off = pos % RING_PAYLOAD_SIZE;
    uint8_t* d = static_cast<uint8_t*>(dst);
    for (uint32_t i = 0; i < n; ++i)
        d[i] = rb->payload[(off + i) % RING_PAYLOAD_SIZE];
}

// ═══════════════════════════════════════════════════════════════════════════
//  Push — producer side
//  Writes one framed message (length prefix + type + payload) to the ring
//  and advances head. Returns true on success, false when the ring is full.
// ═══════════════════════════════════════════════════════════════════════════
inline bool ring_push(RingBuffer* rb, uint8_t type,
                      const void* payload, uint32_t payload_len) noexcept {
    if (payload_len > RING_FRAME_MAX) return false;

    const uint32_t total = RING_LEN_PREFIX + 1 + payload_len;  // len + type + data
    const uint64_t head  = rb->head.load(std::memory_order_acquire);
    const uint64_t tail  = rb->tail.load(std::memory_order_acquire);
    const uint64_t used  = head - tail;

    if (used + total > RING_PAYLOAD_SIZE) return false;  // ring full

    // Write the frame (length prefix, type byte, payload)
    ring_write_bytes(rb, head, &total, RING_LEN_PREFIX);
    ring_write_bytes(rb, head + RING_LEN_PREFIX, &type, 1);
    if (payload_len > 0)
        ring_write_bytes(rb, head + RING_LEN_PREFIX + 1, payload, payload_len);

    // Publish: advance head after the frame is fully visible
    rb->head.store(head + total, std::memory_order_release);
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Pop — consumer side
//  Reads one framed message. Returns the total frame size (including prefix)
//  on success, or 0 when the ring is empty or the frame is corrupt.
//  type_out, payload_out, and payload_len_out are optional (may be nullptr).
// ═══════════════════════════════════════════════════════════════════════════
inline uint32_t ring_pop(RingBuffer* rb,
                          uint8_t*      type_out,
                          void*         payload_out,
                          uint32_t      payload_cap,
                          uint32_t*     payload_len_out) noexcept {
    const uint64_t head = rb->head.load(std::memory_order_acquire);
    const uint64_t tail = rb->tail.load(std::memory_order_acquire);
    if (head == tail) return 0;  // empty

    const uint32_t total = ring_read_u32(rb, tail);
    // Sanity: frame must have at least len + type, and must not exceed max
    if (total < RING_LEN_PREFIX + 1 ||
        total > RING_FRAME_MAX + RING_LEN_PREFIX + 1) return 0;  // corrupt
    if (head - tail < total) return 0;  // not fully committed yet

    uint8_t type = 0;
    ring_read_bytes(rb, tail + RING_LEN_PREFIX, &type, 1);
    const uint32_t plen = total - RING_LEN_PREFIX - 1;

    if (payload_out && plen > 0 && plen <= payload_cap)
        ring_read_bytes(rb, tail + RING_LEN_PREFIX + 1, payload_out, plen);

    if (type_out)       *type_out       = type;
    if (payload_len_out) *payload_len_out = plen;

    // Publish: advance tail after the frame is consumed
    rb->tail.store(tail + total, std::memory_order_release);
    return total;
}