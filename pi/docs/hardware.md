# InkDeck for Raspberry Pi: hardware

## Display

The Waveshare 7.5 inch e-Paper HAT (V2, 800 × 480) plugs onto the Pi's
40-pin header and uses SPI. InkDeck shows it in portrait (rotated 90°); change
`display.rotation` in `config/local.yaml` if your panel is mounted the other
way. Page turns use the panel's partial refresh; a full refresh every few page
turns (10 by default, set in the web interface) clears ghosting.

## Button

One push button between **GPIO 5 (physical pin 29)** and **GND (pin 30)**.
The internal pull-up is used, so no resistor is needed. The pin, debounce and
long-press time are under `buttons` in `config/config.yaml`.

## Battery

### PiSugar 2 (recommended)

The PiSugar 2 provides the battery, charging, a battery gauge, a real-time
clock and a button. `scripts/install.sh` installs its power manager
(`pisugar-server`, with a web interface on port 8421). InkDeck finds it
automatically and shows the battery level on every screen. Check that it is
present with `sudo i2cdetect -y 1`: it appears at 0x75 and 0x32.

To use its button with InkDeck, see "PiSugar 2 button" in the
[README](../README.md#using-it).

### ADS1115 (do it yourself)

Without a PiSugar, an ADS1115 ADC can measure a single-cell lithium battery
through a 2:1 divider made of two 10 kΩ resistors:

```
Battery+ ──[10 kΩ]──┬──[10 kΩ]── Battery− / GND
                    └── ADS1115 A0
```

| ADS1115 | Raspberry Pi |
| --- | --- |
| VDD | 3.3 V (pin 1) |
| GND | GND (pin 6) |
| SCL | GPIO 3 (pin 5) |
| SDA | GPIO 2 (pin 3) |
| ADDR | GND (address 0x48) |

Measure the battery itself, not the boost converter's 5 V output. Install the
driver with `pip3 install --break-system-packages adafruit-circuitpython-ads1x15`.
The channel, divider ratio and empty and full voltages are under `battery` in
`config/config.yaml`. An ADC cannot tell whether the battery is charging.

## Power

InkDeck saves power on its own: it takes the extra CPU cores offline and turns
Wi-Fi off while a book is open, and can sleep after a period without input
(Settings in the web interface).

`sudo ./scripts/setup_power_optimization.sh` adds boot-level savings to
`/boot/firmware/config.txt` (about 50 mA in all), based on
[Kitten Labs' Pi boot optimization](https://kittenlabs.de/blog/2024/09/01/extreme-pi-boot-optimization/):

- HDMI off (about 14 mA), audio off (8 mA), LEDs off (3 mA).
- Bluetooth off (25 mA). A Bluetooth keyboard then cannot connect; remove
  `dtoverlay=disable-bt` from `config.txt` to use one.
- Unused hardware probing and services off.

It backs up `config.txt` first; restore the backup and reboot to undo it.

### CPU cores and undervolting

The web interface's **Settings → CPU power** sets two experimental options,
both applied at the next reboot:

- **CPU cores at boot** (`maxcpus` in `cmdline.txt`). On a Pi Zero 2 W,
  about 2 W with 4 cores, 1.5 W with 2 and 1 W with 1, from
  [Jeff Geerling's measurements](https://www.jeffgeerling.com/blog/2021/disabling-cores-reduce-pi-zero-2-ws-power-consumption-half).
  With fewer cores at boot, InkDeck cannot bring more online later.
- **Undervolt** (`over_voltage` in `config.txt`): each step is 25 mV lower,
  down to −8. Forum testing on a Pi Zero 2 W measured 2.07 W at stock and
  1.06 W at −8. Too low a value can stop the Pi from booting; to recover, put
  the SD card in a computer and remove the `over_voltage` line from
  `config.txt`. Start at −2 and test stability, for example with
  `stress-ng --cpu 4 --timeout 600s`, before going lower.

Both helpers back up the file they change (`.inkdeck-backup`).

### Memory

The Pi Zero 2 W has 512 MB of RAM. If very large books run out of memory,
`./scripts/setup_zram.sh` adds compressed swap in RAM.
