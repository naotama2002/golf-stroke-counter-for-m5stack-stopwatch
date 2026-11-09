// PC 上で動くロジックテスト: pio test -e native
#include <unity.h>
#include "round.h"
#include "power_policy.h"

using namespace golf;

void setUp() {}
void tearDown() {}

static void play(Round& r, int strokes) {
    for (int i = 0; i < strokes; i++) r.plus();
    r.holeOut();
}

void test_initial_state() {
    Round r;
    TEST_ASSERT_EQUAL(Phase::Start, r.phase());
    TEST_ASSERT_EQUAL(0, r.current());
    TEST_ASSERT_EQUAL(0, r.total());
    TEST_ASSERT_FALSE(r.plus());  // START 中は数えない
}

void test_plus_minus_bounds() {
    Round r;
    r.start();
    TEST_ASSERT_FALSE(r.minus());  // 0 未満にならない
    for (int i = 0; i < 30; i++) r.plus();
    TEST_ASSERT_EQUAL(kMaxStrokes, r.currentHole().strokes);
    TEST_ASSERT_TRUE(r.minus());
    TEST_ASSERT_EQUAL(kMaxStrokes - 1, r.currentHole().strokes);
}

void test_holeout_requires_stroke() {
    Round r;
    r.start();
    TEST_ASSERT_FALSE(r.holeOut());
    TEST_ASSERT_EQUAL(0, r.current());
    r.plus();
    TEST_ASSERT_TRUE(r.holeOut());
    TEST_ASSERT_EQUAL(1, r.current());
    TEST_ASSERT_EQUAL(1, r.total());
}

void test_full_round_auto_finish_and_out_in() {
    Round r;
    r.start();
    for (int i = 0; i < kHoleCount; i++) play(r, i < 9 ? 4 : 5);
    TEST_ASSERT_EQUAL(Phase::Finished, r.phase());
    TEST_ASSERT_EQUAL(18, r.playedCount());
    TEST_ASSERT_EQUAL(36, r.total(0, 9));
    TEST_ASSERT_EQUAL(45, r.total(9, 18));
    TEST_ASSERT_EQUAL(81, r.total());
}

void test_fix_only_done_holes() {
    Round r;
    r.start();
    play(r, 5);
    TEST_ASSERT_FALSE(r.fix(1, 3));  // 未プレー
    TEST_ASSERT_TRUE(r.fix(0, 3));
    TEST_ASSERT_EQUAL(3, r.total());
    r.fix(0, 0);
    TEST_ASSERT_EQUAL(1, r.hole(0).strokes);  // 1 未満は 1
    r.fix(0, 99);
    TEST_ASSERT_EQUAL(kMaxStrokes, r.hole(0).strokes);
}

void test_finish_without_play_goes_back_to_start() {
    Round r;
    r.start();
    r.finish();
    TEST_ASSERT_EQUAL(Phase::Start, r.phase());
    r.start();
    play(r, 3);
    r.finish();
    TEST_ASSERT_EQUAL(Phase::Finished, r.phase());
}

void test_serialize_roundtrip() {
    Round a;
    a.start();
    play(a, 4);
    play(a, 7);
    a.plus();
    a.plus();
    uint8_t buf[Round::kBlobSize];
    a.serialize(buf);

    Round b;
    TEST_ASSERT_TRUE(b.deserialize(buf, sizeof(buf)));
    TEST_ASSERT_EQUAL(Phase::Playing, b.phase());
    TEST_ASSERT_EQUAL(2, b.current());
    TEST_ASSERT_EQUAL(2, b.currentHole().strokes);
    TEST_ASSERT_EQUAL(11, b.total());
    TEST_ASSERT_TRUE(b.hole(1).done);
}

void test_deserialize_rejects_garbage() {
    Round r;
    r.start();
    r.plus();
    uint8_t buf[Round::kBlobSize];
    r.serialize(buf);

    uint8_t bad[Round::kBlobSize];
    for (size_t i = 0; i < sizeof(bad); i++) bad[i] = buf[i];
    bad[0] = 'X';
    TEST_ASSERT_FALSE(r.deserialize(bad, sizeof(bad)));
    for (size_t i = 0; i < sizeof(bad); i++) bad[i] = buf[i];
    bad[5] = 18;  // current 範囲外
    TEST_ASSERT_FALSE(r.deserialize(bad, sizeof(bad)));
    TEST_ASSERT_FALSE(r.deserialize(buf, sizeof(buf) - 1));
    // 失敗しても状態は壊れない
    TEST_ASSERT_EQUAL(1, r.currentHole().strokes);
}

void test_mood() {
    TEST_ASSERT_EQUAL(Mood::Idle, moodFor(0));
    TEST_ASSERT_EQUAL(Mood::Happy, moodFor(3));
    TEST_ASSERT_EQUAL(Mood::Normal, moodFor(5));
    TEST_ASSERT_EQUAL(Mood::Sweat, moodFor(7));
    TEST_ASSERT_EQUAL(Mood::Dizzy, moodFor(8));
}

void test_power_policy() {
    PowerPolicy p({1000, 3000});
    p.onInput(0);
    TEST_ASSERT_FALSE(p.tick(500));
    TEST_ASSERT_EQUAL(Screen::Active, p.screen());
    TEST_ASSERT_TRUE(p.tick(1000));
    TEST_ASSERT_EQUAL(Screen::Dim, p.screen());
    // DIM 中の操作はそのまま有効
    TEST_ASSERT_TRUE(p.onInput(1500));
    TEST_ASSERT_EQUAL(Screen::Active, p.screen());
    p.tick(4500);
    TEST_ASSERT_EQUAL(Screen::Off, p.screen());
    // OFF から起こした操作は「起こすだけ」
    TEST_ASSERT_FALSE(p.onInput(9000));
    TEST_ASSERT_EQUAL(Screen::Active, p.screen());
    TEST_ASSERT_TRUE(p.onInput(9100));
}

// 回帰テスト: 入力時刻が tick の now より 1ms 新しくても消灯しない
void test_power_policy_input_newer_than_now() {
    PowerPolicy p({1000, 3000});
    p.onInput(122886);
    TEST_ASSERT_FALSE(p.tick(122885));
    TEST_ASSERT_EQUAL(Screen::Active, p.screen());
}

static void assertSameAfterReload(const Round& a) {
    uint8_t buf[Round::kBlobSize];
    a.serialize(buf);
    Round b;
    TEST_ASSERT_TRUE(b.deserialize(buf, sizeof(buf)));
    TEST_ASSERT_EQUAL(a.phase(), b.phase());
    TEST_ASSERT_EQUAL(a.current(), b.current());
    for (int i = 0; i < kHoleCount; i++) {
        TEST_ASSERT_EQUAL(a.hole(i).strokes, b.hole(i).strokes);
        TEST_ASSERT_EQUAL(a.hole(i).done, b.hole(i).done);
    }
}

// どの状態で電源を切っても、読み込み直せば同じ状態に戻る
void test_reload_from_every_state() {
    Round r;
    assertSameAfterReload(r);                  // START 画面
    r.start();
    assertSameAfterReload(r);                  // 1 番 0 打
    for (int h = 0; h < kHoleCount; h++) {
        for (int k = 0; k < (h % 5) + 1; k++) {
            r.plus();
            assertSameAfterReload(r);          // ホール途中
        }
        r.minus();
        r.plus();
        assertSameAfterReload(r);
        r.holeOut();
        assertSameAfterReload(r);              // ホールアウト直後（9 番後の休憩を含む）
    }
    TEST_ASSERT_EQUAL(Phase::Finished, r.phase());
    r.fix(3, 9);
    assertSameAfterReload(r);                  // 結果画面で修正後

    Round m;                                   // 途中で END ROUND
    m.start();
    play(m, 4);
    m.finish();
    assertSameAfterReload(m);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_initial_state);
    RUN_TEST(test_plus_minus_bounds);
    RUN_TEST(test_holeout_requires_stroke);
    RUN_TEST(test_full_round_auto_finish_and_out_in);
    RUN_TEST(test_fix_only_done_holes);
    RUN_TEST(test_finish_without_play_goes_back_to_start);
    RUN_TEST(test_serialize_roundtrip);
    RUN_TEST(test_deserialize_rejects_garbage);
    RUN_TEST(test_mood);
    RUN_TEST(test_power_policy);
    RUN_TEST(test_power_policy_input_newer_than_now);
    RUN_TEST(test_reload_from_every_state);
    return UNITY_END();
}
