#include "keyboard.h"
#include "process.h"

/* bitmap of down keys */
bool *keysDown;

void keyboard_initialize() {
	keysDown = new bool[104];
	for(unsigned int i = 0; i < 104; i++)
		keysDown[i] = false;
}

void keyboard_key_down(unsigned int key) {
	keysDown[key] = true;
}

void keyboard_key_up(unsigned int key) {
	keysDown[key] = false;
}

unsigned int keyboard_translate_control(Process *process, unsigned int key) {
	switch(key) {
	case KEY_NUMPAD_7:
		if(process->keyboardState.num_lock)
			return KEY_HOME;
		else
			return KEY_7;
	case KEY_NUMPAD_8:
		if(process->keyboardState.num_lock)
			return KEY_UP;
		else
			return KEY_8;
	case KEY_NUMPAD_9:
		if(process->keyboardState.num_lock)
			return KEY_PAGE_UP;
		else
			return KEY_9;
	case KEY_NUMPAD_4:
		if(process->keyboardState.num_lock)
			return KEY_LEFT;
		else
			return KEY_4;
	case KEY_NUMPAD_6:
		if(process->keyboardState.num_lock)
			return KEY_RIGHT;
		else
			return KEY_5;
	case KEY_NUMPAD_1:
		if(process->keyboardState.num_lock)
			return KEY_END;
		else
			return KEY_1;
	case KEY_NUMPAD_2:
		if(process->keyboardState.num_lock)
			return KEY_DOWN;
		else
			return KEY_2;
	case KEY_NUMPAD_3:
		if(process->keyboardState.num_lock)
			return KEY_PAGE_DOWN;
		else
			return KEY_9;
	case KEY_NUMPAD_0:
		if(process->keyboardState.num_lock)
			return KEY_INSERT;
		else
			return KEY_0;
	case KEY_NUMPAD_PERIOD:
		if(process->keyboardState.num_lock)
			return KEY_DELETE;
		else
			return KEY_PERIOD;
	default:
		return key;
	}
}

bool is_shift_character(Process *process) {
	return process->keyboardState.caps_lock ^ (keysDown[KEY_LEFT_SHIFT] || keysDown[KEY_RIGHT_SHIFT]);
};

char keyboard_key_to_character(Process *process, unsigned int key) {
	switch(key) {
	default:
		return '\0';
	case KEY_TILDE:
		if(is_shift_character(process))
			return '~';
		else
			return '`';
	case KEY_1:
		if(is_shift_character(process))
			return '!';
		else
			return '1';
	case KEY_2:
		if(is_shift_character(process))
			return '@';
		else
			return '2';
	case KEY_3:
		if(is_shift_character(process))
			return '#';
		else
			return '3';
	case KEY_4:
		if(is_shift_character(process))
			return '$';
		else
			return '4';
	case KEY_5:
		if(is_shift_character(process))
			return '%';
		else
			return '5';
	case KEY_6:
		if(is_shift_character(process))
			return '^';
		else
			return '6';
	case KEY_7:
		if(is_shift_character(process))
			return '&';
		else
			return '7';
	case KEY_8:
		if(is_shift_character(process))
			return '*';
		else
			return '8';
	case KEY_9:
		if(is_shift_character(process))
			return '(';
		else
			return '9';
	case KEY_0:
		if(is_shift_character(process))
			return ')';
		else
			return '0';
	case KEY_UNDERSCORE:
		if(is_shift_character(process))
			return '_';
		else
			return '-';
	case KEY_EQUALS:
		if(is_shift_character(process))
			return '+';
		else
			return '=';
	case KEY_BACKSPACE:
		return '\b';
	case KEY_TAB:
		return '\t';
	case KEY_Q:
		if(is_shift_character(process))
			return 'Q';
		else
			return 'q';
	case KEY_W:
		if(is_shift_character(process))
			return 'W';
		else
			return 'w';
	case KEY_E:
		if(is_shift_character(process))
			return 'E';
		else
			return 'e';
	case KEY_R:
		if(is_shift_character(process))
			return 'R';
		else
			return 'r';
	case KEY_T:
		if(is_shift_character(process))
			return 'T';
		else
			return 't';
	case KEY_Y:
		if(is_shift_character(process))
			return 'Y';
		else
			return 'y';
	case KEY_U:
		if(is_shift_character(process))
			return 'U';
		else
			return 'u';
	case KEY_I:
		if(is_shift_character(process))
			return 'I';
		else
			return 'i';
	case KEY_O:
		if(is_shift_character(process))
			return 'O';
		else
			return 'o';
	case KEY_P:
		if(is_shift_character(process))
			return 'P';
		else
			return 'p';
	case KEY_OPENING_BRACKET:
		if(is_shift_character(process))
			return '{';
		else
			return '[';
	case KEY_CLOSING_BRACKET:
		if(is_shift_character(process))
			return '}';
		else
			return ']';
	case KEY_BACK_SLASH:
		if(is_shift_character(process))
			return '|';
		else
			return '\\';
	case KEY_A:
		if(is_shift_character(process))
			return 'A';
		else
			return 'a';
	case KEY_S:
		if(is_shift_character(process))
			return 'S';
		else
			return 's';
	case KEY_D:
		if(is_shift_character(process))
			return 'D';
		else
			return 'd';
	case KEY_F:
		if(is_shift_character(process))
			return 'F';
		else
			return 'f';
	case KEY_G:
		if(is_shift_character(process))
			return 'G';
		else
			return 'g';
	case KEY_H:
		if(is_shift_character(process))
			return 'H';
		else
			return 'h';
	case KEY_J:
		if(is_shift_character(process))
			return 'J';
		else
			return 'j';
	case KEY_K:
		if(is_shift_character(process))
			return 'K';
		else
			return 'k';
	case KEY_L:
		if(is_shift_character(process))
			return 'L';
		else
			return 'l';
	case KEY_SEMI_COLON:
		if(is_shift_character(process))
			return ':';
		else
			return ';';
	case KEY_QUOTE:
		if(is_shift_character(process))
			return '"';
		else
			return '\'';
	case KEY_ENTER:
		return '\n';
	case KEY_Z:
		if(is_shift_character(process))
			return 'Z';
		else
			return 'z';
	case KEY_X:
		if(is_shift_character(process))
			return 'X';
		else
			return 'x';
	case KEY_C:
		if(is_shift_character(process))
			return 'C';
		else
			return 'c';
	case KEY_V:
		if(is_shift_character(process))
			return 'V';
		else
			return 'v';
	case KEY_B:
		if(is_shift_character(process))
			return 'B';
		else
			return 'b';
	case KEY_N:
		if(is_shift_character(process))
			return 'N';
		else
			return 'n';
	case KEY_M:
		if(is_shift_character(process))
			return 'M';
		else
			return 'm';
	case KEY_COMMA:
		if(is_shift_character(process))
			return '<';
		else
			return ',';
	case KEY_PERIOD:
		if(is_shift_character(process))
			return '>';
		else
			return '.';
	case KEY_FORWARD_SLASH:
		if(is_shift_character(process))
			return '?';
		else
			return '/';
	case KEY_SPACE:
		return ' ';
	}
}

bool keyboard_is_key_down(unsigned int key) {
	return keysDown[key];
}
