# ESPHome LED Matrix

A scene-driven [ESPHome](https://esphome.io/) setup for HUB75 LED matrix panels.
One device renders a clock, weather, icons, messages, sensor values and seasonal animations — all switchable at runtime from Home Assistant, no reflashing required.

## Hardware

- 64×32 HUB75 RGB matrix panel (P3 pitch)
- ESP32 dev board (config targets `nodemcu-32s`, ESP-IDF framework)
- Standard HUB75 pinout: R1/G1/B1 the top half's colour lines, R2/G2/B2 the bottom half, A–E the row pair, CLK/LAT/OE shift clock, latch and output enable

Panel geometry is `PANEL_W`/`PANEL_H` in `matrix_scenes.h`, and every scene derives from it.
A chained or larger build is one edit there plus `layout_cols`/`layout_rows` and `layout:`.

## Files

| Path                                    | What                                                     |
| --------------------------------------- | -------------------------------------------------------- |
| `led-matrix.yaml`                       | ESPHome config: display, entities, one short draw lambda |
| `matrix_scenes.h`                       | Scene renderer, one C++ function per scene               |
| `components/virtual_light`              | Local component: an RGB light with no hardware           |
| `art/`                                  | Chroma-keyed GIF sprites for the seasonal scenes         |
| `icons/font-icons.yaml`                 | 101 curated MDI glyphs, baked as a font                  |
| `icons/materialdesignicons-webfont.ttf` | Vendored MDI webfont (Apache 2.0)                        |

## How it works

`led-matrix.yaml` has a `display:` with one short lambda; the scenes live in `matrix_scenes.h`, pulled in via `esphome: includes:`.
That copies the header next to `main.cpp` and `#include`s it after the generated globals, which is why the entity names in the header _are_ this file's ids, and why the scene code is real C++ with a compiler and a linter behind it instead of a 900-line YAML block scalar.

## Rendering rules

Three rules every scene obeys:

- **Centre on ink, not line boxes.** `Font::measure()` returns ink width _plus_ the left bearing, so using it as a pen advance drifts letters apart and inflates the box; the glyph tables are read directly instead (see the `text_layout` cache).
- **Keep particles out of the sprite box.** Sprites are chroma-keyed, so their transparent pixels are not drawn, and anything rendered behind one shows through its gaps and reads as flicker.
  `inside_sprite_box()` is the single place that rule lives.
  Motion around a sprite goes _before_ the sprite (which occludes it), never inside its 32×32 box.
- **Glyphs are baked at compile time.** A codepoint outside the font's own character set draws nothing and logs `Codepoint ... not found in font`.

## Why the odd choices

**Colour pickers via `light`.** ESPHome has no colour entity type — `color:` is compile-time only, and `light` normally drives an LED strip.
A `light` is the only entity the API can carry RGB in, so `components/virtual_light` is an RGB light with no hardware behind it.
Home Assistant then renders a real colour wheel and the state lives on the device.

**`RESTORE_DEFAULT_ON`, not `RESTORE_AND_ON`.** The latter reads like "restore the state" but emits an unconditional `state = true` in the restore callback, so turning a colour light off and rebooting brings it back on.
`RESTORE_DEFAULT_ON` restores faithfully and only defaults to on when nothing was ever stored.

**Brightness curve.** The driver remaps through a curve with a floor that keeps bit-plane ratios intact, so on this panel 1% arrives as effective 19/255 (~7% duty).
1%, 2% and 3% are indistinguishable: the low end is compressed, not linear.
`min_value: 1` because 0 means "off" and belongs to the switch.

**Brightness 0 is a true off.** The driver blanks the OE line and stops pushing pixels, so the panel draws no current and does not glow in the dark.
`on_boot` re-applies brightness at priority 390 because the driver forces 255 in its own setup at 400, discarding the restored value; without it the panel flashes at 100% on every boot.
Priority runs high-to-low, so 390 is after the panel and before WiFi at 250.

**`auto_clear_enabled: false`.** The lambda fills the panel with the background colour every frame, so the component's own clear would only blank it first and then be painted over.
Turning it off removes a full framebuffer clear (every row × every bit plane) from each of the ~33 redraws a second.

**Display timing.** `update_interval: 30ms` is ~33 redraws/s; much below 20ms fights the DMA refresh.
`bit_depth: 6` gives 64 levels per channel in comfortable RAM (8 needs more buffer memory and costs refresh rate).
`double_buffer: true` draws into a back buffer and flips, so no half-drawn frame.

**Fonts.** Four sizes × two weights, baked as 1-bit glyphs.
`size:` is the line-box height, not the capital height — a size 24 face draws ~17px capitals, which is why X-Large still fits a 32px panel.
Small uses `@black` because `@bold` is identical to regular at 10px; the larger sizes use `@bold` because `@black` smears at one bit per pixel.
Umlauts are listed explicitly because the default `GF_Latin_Kernel` glyphset has none, which rendered "März" as "Mrz".
Note that `glyphs:` _replaces_ the default set, so the glyphset has to be named alongside it.
No emoji, deliberately: Noto Emoji is an outline face, so it renders monochrome on an RGB panel, and colour emoji only exist as COLR/SVG or bitmap fonts, which ESPHome cannot rasterise into a 1-bit glyph table.
The clock and temperature use Roboto Mono, so every digit advances the same em and neither twitches as values change.

**Hand-drawn colon and degree.** At 20px the font rasterises the colon as a 3×3 smear per dot and `°` as a ragged 6×5 blob, so the clock draws two 2×2 rectangles and the weather scene a 4×4 ring.

**Sprites.** GIFs are split into 1-bit frames at compile time; each 32×32 frame is 128 bytes of flash and nothing in RAM.
`animation.next_frame` rewrites the image's data pointer on a 100ms interval (~10fps), so tempo is independent of the 30ms redraw.
To swap art, drop a GIF in `art/` and keep `resize: 32x32` and `transparency: chroma_key`.

**Weather input.** The condition comes from a `weather.*` entity, so any integration works.
The entity ID is a property of _your_ Home Assistant, not this firmware: if it is wrong, HA sends nothing, the state stays empty and the scene hides its icon.
The log names the problem:

```text
[W][panel.weather]: no usable weather state (''); hiding icon
```

Empty quotes there mean the subscription never delivered — check the ID in Developer Tools › States.
The temperature reads an outdoor _sensor_, deliberately not the weather entity's forecast attribute: it is measured at the house rather than predicted for a region.

## Home Assistant entities

| Entity                                                | Purpose                                                                                      |
| ----------------------------------------------------- | -------------------------------------------------------------------------------------------- |
| `select.Panel Scene`                                  | Clock / Weather / Icon / Message / Sensor / Halloween / Winter / Ghost / Birthday / New Year |
| `text.Panel Message`                                  | Free text for the Message scene                                                              |
| `text.Panel Sensor Label` / `text.Panel Sensor Value` | Caption and value for the Sensor scene, pushed from HA                                       |
| `select.Panel Icon`                                   | One of 101 curated MDI icons, centred                                                        |
| `select.Panel Text Size`                              | Small / Medium / Large / X-Large                                                             |
| `select.Panel Font Weight`                            | Normal / Bold                                                                                |
| `number.Panel Brightness`                             | 1–100 %                                                                                      |
| `switch.Panel Power`                                  | On / off                                                                                     |
| `light.Background Colour` / `light.Text Colour`       | Colour wheels for background and glyphs                                                      |

## Usage

Copy all files into a single folder in ESPHome Device Builder — `!include` and `includes:` resolve against the config directory, so `led-matrix.yaml`, `matrix_scenes.h`, `icons/`, `art/` and `components/` must sit next to each other.
Copy the whole `icons/` folder, not just the YAML: it holds the vendored `.ttf` the icon config bakes its glyphs from.
Set the `led_matrix__encryption_key` secret, then build and flash.
