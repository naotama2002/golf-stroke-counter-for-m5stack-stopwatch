// PixelGolf for M5Stack StopWatch — 試作ファーム
//
// 操作
//   画面タップ   : 大きな [+] / 小さな [-] / [HOLE OUT] / スコアのマス
//   横フリック   : HOLE 画面 <-> SCORE 画面
//   BtnA クリック : +1 打       BtnA 長押し : HOLE OUT
//   BtnB クリック : -1 打       BtnB 3 秒長押し : 今すぐ電源 OFF（状態は保存済み）
//
// 省電力
//   無操作 6 秒で減光 → 12 秒で画面 OFF（ESP32 も 80MHz に落とす）
//   自動電源 OFF はしない。電源 OFF は BtnB 3 秒長押しのみ。電源ボタンで起動すると続きから再開
//   画面 OFF 中の最初の操作は「起こすだけ」（誤カウント防止）
#include <M5Unified.h>
#include <M5PM1.h>
#include <Preferences.h>

#include "pixel_font.h"
#include "power_policy.h"
#include "round.h"
#include "sprites.h"
#include "text.h"

using namespace golf;

// ---------------------------------------------------------------- 設定
namespace cfg {
#ifdef DIAG_ALWAYS_ON
constexpr uint32_t kDimAfterMs = 0xFFFFFFF0UL;
constexpr uint32_t kScreenOffAfterMs = 0xFFFFFFF0UL;
#else
constexpr uint32_t kDimAfterMs = 6000;
constexpr uint32_t kScreenOffAfterMs = 12000;
#endif
constexpr uint32_t kPowerOffHoldMs = 3000;
constexpr uint8_t kBrightActive = 160;
constexpr uint8_t kBrightDim = 24;
constexpr uint32_t kToastMs = 1400;
}  // namespace cfg

// ---------------------------------------------------------------- 色（Theme.swift と同じ）
static uint16_t C_PAPER, C_INK, C_LIME, C_ONLIME, C_GRAY, C_BALL;

static void initColors() {
    C_PAPER = lgfx::color565(15, 18, 15);
    C_INK = lgfx::color565(242, 240, 224);
    C_LIME = lgfx::color565(184, 245, 77);
    C_ONLIME = lgfx::color565(15, 18, 15);
    C_GRAY = lgfx::color565(128, 133, 120);
    C_BALL = lgfx::color565(255, 255, 255);
}

// ---------------------------------------------------------------- 状態
static Round round_;
static PowerPolicy power_({cfg::kDimAfterMs, cfg::kScreenOffAfterMs});
static Preferences prefs;
static M5PM1 pm1;
static bool pm1Ready = false;
static M5Canvas canvas(&M5.Display);

enum class Page : uint8_t { Hole, Score };
static Page page = Page::Hole;
static int fixIndex = -1;      // >=0 なら FIX 画面
static int fixValue = 0;
static bool confirmEnd = false;
static uint32_t toastUntil = 0;  // HOLE OUT 演出中
static int toastHole = 0, toastStrokes = 0, toastTotal = 0;
static uint32_t hopUntil = 0;    // ボールくんジャンプ
static uint32_t lastInputMs = 0;
static bool dirty = true;
static String diagLine;
static int W = 466, H = 466, CX = 233, CY = 233;

// ---------------------------------------------------------------- 永続化
static void save() {
    uint8_t buf[Round::kBlobSize];
    round_.serialize(buf);
    prefs.putBytes("round", buf, sizeof(buf));
}

static void load() {
    uint8_t buf[Round::kBlobSize];
    size_t n = prefs.getBytes("round", buf, sizeof(buf));
    if (!round_.deserialize(buf, n)) round_.reset();
}

// ---------------------------------------------------------------- フィードバック
static void buzz(uint8_t level, uint16_t ms) {
    M5.Power.setVibration(level);
    delay(ms);
    M5.Power.setVibration(0);
}
static void hapticClick() { buzz(120, 25); }
static void hapticDown() { buzz(80, 15); }
static void hapticSuccess() {
    buzz(160, 40);
    delay(60);
    buzz(160, 40);
}

// ---------------------------------------------------------------- 描画ヘルパ
struct Rect {
    int x, y, w, h;
    bool contains(int px, int py) const {
        constexpr int pad = 14;  // 指は太いので見た目より少し広く判定する
        return px >= x - pad && px < x + w + pad && py >= y - pad && py < y + h + pad;
    }
};

static void drawText(const char* s, int x, int y, int pixel, uint16_t color, bool bold = false) {
    int advance = 5 + (bold ? 1 : 0) + 1;
    for (int i = 0; s[i]; i++) {
        const Glyph* g = findGlyph(s[i]);
        if (!g) continue;
        int ox = x + i * advance * pixel;
        for (int r = 0; r < 7; r++) {
            for (int c = 0; c < 5; c++) {
                if (g->rows[r] & (1 << (4 - c))) {
                    canvas.fillRect(ox + c * pixel, y + r * pixel, (bold ? 2 : 1) * pixel, pixel, color);
                }
            }
        }
    }
}

static void drawTextC(const char* s, int cx, int y, int pixel, uint16_t color, bool bold = false) {
    drawText(s, cx - textWidth(s, pixel, bold) / 2, y, pixel, color, bold);
}

static uint16_t spriteColor(char c) {
    switch (c) {
        case '#': return C_INK;
        case 'w': return C_BALL;
        case 'k': return C_ONLIME;
        case 'g': return C_GRAY;
        case 'l': return C_LIME;
        default: return 0;
    }
}

static void drawBall(Mood mood, int x, int y, int pixel) {
    BallGrid g = ballSprite(mood);
    for (int r = 0; r < kBallH; r++)
        for (int c = 0; c < kBallW; c++)
            if (g.px[r][c] != '.') canvas.fillRect(x + c * pixel, y + r * pixel, pixel, pixel, spriteColor(g.px[r][c]));
}

static void drawFlag(int x, int y, int pixel) {
    for (int r = 0; r < kFlagH; r++)
        for (int c = 0; c < kFlagW; c++)
            if (kFlagSprite[r][c] != '.')
                canvas.fillRect(x + c * pixel, y + r * pixel, pixel, pixel, spriteColor(kFlagSprite[r][c]));
}

static void drawBox(const Rect& r, uint16_t fill, uint16_t border, int bw = 3) {
    canvas.fillRect(r.x, r.y, r.w, r.h, border);
    canvas.fillRect(r.x + bw, r.y + bw, r.w - bw * 2, r.h - bw * 2, fill);
}

// ---------------------------------------------------------------- 画面レイアウト（466x466 の円形）
// 円の中に収まるよう、要素は中央寄せで縦に積む
static Rect rStart, rPlus, rMinus, rHoleOut, rEnd, rTop, rFixMinus, rFixPlus, rFixOk, rYes, rNo;
static Rect rCells[kHoleCount];

static void layout() {
    W = M5.Display.width();
    H = M5.Display.height();
    CX = W / 2;
    CY = H / 2;
    // 466 基準のスケール
    auto s = [](int v) { return v * M5.Display.width() / 466; };
    rStart = {CX - s(150), s(300), s(300), s(70)};
    rPlus = {CX - s(170), s(180), s(340), s(120)};
    rMinus = {CX - s(150), s(312), s(100), s(80)};
    rHoleOut = {CX - s(40), s(312), s(190), s(80)};
    rFixMinus = {CX - s(170), s(160), s(90), s(110)};
    rFixPlus = {CX + s(80), s(160), s(90), s(110)};
    rFixOk = {CX - s(130), s(306), s(260), s(80)};
    rYes = {CX - s(160), s(250), s(150), s(70)};
    rNo = {CX + s(10), s(250), s(150), s(70)};
    rEnd = {CX - s(120), s(362), s(240), s(60)};
    rTop = {CX - s(110), s(362), s(220), s(60)};
    // スコア 6 列 x 3 行
    int cw = s(54), ch = s(50), gap = s(4);
    int gx = CX - (cw * 6 + gap * 5) / 2;
    int gy = s(200);
    for (int i = 0; i < kHoleCount; i++) {
        int col = i % 6, row = i / 6;
        rCells[i] = {gx + col * (cw + gap), gy + row * (ch + gap), cw, ch};
    }
}

static void drawScoreGrid(int highlight) {
    char buf[8];
    for (int i = 0; i < kHoleCount; i++) {
        const Hole& h = round_.hole(i);
        const Rect& r = rCells[i];
        drawBox(r, C_PAPER, i == highlight ? C_LIME : C_GRAY, i == highlight ? 3 : 1);
        snprintf(buf, sizeof(buf), "%d", i + 1);
        drawTextC(buf, r.x + r.w / 2, r.y + 5, 2, C_GRAY);
        if (h.done) snprintf(buf, sizeof(buf), "%d", h.strokes);
        else snprintf(buf, sizeof(buf), "-");
        drawTextC(buf, r.x + r.w / 2, r.y + 22, 3, C_INK);
    }
}

static void renderStart() {
    drawTextC("PIXEL", CX, 70, 6, C_INK, true);
    drawTextC("GOLF", CX, 130, 6, C_INK, true);
    int bounce = (millis() / 450) % 2 ? -12 : 0;
    drawBall(Mood::Happy, CX - 110, 205 + bounce, 5);
    drawFlag(CX + 40, 200, 5);
    canvas.fillRect(CX - 130, 272, 260, 4, C_INK);
    drawBox(rStart, C_LIME, C_INK);
    bool blink = (millis() / 500) % 2;
    if (!blink) drawText(">", rStart.x + 24, rStart.y + 21, 4, C_ONLIME, true);
    drawTextC("PLAY START", rStart.x + rStart.w / 2 + 14, rStart.y + 21, 4, C_ONLIME);
}

static void renderHole() {
    char buf[24];
    const Hole& h = round_.currentHole();
    snprintf(buf, sizeof(buf), "HOLE %d", round_.current() + 1);
    drawTextC(buf, CX, 40, 4, C_INK);
    snprintf(buf, sizeof(buf), "TOTAL %d", round_.total());
    drawTextC(buf, CX, 78, 3, C_GRAY);

    // ボールくん + 打数
    int hop = millis() < hopUntil ? -12 : 0;
    if (hop) {
        canvas.fillRect(CX - 165, 115, 25, 5, C_INK);
        canvas.fillRect(CX - 155, 130, 15, 5, C_INK);
        canvas.fillRect(CX - 160, 145, 20, 5, C_INK);
    }
    drawBall(moodFor(h.strokes), CX - 130, 104 + hop, 5);
    snprintf(buf, sizeof(buf), "%d", h.strokes);
    drawText(buf, CX + 20, 102, 10, C_INK, true);

    // 大きな +
    drawBox(rPlus, C_LIME, C_INK);
    drawTextC("+", rPlus.x + rPlus.w / 2, rPlus.y + (rPlus.h - textHeight(12)) / 2, 12, C_ONLIME, true);

    // − と HOLE OUT（0 打のときは薄く）
    bool enabled = h.strokes > 0;
    uint16_t inkC = enabled ? C_INK : C_GRAY;
    uint16_t limeC = enabled ? C_LIME : C_GRAY;
    drawBox(rMinus, C_PAPER, inkC, 2);
    drawTextC("-", rMinus.x + rMinus.w / 2, rMinus.y + (rMinus.h - textHeight(5)) / 2, 5, inkC, true);
    drawBox(rHoleOut, C_PAPER, limeC, 2);
    drawTextC("HOLE OUT", rHoleOut.x + rHoleOut.w / 2, rHoleOut.y + (rHoleOut.h - textHeight(3)) / 2, 3, limeC);

    // ページインジケータ
    canvas.fillCircle(CX - 8, 438, 4, C_INK);
    canvas.fillCircle(CX + 8, 438, 4, C_GRAY);
}

static void renderToast() {
    char buf[24];
    snprintf(buf, sizeof(buf), "HOLE %d", toastHole);
    drawTextC(buf, CX, 70, 4, C_INK);
    Rect clear = {CX - 120, 115, 240, 64};
    canvas.fillRect(clear.x, clear.y, clear.w, clear.h, C_LIME);
    drawTextC("CLEAR!", CX, clear.y + 11, 6, C_ONLIME, true);
    drawBall(moodFor(toastStrokes), CX - 8 * 7, 200, 7);
    snprintf(buf, sizeof(buf), "%d SHOTS", toastStrokes);
    drawTextC(buf, CX, 310, 4, C_INK);
    snprintf(buf, sizeof(buf), "TOTAL %d", toastTotal);
    drawTextC(buf, CX, 360, 4, C_GRAY);
}

static void renderScore() {
    char buf[24];
    snprintf(buf, sizeof(buf), "THRU %d", round_.playedCount());
    drawTextC(buf, CX, 40, 3, C_GRAY);
    snprintf(buf, sizeof(buf), "%d", round_.total());
    drawTextC(buf, CX, 72, 9, C_INK, true);
    snprintf(buf, sizeof(buf), "OUT %d   IN %d", round_.total(0, 9), round_.total(9, 18));
    drawTextC(buf, CX, 152, 3, C_INK);
    drawScoreGrid(round_.current());
    drawTextC("TAP TO FIX", CX, 343, 2, C_GRAY);
    drawBox(rEnd, C_PAPER, C_INK, 2);
    drawTextC("END ROUND", CX, rEnd.y + (rEnd.h - textHeight(3)) / 2, 3, C_INK);
    canvas.fillCircle(CX - 8, 438, 4, C_GRAY);
    canvas.fillCircle(CX + 8, 438, 4, C_INK);
}

static void renderFix() {
    char buf[24];
    snprintf(buf, sizeof(buf), "FIX HOLE %d", fixIndex + 1);
    drawTextC(buf, CX, 80, 4, C_INK);
    drawBox(rFixMinus, C_PAPER, C_INK, 2);
    drawTextC("-", rFixMinus.x + rFixMinus.w / 2, rFixMinus.y + 37, 6, C_INK, true);
    drawBox(rFixPlus, C_LIME, C_INK, 2);
    drawTextC("+", rFixPlus.x + rFixPlus.w / 2, rFixPlus.y + 37, 6, C_ONLIME, true);
    snprintf(buf, sizeof(buf), "%d", fixValue);
    drawTextC(buf, CX, 175, 11, C_INK, true);
    int orig = round_.hole(fixIndex).strokes;
    if (orig != fixValue) {
        snprintf(buf, sizeof(buf), "%d > %d", orig, fixValue);
        drawTextC(buf, CX, 285, 3, C_GRAY);
    }
    drawBox(rFixOk, C_PAPER, C_LIME, 2);
    drawTextC("OK", CX, rFixOk.y + (rFixOk.h - textHeight(4)) / 2, 4, C_LIME);
}

static void renderConfirm() {
    drawTextC("END ROUND?", CX, 150, 5, C_INK);
    drawBox(rYes, C_LIME, C_INK, 2);
    drawTextC("END", rYes.x + rYes.w / 2, rYes.y + 21, 4, C_ONLIME);
    drawBox(rNo, C_PAPER, C_INK, 2);
    drawTextC("BACK", rNo.x + rNo.w / 2, rNo.y + 21, 4, C_INK);
}

static void renderFinished() {
    char buf[24];
    Rect clear = {CX - 110, 40, 220, 56};
    canvas.fillRect(clear.x, clear.y, clear.w, clear.h, C_LIME);
    drawTextC("CLEAR!", CX, clear.y + 10, 5, C_ONLIME, true);
    snprintf(buf, sizeof(buf), "%d", round_.total());
    drawTextC(buf, CX, 108, 8, C_INK, true);
    snprintf(buf, sizeof(buf), "%d HOLES", round_.playedCount());
    drawTextC(buf, CX, 172, 2, C_GRAY);
    drawScoreGrid(-1);
    drawTextC("TAP TO FIX", CX, 343, 2, C_GRAY);
    drawBox(rTop, C_PAPER, C_INK, 2);
    drawTextC("TOP", CX, rTop.y + (rTop.h - textHeight(4)) / 2, 4, C_INK);
}

static void render() {
    canvas.fillScreen(C_PAPER);
    if (fixIndex >= 0) renderFix();
    else if (confirmEnd) renderConfirm();
    else if (millis() < toastUntil) renderToast();
    else {
        switch (round_.phase()) {
            case Phase::Start: renderStart(); break;
            case Phase::Playing: page == Page::Hole ? renderHole() : renderScore(); break;
            case Phase::Finished: renderFinished(); break;
        }
    }
    canvas.pushSprite(0, 0);
}

// ---------------------------------------------------------------- 操作
static void doPlus() {
    if (round_.plus()) {
        hopUntil = millis() + 300;
        save();
        hapticClick();
    }
}

static void doMinus() {
    if (round_.minus()) {
        save();
        hapticDown();
    }
}

static void doHoleOut() {
    const Hole& h = round_.currentHole();
    if (h.strokes == 0) return;
    toastHole = round_.current() + 1;
    toastStrokes = h.strokes;
    toastTotal = round_.total() + h.strokes;
    round_.holeOut();  // 先に確定・保存（演出中に電源が落ちても失わない）
    save();
    toastUntil = millis() + cfg::kToastMs;
    hapticSuccess();
}

static void openFix(int i) {
    if (!round_.hole(i).done) return;
    fixIndex = i;
    fixValue = round_.hole(i).strokes;
    hapticClick();
}

static void handleTap(int x, int y) {
    if (millis() < toastUntil) return;
    if (fixIndex >= 0) {
        if (rFixMinus.contains(x, y)) { if (fixValue > 1) fixValue--; hapticClick(); }
        else if (rFixPlus.contains(x, y)) { if (fixValue < kMaxStrokes) fixValue++; hapticClick(); }
        else if (rFixOk.contains(x, y)) { round_.fix(fixIndex, fixValue); save(); fixIndex = -1; hapticSuccess(); }
        return;
    }
    if (confirmEnd) {
        if (rYes.contains(x, y)) { round_.finish(); save(); confirmEnd = false; page = Page::Hole; hapticSuccess(); }
        else if (rNo.contains(x, y)) { confirmEnd = false; }
        return;
    }
    switch (round_.phase()) {
        case Phase::Start:
            if (rStart.contains(x, y)) { round_.start(); page = Page::Hole; save(); hapticSuccess(); }
            break;
        case Phase::Playing:
            if (page == Page::Hole) {
                if (rPlus.contains(x, y)) doPlus();
                else if (rMinus.contains(x, y)) doMinus();
                else if (rHoleOut.contains(x, y)) doHoleOut();
            } else {
                for (int i = 0; i < kHoleCount; i++) if (rCells[i].contains(x, y)) { openFix(i); return; }
                if (rEnd.contains(x, y)) confirmEnd = true;
            }
            break;
        case Phase::Finished:
            for (int i = 0; i < kHoleCount; i++) if (rCells[i].contains(x, y)) { openFix(i); return; }
            if (rTop.contains(x, y)) { round_.reset(); save(); }
            break;
    }
}

static void handleFlick(int dx) {
    if (round_.phase() != Phase::Playing || fixIndex >= 0 || confirmEnd) return;
    if (dx < 0 && page == Page::Hole) page = Page::Score;      // 右→左
    else if (dx > 0 && page == Page::Score) page = Page::Hole; // 左→右
}

// ---------------------------------------------------------------- 電源
static void applyScreen(Screen s) {
    switch (s) {
        case Screen::Active:
            M5.Display.setBrightness(cfg::kBrightActive);
            dirty = true;
            break;
        case Screen::Dim:
            M5.Display.setBrightness(cfg::kBrightDim);
            break;
        case Screen::Off:
            // パネルを sleep させるとタッチで起こせなくなることがあるので、輝度 0 + 描画停止にとどめる
            // （AMOLED は黒画素がほぼ電力ゼロ）
            M5.Display.setBrightness(0);
            break;
    }
}

static void shutdownNow() {
    save();
    // 何が起きたか分かるように表示してから切る
    M5.Display.setBrightness(cfg::kBrightActive);
    canvas.fillScreen(C_PAPER);
    drawTextC("POWER OFF", CX, CY - 40, 5, C_INK);
    drawTextC("PRESS POWER", CX, CY + 20, 3, C_GRAY);
    drawTextC("TO RESUME", CX, CY + 50, 3, C_GRAY);
    canvas.pushSprite(0, 0);
    delay(1500);
    prefs.end();
    M5.Display.sleep();
    Serial.println("[power] shutdown");
    Serial.flush();
    int rc = -999;
    if (pm1Ready) {
        rc = pm1.shutdown();  // L0 まで落ちる。電源ボタンで起動
    } else {
        M5.Power.powerOff();
    }
    Serial.printf("[power] shutdown rc=%d chg=%d -> still alive\n", rc, (int)M5.Power.isCharging());
    Serial.flush();
    // USB 給電中などで電源が切れなかった場合、止まったままにせず再起動して続きから再開する
    delay(2000);
    ESP.restart();
}

// 入力を PowerPolicy に通す。false なら「起こすだけ」で操作は捨てる
static bool acceptInput() {
    uint32_t now = millis();
    Serial.printf("[input] t=%lu screen=%d\n", (unsigned long)now, (int)power_.screen());
    lastInputMs = now;
    bool wasOff = power_.screen() == Screen::Off;
    bool wasDim = power_.screen() == Screen::Dim;
    bool pass = power_.onInput(now);
    if (wasOff || wasDim) applyScreen(Screen::Active);
    dirty = true;
    return pass;
}

// ---------------------------------------------------------------- 状態ダンプ / シリアル操作（動作確認用）
static void dumpState(const char* tag) {
    char holes[kHoleCount * 4 + 1];
    int n = 0;
    for (int i = 0; i < kHoleCount; i++) {
        const Hole& h = round_.hole(i);
        n += snprintf(holes + n, sizeof(holes) - n, "%d%s ", h.strokes, h.done ? "*" : "");
    }
    Serial.printf("[%s] phase=%d current=%d total=%d page=%d fix=%d confirm=%d holes=%s\n", tag, (int)round_.phase(),
                  round_.current() + 1, round_.total(), (int)page, fixIndex, confirmEnd ? 1 : 0, holes);
}

// USB シリアルから 1 文字コマンドで操作する（実機テストの自動化用。普段は使わない）
//   s:START +:+1 -:-1 o:HOLE OUT x:END ROUND n:TOP F:1番の修正画面を開く C:終了確認を開く d:状態表示 R:再起動
static void handleSerialCommand() {
    while (Serial.available()) {
        char c = Serial.read();
        switch (c) {
            case 's': round_.start(); save(); break;
            case '+': doPlus(); break;
            case '-': doMinus(); break;
            case 'o': doHoleOut(); break;
            case 'x': round_.finish(); save(); break;
            case 'n': round_.reset(); save(); break;
            case 'F': openFix(0); break;
            case 'C': confirmEnd = true; break;
            case 'd': dumpState("state"); break;
            case 'P': shutdownNow(); break;
            case 'R': Serial.println("[cmd] restart"); Serial.flush(); ESP.restart(); break;
            default: continue;
        }
        dirty = true;
    }
}

// ---------------------------------------------------------------- Arduino
void setup() {
    // 電源 OFF からの起動（電源投入リセット）直後は IO エキスパンダ(M5IOE1)の起動が間に合わず、
    // M5.begin() の画面電源 ON が失敗して「動いているのに画面が真っ黒」になる
    // （ログ: "M5IOE1 PWM ch1 could not be turned off"）。
    // 周辺チップに電源が入った状態で 1 回だけソフトリセットして、初期化をやり直す。
    if (esp_reset_reason() == ESP_RST_POWERON) {
        delay(300);
        ESP.restart();
    }
    auto c = M5.config();
    M5.begin(c);
    initColors();
#ifdef DIAG_ALWAYS_ON
    // 段階表示: 赤 = M5.begin 直後 / 緑 = PM1 初期化後 / その後アプリ
    M5.Display.setBrightness(200);
    M5.Display.fillScreen(TFT_RED);
    M5.Display.setTextSize(4);
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.drawString("1 RED", 150, 210);
    delay(3000);
#endif

    if (pm1.begin(&M5.In_I2C, M5PM1_DEFAULT_ADDR, M5PM1_I2C_FREQ_100K) == M5PM1_OK) {
        pm1Ready = true;
        pm1.setSingleResetDisable(false);
    }
#ifdef DIAG_ALWAYS_ON
    M5.Display.fillScreen(TFT_GREEN);
    M5.Display.drawString("2 GREEN", 130, 210);
    delay(3000);
#endif

    prefs.begin("pixelgolf", false);
    load();

    layout();
    canvas.setColorDepth(16);
    canvas.setPsram(true);
    bool spriteOk = canvas.createSprite(W, H) != nullptr;

    Serial.begin(115200);
    Serial.printf("[boot] reset_reason=%d\n", (int)esp_reset_reason());
    dumpState("boot");
    diagLine = String("[diag] board=") + (int)M5.getBoard() + " disp=" + W + "x" + H +
               " psram=" + ESP.getPsramSize() + " free_psram=" + ESP.getFreePsram() +
               " sprite=" + (spriteOk ? "ok" : "FAIL") + " pm1=" + (pm1Ready ? "ok" : "FAIL") +
               " phase=" + (int)round_.phase();

    M5.Display.setBrightness(cfg::kBrightActive);
    lastInputMs = millis();
    power_.onInput(lastInputMs);
    render();
}

void loop() {
    M5.update();
    uint32_t now = millis();
#ifdef DIAG_ALWAYS_ON
    static uint32_t lastDiag = 0;
    if (now - lastDiag > 2000) {
        lastDiag = now;
        Serial.printf("%s screen=%d bright=%d t=%lu\n", diagLine.c_str(), (int)power_.screen(),
                      M5.Display.getBrightness(), (unsigned long)now);
    }
#endif

    {
        static uint32_t lastHb = 0;
        if (now - lastHb > 5000) {
            lastHb = now;
            Serial.printf("[hb] t=%lu phase=%d screen=%d btnA=%d btnB=%d touch=%d chg=%d\n", (unsigned long)now,
                          (int)round_.phase(), (int)power_.screen(), M5.BtnA.isPressed(), M5.BtnB.isPressed(),
                          M5.Touch.getCount(), (int)M5.Power.isCharging());
        }
        if (M5.BtnA.wasPressed()) Serial.println("[btn] A down");
        if (M5.BtnB.wasPressed()) Serial.println("[btn] B down");
    }

    handleSerialCommand();

    // --- 物理ボタン ---
    if (M5.BtnA.wasClicked() && acceptInput()) {
        if (round_.phase() == Phase::Start) { round_.start(); save(); hapticSuccess(); }
        else if (round_.phase() == Phase::Playing && fixIndex < 0 && !confirmEnd && now >= toastUntil) {
            page = Page::Hole;
            doPlus();
        }
    }
    if (M5.BtnA.wasHold() && acceptInput()) {
        if (round_.phase() == Phase::Playing && fixIndex < 0 && !confirmEnd) {
            page = Page::Hole;
            doHoleOut();
        }
    }
    if (M5.BtnB.wasClicked() && acceptInput()) {
        if (round_.phase() == Phase::Playing && fixIndex < 0 && !confirmEnd && now >= toastUntil) {
            page = Page::Hole;
            doMinus();
        }
    }
    // 電源 OFF は BtnB を 3 秒押し続けたときだけ（0.5 秒の長押しで誤って切れていた）
    if (M5.BtnB.pressedFor(cfg::kPowerOffHoldMs)) shutdownNow();

    // --- タッチ ---
    // M5Unified の wasClicked() は 8px 動くとフリック扱い / 0.5 秒超で長押し扱いになり、
    // 押しにくい原因だったので、押した位置と離した位置から自前で判定する
    {
        static int pressX = -1, pressY = -1;
        static bool wakeOnly = false;
        auto t = M5.Touch.getDetail();
        if (t.wasPressed()) {
            pressX = t.x;
            pressY = t.y;
            wakeOnly = power_.screen() == Screen::Off;
            if (wakeOnly) acceptInput();  // 消灯中のタッチは起こすだけ
            Serial.printf("[touch] down x=%d y=%d%s\n", pressX, pressY, wakeOnly ? " (wake)" : "");
        } else if (t.wasReleased() && pressX >= 0) {
            int dx = t.x - pressX, dy = t.y - pressY;
            Serial.printf("[touch] up   x=%d y=%d dx=%d dy=%d\n", t.x, t.y, dx, dy);
            if (!wakeOnly && acceptInput()) {
                if (abs(dx) > 70 && abs(dx) > abs(dy) * 2) handleFlick(dx);
                else if (abs(dx) < 50 && abs(dy) < 50) handleTap(pressX, pressY);
            }
            pressX = pressY = -1;
            wakeOnly = false;
        }
    }

    // --- 省電力 ---
    // 入力処理で lastInputMs が更新されているので、ここで now を取り直す（古い now だと uint32 がアンダーフローして即電源 OFF していた）
    now = millis();
    if (power_.tick(now)) {
        Serial.printf("[power] screen=%d t=%lu\n", (int)power_.screen(), (unsigned long)now);
        applyScreen(power_.screen());
    }

    // --- 描画 ---
    bool animating = now < toastUntil + 50 || now < hopUntil + 50 || round_.phase() == Phase::Start;
    if (power_.screen() != Screen::Off && (dirty || animating)) {
        render();
        dirty = false;
    }

    // 画面 OFF 中はポーリングを間引く（ボタンは押下時間が十分長いので 50ms で拾える）
    delay(power_.screen() == Screen::Off ? 50 : 10);
}
