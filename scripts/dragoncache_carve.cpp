/**
 * dragoncache_carve.cpp — the DragonCache carve (v2), pure C++, zero Python
 *
 * "Safe by design. Not safe by limitation."
 *
 * Reserves huge pages, mounts hugetlbfs, and creates:
 *   /mnt/huge/lina_pool          — the IPC pool (header + rings + work areas)
 *   /mnt/huge/lina_model.gguf    — Qwen2-VL-2B, pinned (llama.cpp mmaps it)
 *   /mnt/huge/lina_mmproj.gguf   — the vision projector, pinned
 *
 * The models live OUTSIDE the pool so llama.cpp loads from real pinned huge
 * pages — zero page faults, never evicted. The pool carries the DragonMap
 * heartbeat + the TX/RX rings every spoke shares.
 *
 * Usage (root):
 *   dragoncache_carve             carve (default)
 *   dragoncache_carve --status    report state (non-root ok, partial)
 *   dragoncache_carve --verify    full verification
 *   dragoncache_carve --release   tear down
 */

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <unistd.h>
#include <fcntl.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <pwd.h>
#include <string>

#include "dragon_map.h"
#include "dragon_ring.h"

namespace {

constexpr const char* kPoolPath        = "/mnt/huge/lina_pool";
constexpr const char* kHugetlbfs       = "/mnt/huge";
constexpr const char* kSysfs2M         = "/sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages";
constexpr const char* kSysfs2MFree     = "/sys/kernel/mm/hugepages/hugepages-2048kB/free_hugepages";
constexpr const char* kAddressMapFile  = "/home/server/Lina_cpx/.dragoncache_map";

constexpr const char* kModelQwenSrc   = "/home/server/models/lina-local/Qwen2-VL-2B-Instruct-Q6_K.gguf";
constexpr const char* kModelMmprojSrc = "/home/server/models/lina-local/mmproj-Qwen2-VL-2B-Instruct-f16.gguf";

constexpr uint64_t kHeadroomPages = 96;
constexpr uint32_t kGgufMagic     = 0x46554747; // "GGUF" LE

uint64_t file_size(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return static_cast<uint64_t>(st.st_size);
}

bool write_sysfs(const char* path, uint64_t value) {
    std::ofstream out(path);
    if (!out) {
        std::fprintf(stderr, "[-] cannot write %s: %s\n", path,
                     std::strerror(errno));
        return false;
    }
    out << value;
    return true;
}

uint64_t read_sysfs(const char* path) {
    std::ifstream in(path);
    uint64_t value = 0;
    if (in) in >> value;
    return value;
}

bool is_mounted(const char* path) {
    std::ifstream mounts("/proc/mounts");
    std::string line;
    while (std::getline(mounts, line)) {
        if (line.find(path) != std::string::npos
            && line.find("hugetlbfs") != std::string::npos) {
            return true;
        }
    }
    return false;
}

void reserve_pages(uint64_t total_pages) {
    const uint64_t target = total_pages + kHeadroomPages;
    const uint64_t current = read_sysfs(kSysfs2M);
    if (current < target) {
        if (write_sysfs(kSysfs2M, target)) {
            std::printf("[+] reserved %llu x 2M huge pages (was %llu)\n",
                        static_cast<unsigned long long>(target),
                        static_cast<unsigned long long>(current));
        }
    } else {
        std::printf("[+] huge pages already reserved: %llu\n",
                    static_cast<unsigned long long>(current));
    }
}

bool mount_hugetlbfs() {
    if (is_mounted(kHugetlbfs)) return true;
    if (mount("none", kHugetlbfs, "hugetlbfs", 0, "pagesize=2M") != 0) {
        std::fprintf(stderr, "[-] mount hugetlbfs: %s\n", std::strerror(errno));
        return false;
    }
    std::printf("[+] hugetlbfs mounted at %s\n", kHugetlbfs);
    return true;
}

// ── File ownership for the live carve ───────────────────────────────────────
// The models are read-only weights for llama.cpp (0644 — any local process
// may mmap them). The pool is 0600 but owned by the invoking user (SUDO_USER
// when run via sudo), so interactive runs and the root service both work.
// Falls back to root:root when SUDO_USER is unset or unknown.
void apply_ownership(const char* path) {
    const char* sudo_user = std::getenv("SUDO_USER");
    if (sudo_user && *sudo_user) {
        struct passwd* pw = getpwnam(sudo_user);
        if (pw && chown(path, pw->pw_uid, pw->pw_gid) == 0) return;
    }
    // keep root:root (already the owner) — nothing to do
}

// Creates a hugetlbfs file of `pages` 2M pages and copies `src` into it.
bool place_model(const char* dst_path, const char* src_path, uint64_t pages,
                 const char* label) {
    const uint64_t placed = pages * PAGE_2M;
    const uint64_t src_size = file_size(src_path);
    if (src_size == 0) {
        std::fprintf(stderr, "[-] %s missing or empty: %s\n", label, src_path);
        return false;
    }
    if (src_size > placed) {
        std::fprintf(stderr, "[-] %s (%llu B) exceeds %llu pages\n", label,
                     static_cast<unsigned long long>(src_size),
                     static_cast<unsigned long long>(pages));
        return false;
    }

    // Recreate fresh each carve (like the pool) so the mode/owner are ours:
    // models are 0644 read-only weights; O_TRUNC alone keeps the old mode.
    unlink(dst_path);
    int fd = open(dst_path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        std::fprintf(stderr, "[-] cannot create %s: %s\n", dst_path,
                     std::strerror(errno));
        return false;
    }
    if (ftruncate(fd, static_cast<off_t>(placed)) != 0) {
        std::fprintf(stderr, "[-] ftruncate %s: %s\n", dst_path,
                     std::strerror(errno));
        close(fd);
        return false;
    }
    void* map = mmap(nullptr, placed, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        std::fprintf(stderr, "[-] mmap %s: %s\n", dst_path,
                     std::strerror(errno));
        close(fd);
        return false;
    }

    std::FILE* in = std::fopen(src_path, "rb");
    if (!in) {
        std::fprintf(stderr, "[-] cannot open %s\n", src_path);
        munmap(map, placed);
        close(fd);
        return false;
    }
    char* dst = static_cast<char*>(map);
    char buf[2 * 1024 * 1024];
    uint64_t written = 0;
    while (written < src_size) {
        const size_t want = static_cast<size_t>(
            std::min<uint64_t>(sizeof(buf), src_size - written));
        const size_t got = std::fread(buf, 1, want, in);
        if (got == 0) break;
        std::memcpy(dst + written, buf, got);
        written += got;
    }
    std::fclose(in);
    msync(map, placed, MS_SYNC);
    munmap(map, placed);
    close(fd);
    std::printf("[+] %s placed at %s (%llu MiB, %llu pages)\n", label,
                dst_path,
                static_cast<unsigned long long>(placed / 1024 / 1024),
                static_cast<unsigned long long>(pages));
    apply_ownership(dst_path);
    return true;
}

// Zero a region of the pool (slot / ring sentinel).
void zero_region(void* base, uint64_t offset, uint64_t size, const char* label) {
    std::memset(static_cast<char*>(base) + offset, 0, size);
    msync(static_cast<char*>(base) + offset, size, MS_SYNC);
    std::printf("[+] %s zeroed at %llu KiB (%llu bytes)\n", label,
                static_cast<unsigned long long>(offset / 1024),
                static_cast<unsigned long long>(size));
}

void write_address_map() {
    std::ofstream out(kAddressMapFile);
    if (!out) {
        std::fprintf(stderr, "[-] cannot write %s\n", kAddressMapFile);
        return;
    }
    out << "# DragonCache Address Map (v2 — LINA Core Substrate)\n";
    out << "# Generated by dragoncache_carve on carve\n";
    out << "# Pool: " << (TOTAL_POOL_SIZE / 1024 / 1024)
        << " MiB (" << (TOTAL_POOL_SIZE / PAGE_2M) << " huge pages)\n";
    out << "DRAGONCACHE_POOL_SIZE=" << TOTAL_POOL_SIZE << "\n";
    out << "DRAGONCACHE_POOL_FILE=" << kPoolPath << "\n";
    out << "DRAGONCACHE_HEADER_OFFSET=" << HEADER_OFFSET << "\n";
    out << "DRAGONCACHE_HEADER_SIZE=" << HEADER_SIZE << "\n";
    out << "DRAGONCACHE_MODULE_OFFSET=" << MODULE_OFFSET << "\n";
    out << "DRAGONCACHE_MODULE_SIZE=" << MODULE_SIZE << "\n";
    out << "DRAGONCACHE_SLOT_SERVICE_STATE=" << ADDR_SERVICE_STATE << "\n";
    out << "DRAGONCACHE_SLOT_VALUE_STATE=" << ADDR_VALUE_STATE << "\n";
    out << "DRAGONCACHE_SLOT_MEMORY_STATE=" << ADDR_MEMORY_STATE << "\n";
    out << "DRAGONCACHE_TX_RING_OFFSET=" << ADDR_TX_RING << "\n";
    out << "DRAGONCACHE_TX_RING_SIZE=" << TX_RING_SIZE << "\n";
    out << "DRAGONCACHE_RX_RING_OFFSET=" << ADDR_RX_RING << "\n";
    out << "DRAGONCACHE_RX_RING_SIZE=" << RX_RING_SIZE << "\n";
    out << "DRAGONCACHE_WORK_AREA_OFFSET=" << MODULE_OFFSET + WORK_AREA_OFFSET << "\n";
    out << "DRAGONCACHE_WORK_AREA_SIZE=" << WORK_AREA_SIZE << "\n";
    out << "DRAGONCACHE_MODEL_QWEN=" << MODEL_FILE_QWEN << "\n";
    out << "DRAGONCACHE_MODEL_MMPROJ=" << MODEL_FILE_MMPROJ << "\n";
    out << "DRAGONCACHE_TOTAL_CARVE_PAGES=" << TOTAL_CARVE_PAGES << "\n";
    out.close();
    std::printf("[+] address map written to %s\n", kAddressMapFile);
}

int do_carve() {
    if (geteuid() != 0) {
        std::fprintf(stderr, "the carve needs root — run with sudo\n");
        return 1;
    }
    reserve_pages(TOTAL_CARVE_PAGES);
    if (!mount_hugetlbfs()) return 1;

    // Pool: create + truncate + map.
    unlink(kPoolPath);
    int fd = open(kPoolPath, O_RDWR | O_CREAT, 0600);
    if (fd < 0) {
        std::fprintf(stderr, "[-] cannot create %s: %s\n", kPoolPath,
                     std::strerror(errno));
        return 1;
    }
    if (ftruncate(fd, static_cast<off_t>(TOTAL_POOL_SIZE)) != 0) {
        std::fprintf(stderr, "[-] ftruncate %s: %s\n", kPoolPath,
                     std::strerror(errno));
        close(fd);
        return 1;
    }
    void* base = mmap(nullptr, TOTAL_POOL_SIZE, PROT_READ | PROT_WRITE,
                      MAP_SHARED, fd, 0);
    if (base == MAP_FAILED) {
        std::fprintf(stderr, "[-] mmap pool: %s\n", std::strerror(errno));
        close(fd);
        return 1;
    }

    // DragonMap header.
    auto* header = static_cast<DragonMap*>(base);
    header->global_clock.store(0, std::memory_order_relaxed);
    header->system_status.store(STATUS_BOOTING, std::memory_order_relaxed);
    header->spoke_health.store(0, std::memory_order_relaxed);
    header->magic = DRAGON_MAGIC;
    msync(base, sizeof(DragonMap), MS_SYNC);
    std::printf("[+] DragonMap: magic=ok clock=0 status=booting\n");
    apply_ownership(kPoolPath);

    // Touch every 2M page to wire it resident.
    volatile char* touch = static_cast<char*>(base);
    for (uint64_t off = 0; off < TOTAL_POOL_SIZE; off += PAGE_2M) {
        touch[off] = 0;
    }

    // Slots + ring sentinels (Chamber A).
    zero_region(base, ADDR_SERVICE_STATE, 512, "ServiceState");
    zero_region(base, ADDR_VALUE_STATE, 512, "ValueEngine");
    zero_region(base, ADDR_MEMORY_STATE, 512, "MemoryModule");
    zero_region(base, ADDR_TX_RING, PAGE_2M, "TX-Ring (first 2 MiB)");
    zero_region(base, ADDR_RX_RING, PAGE_2M, "RX-Ring (first 2 MiB)");

    // Mark live.
    dragonmap_set_live(base);
    msync(base, sizeof(DragonMap), MS_SYNC);

    // Models on their own hugetlbfs files.
    if (!place_model(MODEL_FILE_QWEN, kModelQwenSrc, MODEL_QWEN_PAGES,
                     "Qwen2-VL-2B")) {
        return 1;
    }
    if (!place_model(MODEL_FILE_MMPROJ, kModelMmprojSrc, MODEL_MMPROJ_PAGES,
                     "mmproj")) {
        return 1;
    }

    write_address_map();

    if (mlock(base, TOTAL_POOL_SIZE) == 0) {
        std::printf("[+] pool pinned — never swapped\n");
    } else {
        std::printf("[!] mlock: %s (expected if not root)\n",
                    std::strerror(errno));
    }

    std::printf("[+] DragonCache v2 active — pool %llu MiB + models %llu + "
                "%llu = %llu pages (%llu MiB) live\n",
                static_cast<unsigned long long>(TOTAL_POOL_SIZE / 1024 / 1024),
                static_cast<unsigned long long>(MODEL_QWEN_PAGES),
                static_cast<unsigned long long>(MODEL_MMPROJ_PAGES),
                static_cast<unsigned long long>(TOTAL_CARVE_PAGES),
                static_cast<unsigned long long>(TOTAL_CARVE_PAGES * 2));
    return 0;
}

bool check_gguf(const char* path, const char* label) {
    const uint64_t size = file_size(path);
    if (size == 0) {
        std::printf("  %-8s %s — MISSING\n", label, path);
        return false;
    }
    std::FILE* in = std::fopen(path, "rb");
    if (!in) {
        std::printf("  %-8s %s — cannot open\n", label, path);
        return false;
    }
    unsigned char magic[4] = {0, 0, 0, 0};
    const size_t got = std::fread(magic, 1, 4, in);
    std::fclose(in);
    if (got != 4) {
        std::printf("  %-8s %s — unreadable header\n", label, path);
        return false;
    }
    const uint32_t m = static_cast<uint32_t>(magic[0])
                     | (static_cast<uint32_t>(magic[1]) << 8)
                     | (static_cast<uint32_t>(magic[2]) << 16)
                     | (static_cast<uint32_t>(magic[3]) << 24);
    const bool ok = (m == kGgufMagic);
    std::printf("  %-8s %s — %s (%llu MiB)\n", label, path,
                ok ? "GGUF ok" : "MAGIC MISMATCH",
                static_cast<unsigned long long>(size / 1024 / 1024));
    return ok;
}

int do_verify() {
    bool all_ok = true;
    std::printf("── DragonCache Verify (v2) ──\n");

    const uint64_t pool_size = file_size(kPoolPath);
    std::printf("  Pool:     %s — %s (%llu MiB)\n", kPoolPath,
                pool_size == TOTAL_POOL_SIZE ? "size ok" : "SIZE MISMATCH",
                static_cast<unsigned long long>(pool_size / 1024 / 1024));
    all_ok &= (pool_size == TOTAL_POOL_SIZE);

    // DragonMap state.
    const int fd = open(kPoolPath, O_RDONLY);
    if (fd >= 0) {
        void* base = mmap(nullptr, sizeof(DragonMap), PROT_READ, MAP_SHARED,
                          fd, 0);
        if (base != MAP_FAILED) {
            auto* header = static_cast<DragonMap*>(base);
            const bool magic_ok = (header->magic == DRAGON_MAGIC);
            const uint32_t status = header->system_status.load(
                std::memory_order_acquire);
            const uint32_t health = header->spoke_health.load(
                std::memory_order_acquire);
            std::printf("  Header:   magic=%s status=%u health=0x%x clock=%llu\n",
                        magic_ok ? "ok" : "BAD",
                        status, health,
                        static_cast<unsigned long long>(
                            header->global_clock.load(std::memory_order_acquire)));
            all_ok &= magic_ok;
            munmap(base, sizeof(DragonMap));
        }
        close(fd);
    } else {
        std::printf("  Header:   cannot open pool (need root?)\n");
        all_ok = false;
    }

    all_ok &= check_gguf(MODEL_FILE_QWEN, "Qwen");
    all_ok &= check_gguf(MODEL_FILE_MMPROJ, "mmproj");

    // 2M huge page accounting — the pool we manage (sysfs, authoritative).
    const uint64_t huge_total = read_sysfs(kSysfs2M);
    const uint64_t huge_free  = read_sysfs(kSysfs2MFree);
    std::printf("  Huge:     %llu x 2M total, %llu used, %llu free\n",
                static_cast<unsigned long long>(huge_total),
                static_cast<unsigned long long>(huge_total > huge_free
                                                    ? huge_total - huge_free
                                                    : 0),
                static_cast<unsigned long long>(huge_free));

    std::printf(all_ok ? "[+] verify: ALL OK\n" : "[-] verify: FAILURES\n");
    return all_ok ? 0 : 1;
}

int do_status() {
    std::printf("── DragonCache Status (v2) ──\n");
    const uint64_t pool_size = file_size(kPoolPath);
    std::printf("  Pool:     %s (%llu MiB)%s\n", kPoolPath,
                static_cast<unsigned long long>(pool_size / 1024 / 1024),
                pool_size ? "" : " — NOT PRESENT");
    check_gguf(MODEL_FILE_QWEN, "Qwen");
    check_gguf(MODEL_FILE_MMPROJ, "mmproj");
    std::printf("  hugetlbfs: %s\n", is_mounted(kHugetlbfs) ? "mounted"
                                                            : "NOT MOUNTED");
    // 2M huge page accounting — the pool we manage (sysfs, authoritative).
    const uint64_t huge_total = read_sysfs(kSysfs2M);
    const uint64_t huge_free  = read_sysfs(kSysfs2MFree);
    std::printf("  Huge:     %llu x 2M = %.1f GiB total, %llu used, %llu free\n",
                static_cast<unsigned long long>(huge_total),
                huge_total * 2.0 / 1024.0,
                static_cast<unsigned long long>(huge_total > huge_free
                                                    ? huge_total - huge_free
                                                    : 0),
                static_cast<unsigned long long>(huge_free));
    return 0;
}

int do_release() {
    if (geteuid() != 0) {
        std::fprintf(stderr, "release needs root — run with sudo\n");
        return 1;
    }
    bool changed = false;
    for (const char* path : {kPoolPath, MODEL_FILE_QWEN, MODEL_FILE_MMPROJ}) {
        if (unlink(path) == 0) {
            std::printf("[+] released %s\n", path);
            changed = true;
        }
    }
    if (is_mounted(kHugetlbfs)) {
        if (umount2(kHugetlbfs, MNT_DETACH) == 0) {
            std::printf("[+] unmounted %s\n", kHugetlbfs);
            changed = true;
        }
    }
    if (write_sysfs(kSysfs2M, 0)) {
        std::printf("[+] huge pages released\n");
        changed = true;
    }
    unlink(kAddressMapFile);
    if (!changed) std::printf("[+] nothing to release\n");
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    const bool do_verify_ = argc > 1 && std::string(argv[1]) == "--verify";
    const bool do_status_ = argc > 1 && std::string(argv[1]) == "--status";
    const bool do_release_ = argc > 1 && std::string(argv[1]) == "--release";
    if (do_verify_) return do_verify();
    if (do_status_) return do_status();
    if (do_release_) return do_release();
    return do_carve();
}
