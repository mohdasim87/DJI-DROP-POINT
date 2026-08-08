// util/ring_buffer.h
//
// Fixed-capacity ring buffer used to hold the pose history needed for
// timestamp-aligned video marker projection (spec 6.7: at least 2 s of
// (timestamp, aircraft state, gimbal attitude) at 50 Hz or better).
#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
#include <vector>

namespace droppoint::util {

template <typename T>
class RingBuffer {
public:
    explicit RingBuffer(std::size_t capacity) : capacity_(capacity), data_(capacity) {}

    void Push(const T& item) {
        data_[next_write_] = item;
        next_write_ = (next_write_ + 1) % capacity_;
        if (size_ < capacity_) ++size_;
    }

    std::size_t Size() const { return size_; }
    std::size_t Capacity() const { return capacity_; }
    bool Empty() const { return size_ == 0; }

    // Index 0 is the most recently pushed item, increasing index goes
    // further back in time.
    const T& At(std::size_t age_index) const {
        const std::size_t idx = (next_write_ + capacity_ - 1 - age_index) % capacity_;
        return data_[idx];
    }

    // Finds the two samples bracketing `timestamp_us` (via `get_timestamp`)
    // and returns them in chronological order, for linear interpolation.
    // Returns nullopt if there are fewer than 2 samples or the timestamp is
    // outside the buffered range.
    template <typename GetTimestampFn>
    std::optional<std::pair<T, T>> FindBracket(std::uint64_t timestamp_us, GetTimestampFn get_timestamp) const {
        if (size_ < 2) return std::nullopt;
        for (std::size_t i = 0; i + 1 < size_; ++i) {
            const T& newer = At(i);
            const T& older = At(i + 1);
            const std::uint64_t t_newer = get_timestamp(newer);
            const std::uint64_t t_older = get_timestamp(older);
            if (timestamp_us <= t_newer && timestamp_us >= t_older) {
                return std::make_pair(older, newer);
            }
        }
        return std::nullopt;
    }

private:
    std::size_t capacity_;
    std::vector<T> data_;
    std::size_t next_write_ = 0;
    std::size_t size_ = 0;
};

}  // namespace droppoint::util
