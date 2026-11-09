# One internal paint makes a Wine message loop spin forever

After a single `RedrawWindow(hwnd, NULL, NULL, RDW_INTERNALPAINT)` whose
`WM_PAINT` is handled normally, Wine keeps the thread's `QS_PAINT` wake bit
set for good. A message loop that waits with
`MsgWaitForMultipleObjectsEx(..., QS_ALLINPUT, MWMO_INPUTAVAILABLE)` then
returns immediately every time and never sleeps again: about 50,000 wake-ups
a second in an idle GUI thread, one core busy.

[winit](https://github.com/rust-windowing/winit), the common Rust windowing
library, does both, so every winit program running under Wine or Proton is
affected once its hidden event window receives a message.

Found while profiling a Windows game build under Proton, where the window
thread used about 550 ms of CPU a second with nothing to do, then reduced to
the 70-line program here.

Reported as [Wine bug 60732](https://bugs.winehq.org/show_bug.cgi?id=60732)
(2026-11-09).

## Results

`repro.exe <paint> <wait>` handles one window's first paint, then optionally
triggers one internal paint and handles its `WM_PAINT`, then waits for 3
seconds with a 100 ms timeout and counts how often the wait returned. Idle,
that is about 30.

| Case | GE-Proton 11-6 | Wine 11.0 | Wine 11.19 | Windows Server 2022 | Windows Server 2025 |
| --- | ---: | ---: | ---: | ---: | ---: |
| `none avail`: no internal paint (control) | 42 | 31 | 31 | 30 | 33 |
| `defproc avail`: internal paint, `WM_PAINT` to `DefWindowProc` | **148,713** | **159,500** | **159,116** | 30 | 33 |
| `validate avail`: internal paint, `WM_PAINT` answered with `ValidateRect` | **149,200** | **153,623** | **158,251** | 30 | 30 |
| `defproc plain`: internal paint, wait without `MWMO_INPUTAVAILABLE` | 36 | 31 | 31 | 30 | 31 |

On Windows the internal `WM_PAINT` is delivered once too, and the loop
stays idle. The Windows columns come from this repository's
[workflow](.github/workflows/windows.yml) on GitHub's runners (10.0.20348
and 10.0.26100;
[run 42612359336](https://github.com/rekoriku/wine-internal-paint-spin/actions/runs/42612359336)).

## Cause

In Wine's server (links pinned to master at
[78e52205](https://gitlab.winehq.org/wine/wine/-/commit/78e52205d9b9ac1c7e6f55fc613b882f1ad093d9),
2026-11-06):

1. `RDW_INTERNALPAINT` sets the window's `PAINT_INTERNAL` flag and raises
   the queue's paint count:
   [window.c#L1690](https://gitlab.winehq.org/wine/wine/-/blob/78e52205d9b9ac1c7e6f55fc613b882f1ad093d9/server/window.c#L1690).
2. `get_message` hands out `WM_PAINT` through `find_window_to_repaint`
   ([queue.c#L3356](https://gitlab.winehq.org/wine/wine/-/blob/78e52205d9b9ac1c7e6f55fc613b882f1ad093d9/server/queue.c#L3356)),
   which clears `PAINT_INTERNAL` without lowering the count
   ([window.c#L1165](https://gitlab.winehq.org/wine/wine/-/blob/78e52205d9b9ac1c7e6f55fc613b882f1ad093d9/server/window.c#L1165)).
   It does so even for a `PM_NOREMOVE` peek.
3. Every path that lowers the count for an internal paint
   (`RDW_NOINTERNALPAINT`, validating the whole window, destroying it)
   first checks that `PAINT_INTERNAL` is still set
   ([window.c#L1591](https://gitlab.winehq.org/wine/wine/-/blob/78e52205d9b9ac1c7e6f55fc613b882f1ad093d9/server/window.c#L1591),
   [#L1695](https://gitlab.winehq.org/wine/wine/-/blob/78e52205d9b9ac1c7e6f55fc613b882f1ad093d9/server/window.c#L1695)),
   so after the handout the increment is never undone. `QS_PAINT` follows
   the count
   ([queue.c#L2796](https://gitlab.winehq.org/wine/wine/-/blob/78e52205d9b9ac1c7e6f55fc613b882f1ad093d9/server/queue.c#L2796)).
4. `MWMO_INPUTAVAILABLE` makes the wait return while any wake bit is set,
   not only on new input, so the stale `QS_PAINT` wakes it at once, and
   `PeekMessage` finds no message to take.

A Wine server trace of the spinning thread shows the pair
`get_message(...) = PENDING` and `set_queue_mask(...) = { wake_bits=0x20 }`
(`QS_PAINT`) repeating about a million times in 25 seconds.

How winit 0.30.13 reaches it (`src/platform_impl/windows/`): its thread
event target window calls `RedrawWindow(..., RDW_INTERNALPAINT)` for every
message other than `WM_PAINT` (`event_loop.rs:2491`, so
`EventLoopProxy` wake-ups trigger it), `Window::request_redraw` does the
same (`window.rs:156`), and the event loop waits with
`MWMO_INPUTAVAILABLE` (`event_loop.rs:756`).

## Possible fix

In `find_window_to_repaint`, when it clears `PAINT_INTERNAL`, also call
`inc_window_paint_count( win, -1 )`; and perhaps leave the flag alone for a
`PM_NOREMOVE` peek. Not yet tried against Wine's tests.

Related: [Wine bug 10522](https://bugs.winehq.org/show_bug.cgi?id=10522)
(2007, still open: "the queue paint count never drops to zero", one core
busy, no cause found) and
[Wine bug 56210](https://bugs.winehq.org/show_bug.cgi?id=56210) (a winit
program with very slow event handling under Wine).

## Running it

Under Wine, with an `x86_64-w64-mingw32` compiler and `wine` on `PATH`:

```sh
./run-wine.sh
```

On Windows, from a Visual Studio developer prompt:

```bat
cl /O2 repro.c user32.lib
repro none avail
repro defproc avail
repro validate avail
repro defproc plain
```
