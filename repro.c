/* Wine: QS_PAINT stays set after a WM_PAINT that came from
 * RedrawWindow(RDW_INTERNALPAINT), so a MsgWaitForMultipleObjectsEx loop
 * with MWMO_INPUTAVAILABLE never sleeps again.
 *
 *   repro.exe <paint> <wait>
 *   paint: none     no internal paint (control)
 *          defproc  RDW_INTERNALPAINT, WM_PAINT left to DefWindowProc
 *          validate RDW_INTERNALPAINT, WM_PAINT answered with ValidateRect
 *   wait:  avail    MWMO_INPUTAVAILABLE (as winit 0.30 waits)
 *          plain    no flags
 *
 * Prints how often the wait returned in 3 seconds (100 ms timeout, so an
 * idle loop returns about 30 times) and whether QS_PAINT is still set. */
#include <stdio.h>
#include <string.h>
#include <windows.h>

static int painted;
static int mode_validate;

static LRESULT CALLBACK proc(HWND window, UINT message, WPARAM w, LPARAM l) {
  if (message == WM_PAINT) {
    painted++;
    if (mode_validate) {
      ValidateRect(window, NULL);
      return 0;
    }
  }
  return DefWindowProcW(window, message, w, l);
}

static void drain(void) {
  MSG msg;
  while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
}

int main(int argc, char** argv) {
  const char* paint = argc > 1 ? argv[1] : "defproc";
  const char* wait = argc > 2 ? argv[2] : "avail";
  mode_validate = strcmp(paint, "validate") == 0;
  WNDCLASSW cls = {0};
  cls.lpfnWndProc = proc;
  cls.hInstance = GetModuleHandleW(NULL);
  cls.lpszClassName = L"repro";
  RegisterClassW(&cls);
  HWND window = CreateWindowExW(0, L"repro", L"repro", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 200,
                                100, NULL, NULL, cls.hInstance, NULL);
  drain(); /* the first, ordinary paint and the window's setup messages */
  int painted_before = painted;
  if (strcmp(paint, "none") != 0) {
    RedrawWindow(window, NULL, NULL, RDW_INTERNALPAINT);
  }
  drain();
  DWORD flags = strcmp(wait, "avail") == 0 ? MWMO_INPUTAVAILABLE : 0;
  long wakeups = 0;
  ULONGLONG end = GetTickCount64() + 3000;
  while (GetTickCount64() < end) {
    MsgWaitForMultipleObjectsEx(0, NULL, 100, QS_ALLINPUT, flags);
    wakeups++;
    drain();
  }
  DWORD status = GetQueueStatus(QS_PAINT);
  printf("paint=%s wait=%s: WM_PAINT after the first %d, wait returned %ld times in 3 s, QS_PAINT %s\n",
         paint, wait, painted - painted_before, wakeups, (LOWORD(status) & QS_PAINT) ? "set" : "clear");
  DestroyWindow(window);
  return 0;
}
