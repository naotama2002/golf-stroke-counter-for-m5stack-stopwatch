// ドット絵スプライト（Sprites.swift の移植）
// 文字: 'w' 白(体) / 'k' 暗(顔・線) / 'g' グレー / 'l' ライム / '#' インク / '.' 透明
#pragma once
#include "round.h"

namespace golf {

constexpr int kBallW = 16;
constexpr int kBallH = 13;

struct BallGrid {
    char px[kBallH][kBallW];
};

inline BallGrid ballSprite(Mood mood) {
    static const char* base[kBallH] = {
        ".....######.....",
        "...##......##...",
        "..#..g.......#..",
        ".#.......g....#.",
        ".#............#.",
        "#..............#",
        "#..............#",
        "#..g...........#",
        ".#..........g.#.",
        ".#............#.",
        "..#....g.....#..",
        "...##......##...",
        ".....######.....",
    };
    BallGrid g;
    for (int r = 0; r < kBallH; r++) {
        int a = -1, b = -1;
        for (int c = 0; c < kBallW; c++) {
            g.px[r][c] = base[r][c];
            if (base[r][c] == '#') {
                if (a < 0) a = c;
                b = c;
            }
        }
        // 輪郭の内側を白で塗る（黒背景でもボールに見えるように）
        if (a >= 0 && a < b) {
            for (int c = a + 1; c < b; c++) {
                if (g.px[r][c] == '.') g.px[r][c] = 'w';
            }
        }
    }
    auto set = [&](int r, int c, char v = '#') { g.px[r][c] = v; };
    struct P { int r, c; };
    auto setAll = [&](const P* ps, int n) {
        for (int i = 0; i < n; i++) set(ps[i].r, ps[i].c);
    };

    switch (mood) {
        case Mood::Idle:
        case Mood::Normal: {
            static const P eyes[] = {{5, 5}, {6, 5}, {5, 10}, {6, 10}};
            setAll(eyes, 4);
            if (mood == Mood::Normal) {
                static const P mouth[] = {{8, 6}, {9, 7}, {9, 8}, {8, 9}};
                setAll(mouth, 4);
            } else {
                static const P mouth[] = {{9, 7}, {9, 8}};
                setAll(mouth, 2);
            }
            break;
        }
        case Mood::Happy: {
            static const P eyes[] = {{5, 5}, {6, 4}, {6, 6}, {5, 10}, {6, 9}, {6, 11}};
            static const P mouth[] = {{8, 5}, {8, 6}, {8, 7}, {8, 8}, {8, 9},
                                      {8, 10}, {9, 6}, {9, 7}, {9, 8}, {9, 9}};
            setAll(eyes, 6);
            setAll(mouth, 10);
            set(7, 3, 'l');
            set(7, 12, 'l');
            break;
        }
        case Mood::Sweat: {
            static const P eyes[] = {{5, 5}, {6, 5}, {5, 10}, {6, 10}};
            static const P mouth[] = {{9, 6}, {8, 7}, {8, 8}, {9, 9}};
            setAll(eyes, 4);
            setAll(mouth, 4);
            set(2, 12, 'l');
            set(3, 12, 'l');
            set(3, 13, 'l');
            break;
        }
        case Mood::Dizzy: {
            static const P eyes[] = {{4, 4}, {4, 6}, {5, 5}, {6, 4}, {6, 6},
                                     {4, 9}, {4, 11}, {5, 10}, {6, 9}, {6, 11}};
            static const P mouth[] = {{9, 5}, {8, 6}, {9, 7}, {8, 8}, {9, 9}, {8, 10}};
            setAll(eyes, 10);
            setAll(mouth, 6);
            break;
        }
    }
    // ボールの線・顔は暗い色（白い体の上で見えるように）
    for (int r = 0; r < kBallH; r++)
        for (int c = 0; c < kBallW; c++)
            if (g.px[r][c] == '#') g.px[r][c] = 'k';
    return g;
}

constexpr int kFlagW = 8;
constexpr int kFlagH = 14;
static const char* const kFlagSprite[kFlagH] = {
    "#.......", "#lll....", "#lllll..", "#lllllll", "#lllll..", "#lll....", "#.......",
    "#.......", "#.......", "#.......", "#.......", "#.......", "#.......", "###.....",
};

}  // namespace golf
