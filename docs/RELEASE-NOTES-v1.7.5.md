# v1.7.5

Run at Startup does what it says: the app comes up quietly in the tray, and
the menu stays shut until you ask for it.

## What was happening

With **Run at Startup: On**, the menu opened by itself a moment after logging
in, with nothing clicked.

Setting a delay made it stop. That is the clue that identifies the bug exactly,
because the delay and the menu have nothing to do with each other — and in the
code they had been welded into one branch:

```cpp
const int delay = settings::GetInt(settings::key::kStartupDelay, 0);
if (delay > 0 && LaunchedAtLogin()) {
    ::SetTimer(hwnd_, kSetupTimer, delay * 1000, nullptr);   // wait, then set up
} else {
    SetUpAfterStartupDelay();
    ShowMenu();                                              // ← and open the menu
}
```

There are two independent questions here:

| Question | Correct answer |
|---|---|
| Defer the setup? | Only when a delay is set **and** Windows started us |
| Open the menu? | Only when the **user** started us — delay or no delay |

`On` with no delay means `delay > 0` is false, so the condition failed, control
fell through to the `else`, and `ShowMenu()` ran alongside the tray icon. With
a delay of 5 s the first branch was taken instead, which is why the 45-second
test showed nothing: the menu was never on that path.

The fix asks the two questions separately:

```cpp
const bool atLogin = LaunchedAtLogin();
const int  delay   = settings::GetInt(settings::key::kStartupDelay, 0);

if (delay > 0 && atLogin) {
    ::SetTimer(hwnd_, kSetupTimer, delay * 1000, nullptr);
} else {
    SetUpAfterStartupDelay();
}

if (!atLogin) ShowMenu();
```

`LaunchedAtLogin()` is asked once and the answer reused. Asking twice would
read the clock twice, and two reads either side of a boundary can disagree.

## The second bug, found on the way

`Run at Startup` had a quieter failure that would have shown up on the next
upgrade.

The executable carries its version in its file name, so the registry entry
written by 1.7.4 names `SnipTextProUltra_1.7.4.exe`. Installing 1.7.5 removes
that file. Windows then has nothing to launch — while the menu still reads
`Run at Startup: On`, because that row only checks whether the entry *exists*,
not whether it points anywhere real. Switching it off and on again was the only
cure, and nothing told you it was needed.

The entry is now compared against the running executable at launch and
rewritten when it no longer matches. One registry read per start; a write only
on the first launch after an upgrade.

## A guess replaced with a fact

To decide whether it had been started by Windows or by you, the app used to
look at how long the machine had been up:

```cpp
return ::GetTickCount64() < 120000;   // up less than two minutes? must be login
```

That is a guess, and it is wrong in one specific case — launching the app
yourself within two minutes of a restart, which is precisely what testing
startup behaviour involves. Your menu would have vanished for no visible
reason.

The startup entry now carries an argument:

```
"C:\...\SnipTextProUltra_1.7.5.exe" --startup
```

so the launch states what it is and the program reads it from its own command
line. The uptime rule stays as a fallback for entries written by older
versions — and since those are rewritten on first launch, it stops being
reachable after one restart.

## Verification

1. Set **Run at Startup: On** (no delay). Restart. The tray icon should appear
   and **no menu should open**. Click the icon — the menu opens normally.
2. Set **Run at Startup: 5 s**. Restart. The tray icon appears about five
   seconds late, still with no menu, and the hotkeys start working at the same
   moment the icon appears.
3. Immediately after a restart, launch the app by hand from Explorer. The menu
   **should** open — this is the case the old uptime guess got wrong.
4. Check `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`, value
   `SnipTextProUltra`. It should read the full 1.7.5 path followed by
   `--startup`, and should have corrected itself from the 1.7.4 path without
   being touched.
