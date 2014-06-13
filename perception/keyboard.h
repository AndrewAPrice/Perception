#ifndef KEYBOARD_H
#define KEYBOARD_H

#define KEY_ESCAPE 0
#define KEY_F1 1
#define KEY_F2 2
#define KEY_F3 3
#define KEY_F4 4
#define KEY_F5 5
#define KEY_F6 6
#define KEY_F7 7
#define KEY_F8 8
#define KEY_F9 9
#define KEY_F10 10
#define KEY_F11 11
#define KEY_F12 12
#define KEY_PRINT_SCREEN 13
#define KEY_SCROLL_LOCK 14
#define KEY_PAUSE_BREAK 15
#define KEY_TILDE 16
#define KEY_1 17
#define KEY_2 18
#define KEY_3 19
#define KEY_4 20
#define KEY_5 21
#define KEY_6 22
#define KEY_7 23
#define KEY_8 24
#define KEY_9 25
#define KEY_0 26
#define KEY_UNDERSCORE 27
#define KEY_EQUALS 28
#define KEY_BACKSPACE 29
#define KEY_TAB 30
#define KEY_Q 31
#define KEY_W 32
#define KEY_E 33
#define KEY_R 34
#define KEY_T 35
#define KEY_Y 36
#define KEY_U 37
#define KEY_I 38
#define KEY_O 39
#define KEY_P 40
#define KEY_OPENING_BRACKET 41
#define KEY_CLOSING_BRACKET 42
#define KEY_BACK_SLASH 43
#define KEY_CAPS_LOCK 44
#define KEY_A 45
#define KEY_S 46
#define KEY_D 47
#define KEY_F 48
#define KEY_G 49
#define KEY_H 50
#define KEY_J 51
#define KEY_K 52
#define KEY_L 53
#define KEY_SEMI_COLON 54
#define KEY_QUOTE 55
#define KEY_ENTER 56
#define KEY_LEFT_SHIFT 57
#define KEY_Z 58
#define KEY_X 59
#define KEY_C 60
#define KEY_V 61
#define KEY_B 62
#define KEY_N 63
#define KEY_M 64
#define KEY_COMMA 65
#define KEY_PERIOD 66
#define KEY_FORWARD_SLASH 67
#define KEY_RIGHT_SHIFT 68
#define KEY_LEFT_CONTROL 69
#define KEY_LEFT_WINDOWS 70
#define KEY_LEFT_ALT 71
#define KEY_SPACE 72
#define KEY_RIGHT_ALT 73
#define KEY_WINDOWS 74
#define KEY_MENU 75
#define KEY_RIGHT_CONTROL 76
#define KEY_INSERT 77
#define KEY_HOME 78
#define KEY_PAGE_UP 79
#define KEY_DELETE 80
#define KEY_END 81
#define KEY_PAGE_DOWN 82
#define KEY_UP 83
#define KEY_LEFT 84
#define KEY_DOWN 85
#define KEY_RIGHT 86
#define KEY_NUM_LOCK 87
#define KEY_NUMPAD_SLASH 88
#define KEY_NUMPAD_MULTIPLY 89
#define KEY_NUMPAD_MINUS 90
#define KEY_NUMPAD_7 91
#define KEY_NUMPAD_8 92
#define KEY_NUMPAD_9 93
#define KEY_NUMPAD_PLUS 94
#define KEY_NUMPAD_4 95
#define KEY_NUMPAD_5 96
#define KEY_NUMPAD_6 97
#define KEY_NUMPAD_1 98
#define KEY_NUMPAD_2 99
#define KEY_NUMPAD_3 100
#define KEY_NUMPAD_ENTER 101
#define KEY_NUMPAD_0 102
#define KEY_NUMPAD_PERIOD 103

struct Process;

extern void keyboard_initialize();
extern void keyboard_key_down(unsigned int key);
extern void keyboard_key_up(unsigned int key);
extern unsigned int keyboard_translate_control(Process *process, unsigned int key);
extern char keyboard_key_to_character(Process *process, unsigned int key);
extern bool keyboard_is_key_down(unsigned int key);

#endif
