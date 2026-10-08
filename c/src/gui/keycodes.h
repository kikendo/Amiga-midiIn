/*
 * keycodes.h - key code constants (mbkeycod.e)
 *
 * Plugins report keys in their keycode field: a vanilla key as its
 * character code, a raw key as code | MYRAWCODE (| SHIFTQUAL with shift).
 */
#ifndef MI_KEYCODES_H
#define MI_KEYCODES_H

#define KEYCODE_F10 0x59
#define DEL_CODE    127
#define MYRAWCODE   0x100
#define ESC_CODE    27
#define SHIFTQUAL   0x200

#endif
