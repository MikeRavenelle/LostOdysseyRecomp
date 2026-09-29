# Scenarios

Scripted in-game runs for `tools/scenario.py`:

```bash
python3 tools/scenario.py list tools/scenarios/*.toml
python3 tools/scenario.py run tools/scenarios/*.toml            # all
python3 tools/scenario.py run tools/scenarios/numara.toml       # one
```

Each run gets its own folder under `out/scenarios/<timestamp>/<name>/` (runtime log,
stdout, screenshots, `result.json`) and never touches the game's own saves, settings
or logs. The shader cache next to the binary is shared.

## File format

```toml
description = "What the run covers"
save = "user17"      # slot folder from --saves (default out/drive-city/save); omit for no save
timeout = 150        # seconds; reaching it is the normal end of a run
buttons = "s@300,a@900"   # swap-stamped presses (LO_AUTO_BUTTONS): s a b x y, u/d/l/r d-pad
timeline = "s@10,a@30"    # the same letters at seconds since launch; use this when the
                          # frame rate differs from 30 FPS (swap counts then drift)
pulse = 6            # polls per stamped press (default 6)

[[step]]             # event-driven steps, in order
wait_draws = 700     # wait until a frame reaches this many draws (scene loaded)
wait_seconds = 0     # and/or this many seconds since launch
press = "a"          # buttons: a b x y start back lb rb up down left right, joined with +
stick = [0, 28000]   # left stick x, y
polls = 600          # how long the press/stick is held (input polls)
hold_seconds = 20    # time before the next step
screenshot = true

[settings]           # settings.ini overrides; runs start from frame_rate=30,
antialiasing = 3     # internal_resolution=0, antialiasing=0, upscaler=0,
                     # window_mode=0, variable_refresh_rate=0

[env]                # extra environment variables for the game
LO_SCREENSHOT_PRESENTED = "1"   # screenshots of the presented image, not the guest frame

[checks]
min_draws = 700      # the scene was reached
min_fps = 25         # median of the second half of the run
max_errors = 0       # [error] log lines allowed
```

A run fails on a crash report, an early exit, a guest bug check, a disc failure, a
shader compile failure, error lines above `max_errors`, unfinished steps, or a missed
draw/FPS target.

## Saves

Saves are personal and never committed. These scenarios expect Xenia saves converted
with `tools/import_xenia_saves.py` (folder `userN` is in-game slot N-1):

| Slot folder | Location |
| :--- | :--- |
| user15 | Disc 1, ship cells after the jail sequence |
| user16 | Disc 2 start, Crimson Forest shrine |
| user17 | Disc 4 world map, Northern Shore of Ipsilon |
| user18 | Seeker of the Deep! DLC dungeon (needs the DLC imported) |
