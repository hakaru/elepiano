#pragma once
#include <array>
#include <atomic>
#include <optional>
#include <cstddef>

/// Single-producer single-consumer ロックフリーキュー
/// N は 2 のべき乗であること (例: 64, 128)
/// - push(): プロデューサースレッドのみ呼ぶ
/// - pop():  コンシューマースレッドのみ呼ぶ
template<typename T, std::size_t N>
class SpscQueue {
    static_assert(N >= 2 && (N & (N - 1)) == 0,
                  "SpscQueue: N must be a power of 2 and >= 2");
public:
    /// プロデューサースレッドから呼ぶ。満杯なら false を返す。
    bool push(const T& val) noexcept {
        const std::size_t h    = head_.load(std::memory_order_relaxed);
        const std::size_t next = (h + 1) & kMask;
        if (next == tail_.load(std::memory_order_acquire)) {
            return false;  // full
        }
        buf_[h] = val;
        head_.store(next, std::memory_order_release);
        return true;
    }

    /// コンシューマースレッドから呼ぶ。空なら std::nullopt を返す。
    std::optional<T> pop() noexcept {
        const std::size_t t = tail_.load(std::memory_order_relaxed);
        if (t == head_.load(std::memory_order_acquire)) {
            return std::nullopt;  // empty
        }
        T val = buf_[t];
        tail_.store((t + 1) & kMask, std::memory_order_release);
        return val;
    }

    bool empty() const noexcept {
        return tail_.load(std::memory_order_acquire) ==
               head_.load(std::memory_order_acquire);
    }

private:
    static constexpr std::size_t kMask = N - 1;

    std::array<T, N> buf_{};
    // 別キャッシュラインに配置してフォールスシェアリングを防ぐ
    alignas(64) std::atomic<std::size_t> head_{0};  // producer が更新
    alignas(64) std::atomic<std::size_t> tail_{0};  // consumer が更新
};
