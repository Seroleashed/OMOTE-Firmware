#pragma once

void init_keys_HAL(void);
void keys_getKeys_HAL(void* ptr, unsigned long currentMillis);

// The simulator has no matrix: its key presses come from an image map of the
// remote (keypad_gui), not from rows and columns. Always returns false.
bool get_keypadMatrix_HAL(char (*matrix)[5]);
