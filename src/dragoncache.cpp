/**
 * dragoncache.cpp — her spoke on the DragonCache (Implementation)
 */

#include "dragoncache.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace lina::dragoncache {

bool Hub::attach(const std::string& pool_path) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (base_) return true; // already attached

    struct stat st;
    if (stat(pool_path.c_str(), &st) != 0) return false;
    if (st.st_size < static_cast<off_t>(TOTAL_POOL_SIZE)) return false;

    const int fd = open(pool_path.c_str(), O_RDWR);
    if (fd < 0) return false;
    void* base = mmap(nullptr, TOTAL_POOL_SIZE, PROT_READ | PROT_WRITE,
                      MAP_SHARED, fd, 0);
    if (base == MAP_FAILED) {
        close(fd);
        return false;
    }

    auto* header = static_cast<DragonMap*>(base);
    if (header->magic != DRAGON_MAGIC) {
        // Foreign pool — refuse to touch it.
        munmap(base, TOTAL_POOL_SIZE);
        close(fd);
        return false;
    }

    fd_ = fd;
    base_ = base;
    // The pool is huge-page aligned and every region offset is a 64B multiple,
    // so the RingBuffer alignment (64B) is guaranteed at runtime.
    tx_ = reinterpret_cast<RingBuffer*>(
        static_cast<char*>(base_) + ADDR_TX_RING);
    rx_ = reinterpret_cast<RingBuffer*>(
        static_cast<char*>(base_) + ADDR_RX_RING);
    return true;
}

void Hub::detach() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (base_) {
        munmap(base_, TOTAL_POOL_SIZE);
        base_ = nullptr;
    }
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
    tx_ = nullptr;
    rx_ = nullptr;
}

void Hub::set_status(uint32_t status) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!base_) return;
    auto* header = static_cast<DragonMap*>(base_);
    header->system_status.store(status, std::memory_order_release);
    header->global_clock.fetch_add(1, std::memory_order_acq_rel);
}

void Hub::spoke_ready(uint32_t bit) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (base_) dragonmap_spoke_ready(base_, bit);
}

void Hub::spoke_offline(uint32_t bit) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (base_) dragonmap_spoke_offline(base_, bit);
}

uint64_t Hub::clock() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return base_ ? dragonmap_clock(base_) : 0;
}

uint32_t Hub::status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return base_ ? dragonmap_status(base_) : STATUS_OFFLINE;
}

uint32_t Hub::spokes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return base_ ? dragonmap_spokes(base_) : 0;
}

bool Hub::push_frame(bool tx, uint8_t type, const void* payload,
                     uint32_t payload_len) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!base_) return false;
    RingBuffer* ring = tx ? tx_ : rx_;
    if (!ring) return false;
    return ring_push(ring, type, payload, payload_len);
}

uint32_t Hub::pop_frame(bool tx, uint8_t* type_out, void* payload_out,
                        uint32_t payload_cap, uint32_t* payload_len_out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!base_) return 0;
    RingBuffer* ring = tx ? tx_ : rx_;
    if (!ring) return 0;
    return ring_pop(ring, type_out, payload_out, payload_cap,
                    payload_len_out);
}

} // namespace lina::dragoncache
