# Wheelclick AutoScroll (Windows->MacOS)

*Windows-style middle-click autoscroll for macOS*

---

## Overview

**AutoScroll** is a small background utility for macOS that implements a **toggleable, velocity-based autoscroll mechanism**, similar to the middle-click autoscroll behavior found on Windows.

Unlike drag-to-scroll tools, AutoScroll uses a **mode-based** interaction:

- Click the scroll wheel (or configured button) once to enable autoscroll
- Move the mouse slightly to set a scrolling speed and direction
- Hold the mouse still to continue scrolling at a constant rate
- Click again to stop

Scrolling speed is proportional to the distance of the mouse cursor from the activation point, allowing long, comfortable scrolling without continuous dragging.

---

## How it works

1. Middle-click (or configured button) toggles autoscroll mode
2. The cursor position at activation becomes an **anchor point**
3. Cursor distance from the anchor controls scroll **velocity**
4. Scrolling continues even when the mouse is stationary
5. A second click (or releasing modifiers) exits autoscroll mode

This interaction closely matches Windows’ native autoscroll behavior and is especially useful for:

- Reading long documents
- Browsing large web pages
- Trackball and mouse users who prefer click-to-scroll over gesture-based scrolling

---

## Supported versions

Tested on macOS **10.9 through 14.x**.

Accessibility and Input Monitoring permissions are required.

---

## Installation

Download the latest release from the **Releases** page and drag `AutoScroll.app` to `/Applications`.

On first launch, macOS will prompt for required permissions:

- **Accessibility**
- **Input Monitoring**

The application will wait until permissions are granted.

> **Important**  
> Do not revoke Accessibility or Input Monitoring permissions while AutoScroll is running.

### Run at login (optional)

- macOS 13.0 and later:  
  `System Settings → General → Login Items`
- Earlier versions:  
  `System Preferences → Users & Groups → Login Items`

Add `AutoScroll.app`.

---

## Configuration

AutoScroll uses macOS defaults for configuration. Restart the application after changing any setting.

### Mouse button

The default toggle button is the **middle mouse button**.

To change it:

```
defaults write com.yourbundleid.AutoScroll button -int BUTTON
```

- Button numbers are one-based:
  - 1 = left click
  - 2 = right click
  - 3 = middle click (default)
  - 4–32 = additional mouse buttons

Set `button` to `0` to disable mouse-button activation.

---

### Modifier keys (optional)

Autoscroll may also be activated by holding modifier keys.

```
defaults write com.yourbundleid.AutoScroll keys -array [KEYS...]
```

Supported keys:

- `capslock`
- `shift`
- `control`
- `option`
- `command`

Example:

```
defaults write com.yourbundleid.AutoScroll keys -array option
```

To disable modifier keys:

```
defaults write com.yourbundleid.AutoScroll keys -array
```

---

### Scroll speed

Controls overall scroll sensitivity.

```
defaults write com.yourbundleid.AutoScroll speed -int SPEED
```

- Default: `3`
- Higher values scroll faster
- Negative values invert scroll direction

---

## Uninstallation

1. Quit AutoScroll
2. Move the application to Trash
3. Remove it from:
   - Accessibility
   - Input Monitoring
   - Login Items (if added)

To remove preferences:

```
defaults delete com.yourbundleid.AutoScroll
```

---

## Potential issues

### App won’t launch

Unsigned binaries may be blocked by Gatekeeper.

```
xattr -dr com.apple.quarantine /Applications/AutoScroll.app
codesign --force --deep --sign - /Applications/AutoScroll.app
```

### App does not respond to mouse input

Ensure **both** permissions are enabled:

- Accessibility
- Input Monitoring

If issues persist, remove and re-add AutoScroll in System Settings.

---

## Origin and credits

AutoScroll is derived from **DragScroll** by Emre Yolcu:

https://github.com/emreyolcu/drag-scroll

This project replaces DragScroll’s drag-based scrolling model with a **stateful, velocity-based autoscroll interaction** inspired by Windows’ native behavior.

The original license terms are preserved.

---

## License

Same license as the original DragScroll project. See `LICENSE` for details.

