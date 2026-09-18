/**
 * win32_shim.h — Minimal Win32 type stubs for cross-platform compilation
 *
 * This provides just enough Win32 type definitions to compile the
 * decompiled Sonic R code on non-Windows platforms. For actual
 * Windows builds, use the real Windows SDK headers instead.
 */

#ifndef WIN32_SHIM_H
#define WIN32_SHIM_H

/* On real Windows the WinSDK headers provide all of these symbols already.
 * Including this shim there causes a wall of redefinition errors, so make
 * the entire body a no-op when _WIN32 is defined. */
#ifndef _WIN32

#include <stdint.h>
#include <stddef.h>

/* Basic Windows types */
typedef uint32_t    DWORD;
typedef uint16_t    WORD;
typedef int32_t     LONG;
#ifndef __OBJC__
typedef int         BOOL;
#endif
typedef uint8_t     BYTE;
typedef uint16_t    USHORT;
typedef char        CHAR;
typedef unsigned char UCHAR;
typedef void       *LPVOID;
typedef const char *LPCSTR;
typedef char       *LPSTR;
typedef DWORD      *LPDWORD;
typedef void       *HANDLE;
typedef void       *HWND;
typedef void       *HINSTANCE;
typedef void       *HMODULE;
typedef void       *HMENU;
typedef void       *HICON;
typedef void       *HCURSOR;
typedef void       *HACCEL;
typedef void       *HBRUSH;
typedef void       *HDC;
typedef void       *HGLOBAL;
typedef uint16_t    ATOM;
typedef uintptr_t   DWORD_PTR;
typedef LONG        LRESULT;
typedef unsigned int UINT;
typedef DWORD       MCIERROR;
typedef int         MMRESULT;

/* Win32 callback types */
typedef LRESULT (*WNDPROC)(HWND, UINT, DWORD, LONG);
typedef DWORD (*LPTHREAD_START_ROUTINE)(LPVOID);
typedef void *LPSECURITY_ATTRIBUTES;

/* Win32 structs (minimal) */
typedef struct { int dummy; } MSG, *LPMSG;
typedef struct {
    UINT style;
    WNDPROC lpfnWndProc;
    int cbClsExtra;
    int cbWndExtra;
    HINSTANCE hInstance;
    HICON hIcon;
    HCURSOR hCursor;
    HBRUSH hbrBackground;
    LPCSTR lpszMenuName;
    LPCSTR lpszClassName;
} WNDCLASSA;
typedef struct { LONG left, top, right, bottom; } RECT, *LPRECT;
typedef struct { BYTE peRed, peGreen, peBlue, peFlags; } PALETTEENTRY, *LPPALETTEENTRY;

/* Win32 API stubs — these would be linked from kernel32/user32/gdi32 */
#define SW_SHOWNORMAL   1
#define SW_SHOWDEFAULT  10
#define SM_CXSCREEN     0
#define SM_CYSCREEN     1
#define CS_HREDRAW      1
#define CS_VREDRAW      2
#define WS_POPUP        0x80000000
#define WS_EX_TOPMOST   8
#define PM_REMOVE       1
#define IDC_ARROW       ((LPCSTR)0x7F00)
#define MAX_PATH        260
#define MEM_COMMIT      0x1000
#define PAGE_EXECUTE_READWRITE 0x40
#define MEM_RELEASE     0x8000
#define WAIT_TIMEOUT    0x102
#define INFINITE        0xFFFFFFFF
#define FALSE           0
#define TRUE            1

/* Win32 function declarations (stubs for compilation).
 * Skip in Objective-C files — BOOL conflicts with objc/objc.h. */
#ifndef __OBJC__
DWORD   timeGetTime(void);
#endif

#endif /* !_WIN32 */

#endif /* WIN32_SHIM_H */
