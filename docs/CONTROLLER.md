# Physical Controller

The physical FX pad is handled by a separate bridge process so serial parsing and center-of-pressure calculation do not block the JUCE message thread.

## Firmware Output

`firmware.ino` prints CSV rows at around 80 Hz:

```text
g0,g1,g2,g3
```

The four values are pressure/weight readings in this order:

| Field | Corner |
| --- | --- |
| `g0` | top-right |
| `g1` | top-left |
| `g2` | bottom-left |
| `g3` | bottom-right |

## Bridge

Run the bridge in a separate terminal:

```sh
build/ControllerBridge
```

Default serial settings:

- Port: `/dev/cu.usbmodem1101`
- Baud: `115200`
- Mode: raw, non-blocking read

The bridge prints only the current coordinate or the no-touch state:

```text
XY 0.523,0.417 total=34.2g
検出なし total=2.1g
```

## Coordinate Calculation

Negative readings are clamped to zero. Touch is active when total force is at least 5 g.

```text
total = max(g0,0) + max(g1,0) + max(g2,0) + max(g3,0)
x = (topRight + bottomRight) / total
y = (topLeft + topRight) / total
```

The physical field is 3:2, but X and Y are normalized independently and mapped onto the square software pad. The bridge applies a 10% inverse margin correction, expanding the reachable physical range around `0.1..0.9` to the software range `0.0..1.0`.

## App Protocol

The app does not open the serial port. `ControllerReceiver` listens on `127.0.0.1:45454` for lightweight UDP packets from `ControllerBridge`:

```text
x,y,touch,total
```

Every 80 Hz sample updates the audio-side pad state. GUI drawing is downsampled to the app's 20 Hz timer and uses only the latest received sample, keeping audio control responsive while avoiding excessive repaint work.
