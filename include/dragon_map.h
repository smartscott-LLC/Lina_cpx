#pragma once
#include <atomic>
#include <cstdint>
#include <cstring>

// ═══════════════════════════════════════════════════════════════════════════
//  dragon_map.h — The Unified Address Map Contract for LINA's DragonCache
//  Carve. Every spoke includes this header. Every spoke mmaps the same
//  physical frames at /mnt/huge/lina_pool. Offsets carve the pool into
//  regions. The DragonMap at offset 0 is the heartbeat — one atomic cache
//  line that makes every spoke state-aware.
//
//  v2 layout (LINA Core Substrate, 2026-08-18): the models moved OUT of the
//  pool into standalone hugetlbfs files (/mnt/huge/lina_model.gguf,
//  /mnt/huge/lina_mmproj.gguf) so llama.cpp mmaps real pinned huge pages.
//  The pool carries only the header + rings + work areas — lean, ~1 GiB.
//
//  The DragonCache is not a pipeline. It is a hub-and-spoke architecture
//  where every spoke reads the same header and knows the state of every
//  other spoke. There is no "last in the chain" — every spoke is aware.
// ═══════════════════════════════════════════════════════════════════════════

// ── Page size ──────────────────────────────────────────────────────────────
constexpr uint64_t PAGE_2M = 2ULL * 1024 * 1024;

// ── Pool layout (v2) ───────────────────────────────────────────────────────
//   Region        Offset     Size       Contents
//   ──────        ──────     ────       ────────
//   Header        0          16 MiB     DragonMap heartbeat + spare
//   Chamber A     16 MiB   1024 MiB     Module states, TX/RX ring, work areas
//   ──────        ──────     ────       ────────
//   Total       1040 MiB = 520 × 2M huge pages (~1.02 GiB)
//
//   Models live OUTSIDE the pool, on their own hugetlbfs files:
//     /mnt/huge/lina_model.gguf   (Qwen2-VL-2B, 607 pages)
//     /mnt/huge/lina_mmproj.gguf  (vision projector, 635 pages)
// ═══════════════════════════════════════════════════════════════════════════

// ── Header region ──────────────────────────────────────────────────────────
constexpr uint64_t HEADER_OFFSET  = 0ULL;
constexpr uint64_t HEADER_SIZE    = 16ULL * 1024 * 1024;          // 16 MiB

// ── Chamber A — Module Offset (spoke live state + TX/RX ring) ──────────────
constexpr uint64_t MODULE_OFFSET  = HEADER_OFFSET + HEADER_SIZE;  // 16 MiB
constexpr uint64_t MODULE_SIZE    = 1ULL * 1024 * 1024 * 1024;    // 1 GiB

//   Chamber A sub-layout (offsets relative to MODULE_OFFSET):
//   ─────────────────────────────────────────────────────────────────
//   Range          Size     Contents
//   0x000000000    512 KiB  Module state slots (spoke state blocks)
//   0x000080000    256 MiB  TX ring (SPSC, variable-length frames)
//   0x100080000    256 MiB  RX ring (SPSC, variable-length frames)
//   0x200080000    ~512 MiB Spoke work areas
//   ─────────────────────────────────────────────────────────────────

//   Module state slots (each 512 bytes, 64B cache-line aligned):
constexpr uint64_t MODULE_SLOT_REGION_OFFSET = 0ULL;
constexpr uint64_t MODULE_SLOT_REGION_SIZE    = 512ULL * 1024;       // 512 KiB

constexpr uint64_t SLOT_DRAGONMAP      = 0ULL;       // offset 0x000000 → DragonMap itself
constexpr uint64_t SLOT_SERVICE_STATE  = 0x000100;   // offset 0x000100 → CarveServiceState (512B)
constexpr uint64_t SLOT_VALUE_STATE    = 0x000300;   // offset 0x000300 → CarveModuleState (512B)
constexpr uint64_t SLOT_MEMORY_STATE   = 0x000500;   // offset 0x000500 → CarveMemoryState (512B)
// ... more slots available at 0x000700, 0x000900, 0x000B00, etc. (each 512B)

//   TX ring (offsets relative to MODULE_OFFSET):
constexpr uint64_t TX_RING_OFFSET = MODULE_SLOT_REGION_SIZE;         // 512 KiB
constexpr uint64_t TX_RING_SIZE   = 256ULL * 1024 * 1024;           // 256 MiB

//   RX ring (offsets relative to MODULE_OFFSET):
constexpr uint64_t RX_RING_OFFSET = TX_RING_OFFSET + TX_RING_SIZE;  // 256 MiB + 512 KiB
constexpr uint64_t RX_RING_SIZE   = 256ULL * 1024 * 1024;           // 256 MiB

//   Spoke work areas (remaining space in Chamber A, ~512 MiB):
constexpr uint64_t WORK_AREA_OFFSET = RX_RING_OFFSET + RX_RING_SIZE;
constexpr uint64_t WORK_AREA_SIZE   = MODULE_SIZE - WORK_AREA_OFFSET;

// ── Model files (v2 — outside the pool, on hugetlbfs) ──────────────────────
constexpr const char* MODEL_FILE_QWEN   = "/mnt/huge/lina_model.gguf";
constexpr const char* MODEL_FILE_MMPROJ = "/mnt/huge/lina_mmproj.gguf";
constexpr uint64_t    MODEL_QWEN_PAGES  = 607;   // 1,214 MiB (file is 1,272,738,560 B)
constexpr uint64_t    MODEL_MMPROJ_PAGES = 635;  // 1,270 MiB (file is 1,331,656,192 B)

// ── Total carve (pool + models) ─────────────────────────────────────────────
constexpr uint64_t TOTAL_POOL_SIZE = 1040ULL * 1024 * 1024;   // 1040 MiB = 520 pages
constexpr uint64_t TOTAL_CARVE_PAGES =
    (TOTAL_POOL_SIZE / PAGE_2M) + MODEL_QWEN_PAGES + MODEL_MMPROJ_PAGES; // 1762

// ── Spoke health bits ──────────────────────────────────────────────────────
// Each spoke sets its bit in DragonMap.spoke_health when it comes online
// and clears it when it goes offline. The DragonCache monitors these bits.
constexpr uint32_t SPOKE_IDENTITY_SERVICE = 1U << 0;
constexpr uint32_t SPOKE_VALUE_ENGINE     = 1U << 1;
constexpr uint32_t SPOKE_MEMORY_MODULE    = 1U << 2;
constexpr uint32_t SPOKE_CORTEX           = 1U << 3;
constexpr uint32_t SPOKE_VOICE            = 1U << 4;
constexpr uint32_t SPOKE_TX_RING          = 1U << 5;
constexpr uint32_t SPOKE_RX_RING          = 1U << 6;
constexpr uint32_t SPOKE_ALL              = 0x000000FFU;

// ── System status codes ─────────────────────────────────────────────────────
constexpr uint32_t STATUS_OFFLINE  = 0;
constexpr uint32_t STATUS_LIVE     = 1;
constexpr uint32_t STATUS_DEGRADED = 2;
constexpr uint32_t STATUS_BOOTING  = 3;

// ── Carve magic ─────────────────────────────────────────────────────────────
// Written by the carve tool at DragonMap.magic; spokes refuse foreign pools.
constexpr uint64_t DRAGON_MAGIC = 0x4C494E4143415256ULL; // "LINACARV"

// ═══════════════════════════════════════════════════════════════════════════
//  DragonMap — The unified heartbeat header (one cache line, 64 bytes)
//
//  Every spoke mmaps this at offset 0 of the pool file. All fields are
//  std::atomic for lock-free reads/writes. The struct is 64-byte aligned
//  so it fits on a single cache line and is never split across lines.
// ═══════════════════════════════════════════════════════════════════════════
struct alignas(64) DragonMap {
    // +0:  global monotonic clock — ticked on every spoke transition
    std::atomic<uint64_t> global_clock;

    // +8:  system status — STATUS_OFFLINE | STATUS_BOOTING | STATUS_LIVE |
    //      STATUS_DEGRADED
    std::atomic<uint32_t> system_status;

    // +12: spoke health bitmask — each spoke sets its bit when ready
    std::atomic<uint32_t> spoke_health;

    // +16: carve magic (v2) — validates the pool before attaching
    uint64_t magic;

    // +24: reserved — fill to 64 bytes
    uint64_t _pad[4];

    DragonMap()
        : global_clock(0)
        , system_status(STATUS_OFFLINE)
        , spoke_health(0)
        , magic(DRAGON_MAGIC)
        , _pad{0, 0, 0, 0} {}
};
static_assert(sizeof(DragonMap) == 64,
              "DragonMap must be exactly 64 bytes (one cache line)");
static_assert(alignof(DragonMap) == 64,
              "DragonMap must be 64-byte aligned");

// ═══════════════════════════════════════════════════════════════════════════
//  Convenience: absolute carve address for each spoke's state slot
// ═══════════════════════════════════════════════════════════════════════════
constexpr uint64_t ADDR_SERVICE_STATE = MODULE_OFFSET + SLOT_SERVICE_STATE;
constexpr uint64_t ADDR_VALUE_STATE   = MODULE_OFFSET + SLOT_VALUE_STATE;
constexpr uint64_t ADDR_MEMORY_STATE  = MODULE_OFFSET + SLOT_MEMORY_STATE;
constexpr uint64_t ADDR_TX_RING       = MODULE_OFFSET + TX_RING_OFFSET;
constexpr uint64_t ADDR_RX_RING       = MODULE_OFFSET + RX_RING_OFFSET;

// ═══════════════════════════════════════════════════════════════════════════
//  Inline helpers — atomic read/write on the DragonMap at a given base
// ═══════════════════════════════════════════════════════════════════════════

/// Tick the global clock and set system status to live.
inline void dragonmap_set_live(void* base) noexcept {
    auto* dm = static_cast<DragonMap*>(base);
    dm->system_status.store(STATUS_LIVE, std::memory_order_release);
    dm->global_clock.fetch_add(1, std::memory_order_acq_rel);
}

/// Register a spoke as ready (sets its bit and ticks the clock).
inline void dragonmap_spoke_ready(void* base, uint32_t spoke_bit) noexcept {
    auto* dm = static_cast<DragonMap*>(base);
    dm->spoke_health.fetch_or(spoke_bit, std::memory_order_acq_rel);
    dm->global_clock.fetch_add(1, std::memory_order_acq_rel);
}

/// Unregister a spoke (clears its bit and ticks the clock).
inline void dragonmap_spoke_offline(void* base, uint32_t spoke_bit) noexcept {
    auto* dm = static_cast<DragonMap*>(base);
    dm->spoke_health.fetch_and(~spoke_bit, std::memory_order_acq_rel);
    dm->global_clock.fetch_add(1, std::memory_order_acq_rel);
}

/// Read the current clock (monotonic, never decreases).
inline uint64_t dragonmap_clock(void* base) noexcept {
    return static_cast<DragonMap*>(base)->global_clock.load(std::memory_order_acquire);
}

/// Read the spoke health bitmask (which spokes are live).
inline uint32_t dragonmap_spokes(void* base) noexcept {
    return static_cast<DragonMap*>(base)->spoke_health.load(std::memory_order_acquire);
}

/// Read the system status.
inline uint32_t dragonmap_status(void* base) noexcept {
    return static_cast<DragonMap*>(base)->system_status.load(std::memory_order_acquire);
}
