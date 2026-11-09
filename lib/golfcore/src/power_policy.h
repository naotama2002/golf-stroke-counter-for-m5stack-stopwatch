// 画面の省電力ステート（ハード非依存の純粋ロジック）
//
//   ACTIVE --(dimAfterMs 無操作)--> DIM --(offAfterMs 無操作)--> OFF
//     ^                                                          |
//     +-------------- 何か操作（ボタン / タッチ） ----------------+
//
// OFF から起こした最初の操作は「起こすだけ」で打数には数えない（誤カウント防止）。
#pragma once
#include <stdint.h>

namespace golf {

enum class Screen : uint8_t { Active, Dim, Off };

struct PowerConfig {
    uint32_t dimAfterMs;
    uint32_t offAfterMs;
    // ESP32 の GCC (C++11) でも {a, b} で初期化できるよう明示コンストラクタにする
    constexpr PowerConfig(uint32_t dim = 6000, uint32_t off = 12000) : dimAfterMs(dim), offAfterMs(off) {}
};

class PowerPolicy {
public:
    explicit PowerPolicy(PowerConfig cfg = {}) : cfg_(cfg) {}

    // 操作があったとき。戻り値: その操作をアプリに渡してよいか
    // （OFF から起こした操作は false = 起こすだけ）
    bool onInput(uint32_t nowMs) {
        bool wasOff = screen_ == Screen::Off;
        lastInputMs_ = nowMs;
        screen_ = Screen::Active;
        return !wasOff;
    }

    // 毎ループ呼ぶ。状態が変わったら true
    bool tick(uint32_t nowMs) {
        // 入力処理の中で lastInputMs_ が nowMs より新しくなることがある。
        // そのまま引くと uint32 がアンダーフローして「巨大な無操作時間」になるので 0 扱いにする
        uint32_t idle = static_cast<int32_t>(nowMs - lastInputMs_) > 0 ? nowMs - lastInputMs_ : 0;
        Screen next = Screen::Active;
        if (idle >= cfg_.offAfterMs) next = Screen::Off;
        else if (idle >= cfg_.dimAfterMs) next = Screen::Dim;
        // 一度 OFF になったら入力が来るまで OFF のまま
        if (screen_ == Screen::Off) next = Screen::Off;
        bool changed = next != screen_;
        screen_ = next;
        return changed;
    }

    Screen screen() const { return screen_; }
    const PowerConfig& config() const { return cfg_; }

private:
    PowerConfig cfg_;
    Screen screen_ = Screen::Active;
    uint32_t lastInputMs_ = 0;
};

}  // namespace golf
