// ドット文字の幅計算（描画系から共通で使う）
#pragma once
#include <string.h>

namespace golf {

// pixel = 1 ドットの大きさ, bold = 横に 1 ドット太らせる
inline int textWidth(const char* s, int pixel, bool bold = false) {
    int n = static_cast<int>(strlen(s));
    if (n == 0) return 0;
    int advance = 5 + (bold ? 1 : 0) + 1;
    return (n * advance - 1) * pixel;
}

inline int textHeight(int pixel) { return 7 * pixel; }

}  // namespace golf
