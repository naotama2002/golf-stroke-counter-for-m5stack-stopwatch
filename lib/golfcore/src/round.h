// PixelGolf のラウンド状態（Apple Watch 版 RoundStore.swift の移植）
// ハードウェア非依存。PC 上の `pio test -e native` でテストできる。
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace golf {

constexpr int kHoleCount = 18;
constexpr int kMaxStrokes = 19;

enum class Phase : uint8_t { Start = 0, Playing = 1, Finished = 2 };

struct Hole {
    uint8_t strokes = 0;
    bool done = false;
};

enum class Mood : uint8_t { Idle, Happy, Normal, Sweat, Dizzy };

// パーが分からないので打数そのもので表情を変える
Mood moodFor(int strokes);

class Round {
public:
    Round() { reset(); }

    // --- プレー操作 ---
    void start();
    bool plus();     // 変化したら true
    bool minus();
    // ホールアウト: 打数 0 なら何もしない。18 番なら Finished。
    bool holeOut();
    // 終わったホールの打数を直す（1〜19）
    bool fix(int index, int strokes);
    // メニューの END ROUND
    void finish();
    // TOP に戻って新しいラウンド
    void reset();

    // --- 参照 ---
    Phase phase() const { return phase_; }
    int current() const { return current_; }
    const Hole& hole(int i) const { return holes_[i]; }
    const Hole& currentHole() const { return holes_[current_]; }
    // ホールアウト済みのみ合計（[from, to)）
    int total(int from = 0, int to = kHoleCount) const;
    int playedCount() const;

    // --- 永続化（NVS に丸ごと書く固定長バイナリ） ---
    static constexpr size_t kBlobSize = 4 + 1 + 1 + kHoleCount;
    void serialize(uint8_t out[kBlobSize]) const;
    // 壊れていたら false を返して状態は変えない
    bool deserialize(const uint8_t* in, size_t len);

private:
    Hole holes_[kHoleCount];
    int current_ = 0;
    Phase phase_ = Phase::Start;
};

}  // namespace golf
