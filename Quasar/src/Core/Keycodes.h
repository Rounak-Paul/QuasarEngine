#pragma once

namespace Quasar {
    typedef enum MouseCode {
        // From glfw3.h
        QS_MBTN_0                = 0,
        QS_MBTN_1                = 1,
        QS_MBTN_2                = 2,
        QS_MBTN_3                = 3,
        QS_MBTN_4                = 4,
        QS_MBTN_5                = 5,
        QS_MBTN_6                = 6,
        QS_MBTN_7                = 7,

        QS_MBTN_LAST             = QS_MBTN_7,
        QS_MBTN_LEFT             = QS_MBTN_0,
        QS_MBTN_RIGHT            = QS_MBTN_1,
        QS_MBTN_MIDDLE           = QS_MBTN_2,
        QS_MBTN_MAX
    } buttons;

    #define DEFINE_KEY(name, code) QS_KEY_##name = code

    typedef enum KeyCode {
        // From glfw3.h
        DEFINE_KEY(SPACE               , 32),
        DEFINE_KEY(APOSTROPHE          , 39), /* ' */
        DEFINE_KEY(COMMA               , 44), /* , */
        DEFINE_KEY(MINUS               , 45), /* - */
        DEFINE_KEY(PERIOD              , 46), /* . */
        DEFINE_KEY(SLASH               , 47), /* / */

        DEFINE_KEY(D0                  , 48), /* 0 */
        DEFINE_KEY(D1                  , 49), /* 1 */
        DEFINE_KEY(D2                  , 50), /* 2 */
        DEFINE_KEY(D3                  , 51), /* 3 */
        DEFINE_KEY(D4                  , 52), /* 4 */
        DEFINE_KEY(D5                  , 53), /* 5 */
        DEFINE_KEY(D6                  , 54), /* 6 */
        DEFINE_KEY(D7                  , 55), /* 7 */
        DEFINE_KEY(D8                  , 56), /* 8 */
        DEFINE_KEY(D9                  , 57), /* 9 */

        DEFINE_KEY(SEMICOLON           , 59), /* ; */
        DEFINE_KEY(EQUAL               , 61), /* = */

        DEFINE_KEY(A                   , 65),
        DEFINE_KEY(B                   , 66),
        DEFINE_KEY(C                   , 67),
        DEFINE_KEY(D                   , 68),
        DEFINE_KEY(E                   , 69),
        DEFINE_KEY(F                   , 70),
        DEFINE_KEY(G                   , 71),
        DEFINE_KEY(H                   , 72),
        DEFINE_KEY(I                   , 73),
        DEFINE_KEY(J                   , 74),
        DEFINE_KEY(K                   , 75),
        DEFINE_KEY(L                   , 76),
        DEFINE_KEY(M                   , 77),
        DEFINE_KEY(N                   , 78),
        DEFINE_KEY(O                   , 79),
        DEFINE_KEY(P                   , 80),
        DEFINE_KEY(Q                   , 81),
        DEFINE_KEY(R                   , 82),
        DEFINE_KEY(S                   , 83),
        DEFINE_KEY(T                   , 84),
        DEFINE_KEY(U                   , 85),
        DEFINE_KEY(V                   , 86),
        DEFINE_KEY(W                   , 87),
        DEFINE_KEY(X                   , 88),
        DEFINE_KEY(Y                   , 89),
        DEFINE_KEY(Z                   , 90),

        DEFINE_KEY(LEFTBRACKET         , 91),  /* [ */
        DEFINE_KEY(BACKSLASH           , 92),  /* \ */
        DEFINE_KEY(RIGHTBRACKET        , 93),  /* ] */
        DEFINE_KEY(GRAVEACCENT         , 96),  /* ` */

        DEFINE_KEY(WORLD1              , 161), /* non-US #1 */
        DEFINE_KEY(WORLD2              , 162), /* non-US #2 */

        /* Function keys */
        DEFINE_KEY(ESCAPE              , 256),
        DEFINE_KEY(ENTER               , 257),
        DEFINE_KEY(TAB                 , 258),
        DEFINE_KEY(BACKSPACE           , 259),
        DEFINE_KEY(INSERT              , 260),
        DEFINE_KEY(DELETE              , 261),
        DEFINE_KEY(RIGHT               , 262),
        DEFINE_KEY(LEFT                , 263),
        DEFINE_KEY(DOWN                , 264),
        DEFINE_KEY(UP                  , 265),
        DEFINE_KEY(PAGEUP              , 266),
        DEFINE_KEY(PAGEDOWN            , 267),
        DEFINE_KEY(HOME                , 268),
        DEFINE_KEY(END                 , 269),
        DEFINE_KEY(CAPSLOCK            , 280),
        DEFINE_KEY(SCROLLLOCK          , 281),
        DEFINE_KEY(NUMLOCK             , 282),
        DEFINE_KEY(PRINTSCREEN         , 283),
        DEFINE_KEY(PAUSE               , 284),
        DEFINE_KEY(F1                  , 290),
        DEFINE_KEY(F2                  , 291),
        DEFINE_KEY(F3                  , 292),
        DEFINE_KEY(F4                  , 293),
        DEFINE_KEY(F5                  , 294),
        DEFINE_KEY(F6                  , 295),
        DEFINE_KEY(F7                  , 296),
        DEFINE_KEY(F8                  , 297),
        DEFINE_KEY(F9                  , 298),
        DEFINE_KEY(F10                 , 299),
        DEFINE_KEY(F11                 , 300),
        DEFINE_KEY(F12                 , 301),
        DEFINE_KEY(F13                 , 302),
        DEFINE_KEY(F14                 , 303),
        DEFINE_KEY(F15                 , 304),
        DEFINE_KEY(F16                 , 305),
        DEFINE_KEY(F17                 , 306),
        DEFINE_KEY(F18                 , 307),
        DEFINE_KEY(F19                 , 308),
        DEFINE_KEY(F20                 , 309),
        DEFINE_KEY(F21                 , 310),
        DEFINE_KEY(F22                 , 311),
        DEFINE_KEY(F23                 , 312),
        DEFINE_KEY(F24                 , 313),
        DEFINE_KEY(F25                 , 314),

        /* Keypad */
        DEFINE_KEY(KP0                 , 320),
        DEFINE_KEY(KP1                 , 321),
        DEFINE_KEY(KP2                 , 322),
        DEFINE_KEY(KP3                 , 323),
        DEFINE_KEY(KP4                 , 324),
        DEFINE_KEY(KP5                 , 325),
        DEFINE_KEY(KP6                 , 326),
        DEFINE_KEY(KP7                 , 327),
        DEFINE_KEY(KP8                 , 328),
        DEFINE_KEY(KP9                 , 329),
        DEFINE_KEY(KPDECIMAL           , 330),
        DEFINE_KEY(KPDIVIDE            , 331),
        DEFINE_KEY(KPMULTIPLY          , 332),
        DEFINE_KEY(KPSUBTRACT          , 333),
        DEFINE_KEY(KPADD               , 334),
        DEFINE_KEY(KPENTER             , 335),
        DEFINE_KEY(KPEQUAL             , 336),

        DEFINE_KEY(LEFTSHIFT           , 340),
        DEFINE_KEY(LEFTCONTROL         , 341),
        DEFINE_KEY(LEFTALT             , 342),
        DEFINE_KEY(LEFTSUPER           , 343),
        DEFINE_KEY(RIGHTSHIFT          , 344),
        DEFINE_KEY(RIGHTCONTROL        , 345),
        DEFINE_KEY(RIGHTALT            , 346),
        DEFINE_KEY(RIGHTSUPER          , 347),
        DEFINE_KEY(MENU                , 348),
        QS_KEY_MAX
    } keys;
}