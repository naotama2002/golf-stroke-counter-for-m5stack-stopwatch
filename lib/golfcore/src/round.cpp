#include "round.h"

namespace golf {

namespace {
constexpr uint8_t kMagic[3] = {'P', 'G', 'F'};
constexpr uint8_t kVersion = 1;
constexpr uint8_t kDoneBit = 0x80;
}  // namespace

Mood moodFor(int strokes) {
    if (strokes <= 0) return Mood::Idle;
    if (strokes <= 3) return Mood::Happy;
    if (strokes <= 5) return Mood::Normal;
    if (strokes <= 7) return Mood::Sweat;
    return Mood::Dizzy;
}

void Round::start() { phase_ = Phase::Playing; }

bool Round::plus() {
    if (phase_ != Phase::Playing) return false;
    Hole& h = holes_[current_];
    if (h.strokes >= kMaxStrokes) return false;
    h.strokes++;
    return true;
}

bool Round::minus() {
    if (phase_ != Phase::Playing) return false;
    Hole& h = holes_[current_];
    if (h.strokes == 0) return false;
    h.strokes--;
    return true;
}

bool Round::holeOut() {
    if (phase_ != Phase::Playing) return false;
    Hole& h = holes_[current_];
    if (h.strokes == 0) return false;
    h.done = true;
    if (current_ >= kHoleCount - 1) {
        phase_ = Phase::Finished;
    } else {
        current_++;
    }
    return true;
}

bool Round::fix(int index, int strokes) {
    if (index < 0 || index >= kHoleCount || !holes_[index].done) return false;
    if (strokes < 1) strokes = 1;
    if (strokes > kMaxStrokes) strokes = kMaxStrokes;
    holes_[index].strokes = static_cast<uint8_t>(strokes);
    return true;
}

void Round::finish() { phase_ = playedCount() > 0 ? Phase::Finished : Phase::Start; }

void Round::reset() {
    for (auto& h : holes_) h = Hole{};
    current_ = 0;
    phase_ = Phase::Start;
}

int Round::total(int from, int to) const {
    if (from < 0) from = 0;
    if (to > kHoleCount) to = kHoleCount;
    int sum = 0;
    for (int i = from; i < to; i++) {
        if (holes_[i].done) sum += holes_[i].strokes;
    }
    return sum;
}

int Round::playedCount() const {
    int n = 0;
    for (const auto& h : holes_) n += h.done ? 1 : 0;
    return n;
}

void Round::serialize(uint8_t out[kBlobSize]) const {
    out[0] = kMagic[0];
    out[1] = kMagic[1];
    out[2] = kMagic[2];
    out[3] = kVersion;
    out[4] = static_cast<uint8_t>(phase_);
    out[5] = static_cast<uint8_t>(current_);
    for (int i = 0; i < kHoleCount; i++) {
        out[6 + i] = holes_[i].strokes | (holes_[i].done ? kDoneBit : 0);
    }
}

bool Round::deserialize(const uint8_t* in, size_t len) {
    if (in == nullptr || len != kBlobSize) return false;
    if (in[0] != kMagic[0] || in[1] != kMagic[1] || in[2] != kMagic[2] || in[3] != kVersion) return false;
    if (in[4] > static_cast<uint8_t>(Phase::Finished)) return false;
    if (in[5] >= kHoleCount) return false;
    for (int i = 0; i < kHoleCount; i++) {
        if ((in[6 + i] & ~kDoneBit) > kMaxStrokes) return false;
    }
    phase_ = static_cast<Phase>(in[4]);
    current_ = in[5];
    for (int i = 0; i < kHoleCount; i++) {
        holes_[i].strokes = in[6 + i] & ~kDoneBit;
        holes_[i].done = (in[6 + i] & kDoneBit) != 0;
    }
    return true;
}

}  // namespace golf
