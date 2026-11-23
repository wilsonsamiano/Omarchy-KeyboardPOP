# KeyboardPOP for Omarchy

![KeyboardPOP with the popup bubble over the g key and next-word suggestions](docs/screenshot.png)

A touch keyboard for Omarchy tablets like the Surface Go, ported from the
[PressPop / SafePop Android keyboard](https://github.com/wilsonsamiano/presspop-keyboard).
Press a key and an enlarged bubble pops up above it; glide across the
letters to swipe-type. It follows the Omarchy theme's accent and window
rounding live, and its few words follow the language picked under
Setup > Language.

## What it does

- **Key popups** on every key, iPhone style. Slide and the bubble follows.
- **Swipe typing.** Glide across the letters and lift; the shape is matched
  against the dictionary (your own words first), with a trail in the accent
  color.
- **Suggestions.** The strip shows completions while you type and the
  likely next word between words (from your own sentences, then KeyboardPOP's
  phrase model). On Space, a word that isn't in the dictionary gets its
  closest word offered in the middle slot: tap it to fix the word, tap the
  word as you typed it to keep it.
- **Always fixed:** `i` → `I`, `dont` → `don't`, `im` → `I'm`, rank
  abbreviations (`sgt` → `SGT`) and brand names (`iphone` → `iPhone`).
  Delete right after a contraction puts the word back as you typed it.
- **Auto-capitalization** at the start of a field and after `. ? !`.
- **Personal dictionary.** A word you keep typing (77 times, or sooner when
  you turn down its corrections) is learned and stops being corrected.
- **Accents.** Hold a letter for its accents, slide to one, lift.
- **Symbols without switching.** Pull a letter key down for the symbol in
  its corner (`q` → `1`, `a` → `@`, …).
- **Space-bar cursor.** Hold Space, then slide to move the cursor (up and
  down moves lines).
- **Double-tap Shift** for caps lock. **Double-tap Space** for `. `.
- **Delete** repeats while held; swipe left off it to delete a word.
- **Control row** on every page, for driving the computer by touch: `esc`,
  `tab`, the arrows, and `ctrl`, `alt`, and `❖` (Super). The modifiers
  latch: tap `❖` then Space for the Omarchy menu, `ctrl` then `c` to copy.
- **Pages:** letters, numbers, symbols, 30 emoji, and a phone-style number
  pad.
- **Languages:** tap **EN** in the strip to cycle English, Tagalog, and
  Spanish dictionaries.
- **Two-thumb typing:** a second finger landing finishes the first finger's
  tap instead of being ignored.

Everything runs on this machine; nothing is sent anywhere.

## Showing and hiding

Tap **KeyboardPOP** in the app launcher (`Super + Space`) to show the keyboard,
and again to hide it; the chevron at the right of the strip hides it too.
Windows make room above it instead of being covered.

To open and close it with a key, bind `keyboardpop toggle` in
`~/.config/hypr/bindings.lua`:

```lua
o.bind("SUPER + CTRL + Y", "Touch keyboard", "keyboardpop toggle")
```

### Opening by itself on text fields

KeyboardPOP can open whenever a text field is focused, and then it also knows
the text around the cursor and turns off suggestions in password fields.
That needs KeyboardPOP to be the seat's **input method**, and Wayland allows
only one. Omarchy runs **fcitx5** as the input method (for compose keys,
emoji picking, and Asian scripts), so while fcitx5 is running KeyboardPOP types
as a virtual keyboard instead: it works in every app, but you show and hide
it yourself. The ⓘ in the strip says which way it's typing.

Without fcitx5, start KeyboardPOP with the session, hidden, in
`~/.config/hypr/autostart.lua`, and it will come and go with text fields:

```lua
o.launch_on_start("keyboardpop hide")
```

It only pops up when you tap a field with your finger; clicking one with
the mouse or tabbing to it leaves it hidden. To tell the two apart KeyboardPOP
reads the touchscreen, which the package's udev rule
(`/usr/lib/udev/rules.d/70-keyboardpop-touchscreen.rules`) lets the logged-in
user do for touchscreens only. Without that access it pops up on every
focused field.

It gets out of the way when the hardware keyboard is in use: when a
keyboard (like the type cover) is attached, when the touchpad is touched
(the udev rule covers touchpads too), and when a hardware key is pressed.
That last one needs a few lines in `~/.config/hypr/autostart.lua`, since
only Hyprland sees which keys are pressed. It sends only "a key was
pressed", and only while the keyboard is showing:

```lua
hl.on("input.keyboard.key", function(_, _, pressed)
  if pressed ~= 1 then return end
  for _, layer in ipairs(hl.get_layers()) do
    if layer.namespace == "keyboardpop" and layer.mapped then
      hl.exec_cmd("keyboardpop physical-key")
      return
    end
  end
end)
```

## Tablet extras

Two small tools ship alongside the keyboard to make an Omarchy tablet feel
more like an iPad. Start both with the session in
`~/.config/hypr/autostart.lua`:

```lua
o.launch_on_start("autorotate")
hl.on("config.reloaded", function()
  hl.exec_cmd("autorotate reapply")
end)
o.launch_on_start("touchswipe")
```

**`autorotate`** turns the built-in screen, touchscreen, and pen to match
how the tablet is held, reading the accelerometer through
`iio-sensor-proxy` (`sudo pacman -S iio-sensor-proxy`).
`autorotate lock` toggles rotation lock; bind it to a key if you like:

```lua
o.bind("SUPER + CTRL + U", "Rotation lock", "autorotate lock")
```

The `config.reloaded` hook puts the rotation back after a Hyprland reload,
which would otherwise reset the screen to upright. The lock state is
written to `~/.local/state/autorotate/status` for a bar widget to show.

**`touchswipe`** adds iPad-style touchscreen gestures:

- A quick one-finger swipe switches desktops the iPad way: the desktops
  follow your finger, so right to left brings in the next desktop and left
  to right goes back. It has to cross nearly half the screen, so shorter
  swipes, like a browser's back swipe, still go to the app. It also has to
  be quick, so slow drags like selecting text don't count, and swipes on
  the on-screen keyboard are left alone.
- Four fingers swiped left or right do the same.
- A swipe up from the bottom edge opens the Omarchy menu (Super + Space).
  Swiping up and holding still opens the app switcher instead (below).
- A touch dismisses the Omarchy screensaver, which otherwise only closes on
  a keypress. Hyprland
only does gestures on touchpads, so it reads the touchscreen (through the
same udev rule as the keyboard) and follows `autorotate`'s rotation. It calls
a `desktop_step` function that goes in `~/.config/hypr/input.lua`, along with
the same swipe on the touchpad:

```lua
function desktop_step(step)
  local current = hl.get_active_workspace().id
  local target = math.max(1, math.min(5, current + step))
  if target ~= current then
    hl.dispatch(hl.dsp.focus({ workspace = tostring(target) }))
  end
  -- Page dots at the bottom (tablet/gesture-feedback); they nudge at the ends.
  hl.exec_cmd("omarchy-shell gesture desktop " .. target .. " 5 " .. (target == current and 1 or 0))
end
hl.gesture({ fingers = 4, direction = "left", action = function() desktop_step(1) end })
hl.gesture({ fingers = 4, direction = "right", action = function() desktop_step(-1) end })
```

**App switcher** (`tablet/app-switcher`) is an Omarchy shell plugin with a
card for each open window, newest first: tap a card to switch to it, swipe
it up to close the window, or drag it onto a desktop number to move it
there. Install it with:

```sh
cp -r tablet/app-switcher ~/.config/omarchy/plugins/
omarchy plugin enable touch.app-switcher
```

`omarchy-shell shell summon touch.app-switcher` opens it, which you can
also bind to a key.

**Gesture feedback** (`tablet/gesture-feedback`) shows iPad-style page dots
at the bottom of the screen when a swipe switches desktops, and nudges them
when a swipe runs into the first or last desktop. Its window only exists
while the dots are showing, and touches pass through it. Install it like the
app switcher (`omarchy plugin enable touch.gesture-feedback`); the
`desktop_step` function above triggers it.

## Commands

| Command | |
|---|---|
| `keyboardpop` / `keyboardpop toggle` | Show the keyboard, or hide it if it's showing |
| `keyboardpop show` / `keyboardpop hide` | Show or hide it (`hide` starts it hidden if it isn't running) |
| `keyboardpop quit` | Stop it |
| `keyboardpop import <file>` | Merge a personal-dictionary backup from the Android app |
| `keyboardpop export <file>` | Save the personal dictionary as a backup |

The personal dictionary lives in `~/.local/share/keyboardpop/personal.json`,
in the same format the Android app exports, so the two can share words.

The keyboard never takes keyboard focus away from the app you're typing in,
so it has no keyboard shortcuts of its own (no `?` or `Q`): everything is a
touch target.

## Build

```sh
bin/build      # qmake6 + make into build/keyboardpop
bin/test       # the Qt Test suite, offscreen
bin/install    # makepkg -fsi, so it shows up in the app launcher
```

Needs `qt6-base`, `qt6-declarative`, `qt6-wayland`, `layer-shell-qt`,
`wayland`, and `libxkbcommon`, all part of Omarchy.

## Not yet

From the Android app: clipboard history, one-handed mode, the number-row
toggle, custom language packs, voice typing, and GIF search.

## License

MIT. See [LICENSE](LICENSE).
