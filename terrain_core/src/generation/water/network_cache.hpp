#pragma once

// The cache of the fine network (drainage_network.hpp), internal to terrain_core: the nodes of the
// cells and the blocks, kept under a budget of bytes, the least recently used dropped first. An
// entry is made out of the lock (two threads may make the same one: the same bits) and kept whole;
// nothing half made is ever seen (an exception leaves the cache as it was).

#include "network_block.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <type_traits>

namespace terrain::water {

class NetworkCache {
public:
    explicit NetworkCache(std::size_t budget) : budget_(budget) {}

    std::shared_ptr<const CellNodes> nodes(std::size_t cell,
                                           const std::function<CellNodes()>& make) {
        return get<CellNodes>(nodes_, cell, make,
                              [](const CellNodes& n) { return n.height.size() * 9; });
    }
    std::shared_ptr<const NetworkBlock> block(std::size_t cell,
                                              const std::function<NetworkBlock()>& make) {
        return get<NetworkBlock>(blocks_, cell, make, [](const NetworkBlock& b) {
            return b.bytes();
        });
    }
    [[nodiscard]] std::size_t bytes() const {
        const std::lock_guard<std::mutex> lock(mutex_);
        return bytes_;
    }
    [[nodiscard]] std::size_t peak() const {
        const std::lock_guard<std::mutex> lock(mutex_);
        return peak_;
    }
    [[nodiscard]] std::size_t blocksMade() const {
        const std::lock_guard<std::mutex> lock(mutex_);
        return blocksMade_;
    }

private:
    template <class T> struct Entry {
        std::shared_ptr<const T> value;
        std::size_t bytes = 0;
        std::uint64_t used = 0;
    };

    template <class T, class Size>
    std::shared_ptr<const T> get(std::map<std::size_t, Entry<T>>& map, std::size_t key,
                                 const std::function<T()>& make, Size size) {
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            if (auto it = map.find(key); it != map.end()) {
                it->second.used = ++clock_;
                return it->second.value;
            }
        }
        auto value = std::make_shared<const T>(make());
        const std::lock_guard<std::mutex> lock(mutex_);
        if (auto it = map.find(key); it != map.end()) {
            it->second.used = ++clock_;
            return it->second.value;
        }
        const std::size_t bytes = size(*value) + sizeof(Entry<T>);
        map.emplace(key, Entry<T>{value, bytes, ++clock_});
        bytes_ += bytes;
        blocksMade_ += std::is_same_v<T, NetworkBlock> ? 1 : 0;
        peak_ = std::max(peak_, bytes_);
        evict(key);
        return value;
    }

    // Drops the least recently used entries but the one just made until under the budget.
    void evict(std::size_t keep) {
        while (bytes_ > budget_) {
            std::uint64_t oldest = clock_ + 1;
            bool inNodes = false;
            std::size_t key = 0;
            for (const auto& [k, e] : nodes_) {
                if (e.used < oldest && k != keep) {
                    oldest = e.used;
                    key = k;
                    inNodes = true;
                }
            }
            for (const auto& [k, e] : blocks_) {
                if (e.used < oldest && k != keep) {
                    oldest = e.used;
                    key = k;
                    inNodes = false;
                }
            }
            if (oldest > clock_) {
                return;
            }
            if (inNodes) {
                bytes_ -= nodes_[key].bytes;
                nodes_.erase(key);
            } else {
                bytes_ -= blocks_[key].bytes;
                blocks_.erase(key);
            }
        }
    }

    std::size_t budget_;
    mutable std::mutex mutex_;
    std::map<std::size_t, Entry<CellNodes>> nodes_;
    std::map<std::size_t, Entry<NetworkBlock>> blocks_;
    std::size_t bytes_ = 0;
    std::size_t peak_ = 0;
    std::size_t blocksMade_ = 0;
    std::uint64_t clock_ = 0;
};

} // namespace terrain::water
