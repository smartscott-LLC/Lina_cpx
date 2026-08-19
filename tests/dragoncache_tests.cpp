// dragoncache_tests.cpp — the RAM-unlock spoke: the unified header, the TX/RX
// rings, and the Hub lifecycle. Runs against a sparse temp file for the pool —
// no root, no huge pages required for the unit surface (the carve tool itself
// owns the real hugetlbfs setup).

#include "dragon_map.h"
#include "dragon_ring.h"
#include "dragoncache.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

// Create a sparse pool file of exactly TOTAL_POOL_SIZE carrying the given
// magic. Returns the path (caller unlinks). Empty on failure.
std::string make_pool(uint64_t magic) {
    char path[] = "/tmp/lina_dragoncache_test_XXXXXX";
    const int fd = mkstemp(path);
    if (fd < 0) return {};
    if (ftruncate(fd, static_cast<off_t>(TOTAL_POOL_SIZE)) != 0) {
        close(fd);
        unlink(path);
        return {};
    }
    void* base = mmap(nullptr, TOTAL_POOL_SIZE, PROT_READ | PROT_WRITE,
                      MAP_SHARED, fd, 0);
    if (base == MAP_FAILED) {
        close(fd);
        unlink(path);
        return {};
    }
    static_cast<DragonMap*>(base)->magic = magic;
    msync(base, sizeof(DragonMap), MS_SYNC);
    munmap(base, TOTAL_POOL_SIZE);
    close(fd);
    return path;
}

// ── 1. The unified address map — geometry is the contract ───────────────────
void test_geometry() {
    check(sizeof(DragonMap) == 64, "DragonMap is one 64B cache line");
    check(alignof(DragonMap) == 64, "DragonMap is 64B aligned");
    check(DRAGON_MAGIC == 0x4C494E4143415256ULL, "carve magic constant");
    check(DRAGON_MAGIC != 0, "magic is non-zero");

    check(ADDR_TX_RING + TX_RING_SIZE <= TOTAL_POOL_SIZE,
          "TX ring fits in the pool");
    check(ADDR_RX_RING + RX_RING_SIZE <= TOTAL_POOL_SIZE,
          "RX ring fits in the pool");
    check(WORK_AREA_OFFSET + WORK_AREA_SIZE == MODULE_SIZE,
          "work area fills Chamber A exactly");

    check(TOTAL_POOL_SIZE / PAGE_2M == 520, "pool is 520 x 2M huge pages");
    check(TOTAL_CARVE_PAGES == 520 + MODEL_QWEN_PAGES + MODEL_MMPROJ_PAGES,
          "carve page accounting");
}

// ── 2. Hub lifecycle against a real (sparse) pool ───────────────────────────
void test_hub_lifecycle() {
    const std::string pool = make_pool(DRAGON_MAGIC);
    check(!pool.empty(), "sparse pool created");

    lina::dragoncache::Hub hub;
    check(hub.attach(pool), "attach to a valid pool");
    check(hub.attached(), "attached() after attach");

    check(hub.status() == STATUS_OFFLINE, "fresh pool starts offline");
    const uint64_t c0 = hub.clock();
    check(c0 == 0, "clock starts at 0");

    hub.set_status(STATUS_LIVE);
    check(hub.status() == STATUS_LIVE, "set_status(STATE_LIVE)");
    check(hub.clock() > c0, "status transition ticks the clock");

    hub.spoke_ready(SPOKE_VOICE);
    check((hub.spokes() & SPOKE_VOICE) != 0, "voice spoke registers its bit");
    hub.spoke_ready(SPOKE_MEMORY_MODULE);
    check((hub.spokes() & SPOKE_MEMORY_MODULE) != 0,
          "memory spoke registers its bit");

    hub.spoke_offline(SPOKE_VOICE);
    check((hub.spokes() & SPOKE_VOICE) == 0, "voice spoke clears its bit");
    check((hub.spokes() & SPOKE_MEMORY_MODULE) != 0,
          "memory bit survives voice offline");

    hub.spoke_ready(SPOKE_ALL);
    check(hub.spokes() == SPOKE_ALL, "SPOKE_ALL sets every bit");

    hub.detach();
    check(!hub.attached(), "detach() releases the mapping");
    check(hub.status() == STATUS_OFFLINE, "detached hub reports offline");

    unlink(pool.c_str());
}

// ── 3. Ring frames — TX/RX round trips, empty, corrupt, oversize ────────────
void test_rings() {
    const std::string pool = make_pool(DRAGON_MAGIC);
    lina::dragoncache::Hub hub;
    check(hub.attach(pool), "attach for ring tests");
    check(hub.attached(), "ring tests attached");

    // Empty ring pops nothing.
    uint8_t type = 0;
    char buf[4096];
    uint32_t len = 0;
    check(hub.pop_frame(/*tx=*/true, &type, buf, sizeof(buf), &len) == 0,
          "TX ring starts empty");
    check(hub.pop_frame(/*tx=*/false, &type, buf, sizeof(buf), &len) == 0,
          "RX ring starts empty");

    // TX round trip with a real payload.
    const char ping[] = "ping from the core";
    check(hub.push_frame(/*tx=*/true, MSG_COMMAND, ping, sizeof(ping) - 1),
          "push TX command frame");
    len = 0;
    const uint32_t total = hub.pop_frame(/*tx=*/true, &type, buf, sizeof(buf),
                                         &len);
    check(total != 0, "pop TX frame returns its size");
    check(type == MSG_COMMAND, "TX frame type preserved");
    check(len == sizeof(ping) - 1 && std::memcmp(buf, ping, len) == 0,
          "TX frame payload preserved");

    // RX round trip (spokes → LINA): a telemetry event mirror.
    const char evt[] = "dragoncache spoke attached";
    check(hub.push_frame(/*tx=*/false, MSG_EVENT, evt, sizeof(evt) - 1),
          "push RX event frame");
    len = 0;
    check(hub.pop_frame(/*tx=*/false, &type, buf, sizeof(buf), &len) != 0,
          "pop RX event frame");
    check(type == MSG_EVENT, "RX frame type preserved");
    check(len == sizeof(evt) - 1 && std::memcmp(buf, evt, len) == 0,
          "RX frame payload preserved");

    // Zero-length payload frame.
    check(hub.push_frame(/*tx=*/true, MSG_CONTROL, nullptr, 0),
          "push zero-length control frame");
    len = 99;
    check(hub.pop_frame(/*tx=*/true, &type, buf, sizeof(buf), &len) != 0,
          "pop zero-length frame");
    check(type == MSG_CONTROL, "zero-length frame type preserved");
    check(len == 0, "zero-length frame payload is empty");

    // Oversize frames are refused before the payload is touched.
    uint8_t dummy = 0;
    check(!hub.push_frame(/*tx=*/true, MSG_DATA, &dummy, RING_FRAME_MAX + 1),
          "oversize frame refused");

    // Frames that never fit are refused (ring full discipline).
    check(!hub.push_frame(/*tx=*/true, MSG_DATA, &dummy, RING_PAYLOAD_SIZE),
          "frame larger than the ring refused");

    hub.detach();
    unlink(pool.c_str());
}

// ── 4. Foreign pools are refused — the magic is the contract ────────────────
void test_foreign_pool() {
    const std::string pool = make_pool(0xDEADBEEFDEADBEEFULL);
    check(!pool.empty(), "foreign sparse pool created");

    lina::dragoncache::Hub hub;
    check(!hub.attach(pool), "foreign magic refused");
    check(!hub.attached(), "nothing attached after refusal");
    check(hub.status() == STATUS_OFFLINE, "refused hub stays offline");

    // Push/pop on a detached hub is a safe no-op.
    uint8_t dummy = 0;
    uint32_t len = 0;
    check(!hub.push_frame(true, MSG_COMMAND, &dummy, 1),
          "push on detached hub returns false");
    check(hub.pop_frame(true, &dummy, &dummy, 1, &len) == 0,
          "pop on detached hub returns 0");

    unlink(pool.c_str());
}

} // namespace

int main() {
    test_geometry();
    test_hub_lifecycle();
    test_rings();
    test_foreign_pool();

    if (failures == 0) {
        std::printf("dragoncache_tests: ALL CHECKS GREEN\n");
        return 0;
    }
    std::fprintf(stderr, "dragoncache_tests: %d FAILURES\n", failures);
    return 1;
}
