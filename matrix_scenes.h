#pragma once

// =============================================================================
// Renderer for the 64x32 HUB75 panel — one function per scene.
//
// Pulled out of the `display: lambda:` so the scene code is real C++ a compiler
// and a linter can check. Included from led-matrix.yaml via `esphome:
// includes:`, which copies it next to main.cpp and #includes it after the
// generated globals, so the entity names below ARE the YAML ids. It must
// therefore sit next to led-matrix.yaml in Device Builder, like `icons/`,
// `art/` and `components/`.
//
// Layout rules and the reasoning behind the measured constants: see README.md.
// =============================================================================

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "esphome/components/display/display.h"
#include "esphome/components/font/font.h"
#include "esphome/components/image/image.h"
#include "esphome/components/light/light_state.h"
#include "esphome/components/select/select.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text/text.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/components/unicode/unicode.h"
#include "esphome/core/color.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/core/string_ref.h"
#include "esphome/core/time.h"

namespace esphome::panel {

// Panel geometry. Every layout derives from these two, so a chained or larger
// build is one edit here rather than a search for stray literals.
constexpr int PANEL_W = 64;
constexpr int PANEL_H = 32;

// Sprites are chroma-keyed: anything drawn inside their box shows through the
// transparent gaps and reads as flicker. One test, so no scene gets it wrong.
constexpr int SPRITE_W = 32;
constexpr int SPRITE_X = (PANEL_W - SPRITE_W) / 2;

inline bool inside_sprite_box(int x, int y) {
  return y >= 0 && y < PANEL_H && x >= SPRITE_X && x < SPRITE_X + SPRITE_W;
}

// Animation clock: counts redraws, not milliseconds. One step every 3 frames at
// 30ms is ~90ms, smooth for snow and fast enough for flapping wings.
constexpr uint8_t ANIM_EVERY = 3;
constexpr uint8_t ANIM_STEPS = 64;

inline uint16_t animation_frame = 0;
inline uint8_t animation_step = 0;

inline void advance_animation() {
  animation_frame++;
  if (animation_frame % ANIM_EVERY == 0) {
    if (++animation_step >= ANIM_STEPS)
      animation_step = 0;
  }
}

// The colours live in the two light entities, so they survive a reboot and the
// picker lives in Home Assistant. A light reports "off" until a colour has been
// picked, which is what the fallbacks are for.
inline Color light_state_color(light::LightState *state, Color fallback) {
  if (state == nullptr || !state->current_values.is_on())
    return fallback;
  float red = 0.0f;
  float green = 0.0f;
  float blue = 0.0f;
  // Gamma-corrected 0..1, so the wheel matches what a real LED would emit.
  state->current_values_as_rgb(&red, &green, &blue);
  return Color(static_cast<uint8_t>(red * 255.0f + 0.5f), static_cast<uint8_t>(green * 255.0f + 0.5f),
               static_cast<uint8_t>(blue * 255.0f + 0.5f));
}

// -----------------------------------------------------------------------------
// Scene 1 — Clock.
//
// 24-hour: 16:00 is unambiguous in a way 4:00 PM is not.
// -----------------------------------------------------------------------------
inline void draw_clock(display::Display &it, Color foreground) {
  const auto now = ha_time->now();

  char time_buf[8];
  char date_buf[16];
  if (now.is_valid()) {
    // %H is 00-23, so no AM/PM and no locale surprises.
    snprintf(time_buf, sizeof(time_buf), "%02u:%02u", (unsigned) now.hour, (unsigned) now.minute);
    // day_of_week is 1-7 with sunday = 1, so index it with -1. Months are
    // spelled short but not abbreviated: the widest case, "Mo 31. März", is
    // 59px of the 64px panel in the condensed 12px date face.
    static const char *const WEEKDAYS[] = {"So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};
    static const char *const MONTHS[] = {"Jan",  "Feb", "März", "Apr", "Mai", "Juni",
                                         "Juli", "Aug", "Sept", "Okt", "Nov", "Dez"};
    // An unsynced clock can report month 0, which would index before the array.
    const unsigned month_index = now.month >= 1 && now.month <= 12 ? now.month - 1 : 0;
    snprintf(date_buf, sizeof(date_buf), "%s %u. %s", WEEKDAYS[now.day_of_week - 1], (unsigned) now.day_of_month,
             MONTHS[month_index]);
  } else {
    snprintf(time_buf, sizeof(time_buf), "--:--");
    snprintf(date_buf, sizeof(date_buf), "warte...");
  }

  // Every number below is a measured glyph metric, not a guess:
  //
  //   Roboto Mono 20   digits 15px high in a 12px advance, ink 6-10px wide,
  //                    offset_y 6. Medium weight: bold rasterises '8' with a
  //                    lower loop 1px wider than its upper, so the digit steps
  //                    out past its neighbours; regular fixes that but drops to
  //                    1px hairlines on 0/4/5/9. See README.md.
  //   Roboto Condensed 12   capheight 9, baseline 12, so the capital band is
  //                         offset_y 3..11. Descenders (Apr, Aug, Sept) hang
  //                         below that baseline, which is what a real clock
  //                         face does; the caps stay put day to day.
  //
  // The date has its own face (font_date) because Roboto at 10px cannot draw a
  // round zero, and 12px Roboto is too wide for "Mo 31. März". See README.md.
  //
  // The two blocks are laid out as ONE group centred in the 32px panel, each
  // offset by its own ink top. The date is pinned to its cap band rather than
  // to whatever ink today's string happens to have, so the time never moves
  // and only genuine descenders reach lower.
  //
  // Gap 2: digit ink (15) + caps (9) is even, so an even gap keeps the group
  // even and the top/bottom split exact. 15 + 2 + 9 = 26 leaves 3px above and
  // 3px below, with descenders using one of the lower rows.
  const font::Glyph *digit_glyph = font_clock->find_glyph((uint32_t) '0');
  const int cell = digit_glyph != nullptr ? digit_glyph->advance : 12;
  const int ink_top = digit_glyph != nullptr ? digit_glyph->offset_y : 6;
  const int ink_h = digit_glyph != nullptr ? digit_glyph->height : 15;

  font::Font *date_font = font_date;
  const int date_cap_top = date_font->get_baseline() - date_font->get_capheight();
  const int date_cap_h = date_font->get_capheight();

  const int gap = 2;
  const int group_h = ink_h + gap + date_cap_h;
  const int group_top = (PANEL_H - group_h) / 2;
  const int time_y = group_top - ink_top;
  const int date_y = group_top + ink_h + gap - date_cap_top;

  // Horizontal layout. The four digits are monospaced, so they are laid out on
  // a fixed grid of cells rather than on ink: every glyph draws inside its own
  // 12px cell, so centring the grid centres the time for every value.
  const int colon_w = 2;
  const int colon_gap = 2;
  const int grid_w = 4 * cell + 2 * colon_gap + colon_w;
  const int grid_x = (PANEL_W - grid_w) / 2;
  const int time_x = grid_x;
  const int colon_x = grid_x + 2 * cell + colon_gap;
  const int right_pen = colon_x + colon_w + colon_gap;

  char digit_buf[2] = {'\0', '\0'};
  digit_buf[0] = time_buf[0];
  it.print(time_x, time_y, font_clock, foreground, display::TextAlign::TOP_LEFT, digit_buf);
  digit_buf[0] = time_buf[1];
  it.print(time_x + cell, time_y, font_clock, foreground, display::TextAlign::TOP_LEFT, digit_buf);
  digit_buf[0] = time_buf[3];
  it.print(right_pen, time_y, font_clock, foreground, display::TextAlign::TOP_LEFT, digit_buf);
  digit_buf[0] = time_buf[4];
  it.print(right_pen + cell, time_y, font_clock, foreground, display::TextAlign::TOP_LEFT, digit_buf);

  // Two 2x2 dots instead of the font's colon, which at 20px rasterises as a
  // ragged 4x3 blob per dot and reads as a smear. They sit at the thirds of the
  // digit ink, blinking 1s on / 1s off; an unsynced clock stays solid.
  if (!now.is_valid() || ((millis() / 1000) % 2 == 0)) {
    const int upper_y = time_y + ink_top + ink_h / 3 - 1;
    const int lower_y = time_y + ink_top + (2 * ink_h) / 3 - 1;
    it.filled_rectangle(colon_x, upper_y, colon_w, 2, foreground);
    it.filled_rectangle(colon_x, lower_y, colon_w, 2, foreground);
  }

  it.printf(PANEL_W / 2, date_y, date_font, foreground, display::TextAlign::TOP_CENTER, "%s", date_buf);
}

// -----------------------------------------------------------------------------
// Scene 2 — Weather.
//
// Icon and temperature as ONE centred group, so the pair does not drift as the
// condition and value change width (the icon spans 15-19px, the value 9-32px).
//
// Measured, not guessed:
//
//   MDI 26               sunny 19x21  cloudy 24x15  snowy 24x23  rainy 24x21,
//                        offset_x 1-4, so ink is placed by cancelling bearings
//   Roboto Mono 20       digit ink 6-10px, offset_y 6, uniform 12px advance
//
// font_clock keeps the digits monospaced: proportional digits would re-space
// the pair on every update. The degree is hand-drawn, the same reason the
// clock's colon is rectangles.
// -----------------------------------------------------------------------------
inline void draw_weather(display::Display &it, Color foreground) {
  const std::string &condition = weather_condition->state;

  // An empty state means HA has not sent it yet; unavailable/unknown are HA's
  // own non-weather states. Drawing the cloudy fallback for those would
  // silently lie about the sky.
  const bool have_condition = !condition.empty() && condition != "unavailable" && condition != "unknown";

  // ~33 calls a second, so log only on change or the API log floods.
  static std::string logged_condition = "\x01";
  if (condition != logged_condition) {
    logged_condition = condition;
    if (have_condition) {
      ESP_LOGI("panel.weather", "condition '%s'", condition.c_str());
    } else {
      ESP_LOGW("panel.weather", "no usable weather state ('%s'); hiding icon", condition.c_str());
    }
  }

  const char *glyph = "";
  if (!have_condition) {
    glyph = "";
  } else if (condition == "sunny") {
    glyph = "\U000F0599";
  } else if (condition == "partlycloudy") {
    glyph = "\U000F0595";
  } else if (condition == "rainy" || condition == "pouring") {
    glyph = "\U000F0597";
  } else if (condition == "snowy" || condition == "snowy-rainy") {
    glyph = "\U000F0598";
  } else if (condition == "fog") {
    glyph = "\U000F0591";
  } else if (condition == "lightning" || condition == "lightning-rainy") {
    glyph = "\U000F067E";
  } else if (condition == "clear-night") {
    glyph = "\U000F0594";
  } else if (condition == "windy" || condition == "windy-variant") {
    glyph = "\U000F059D";
  } else if (condition == "hail") {
    glyph = "\U000F0598";
  } else if (condition == "cloudy") {
    glyph = "\U000F0590";
  } else if (condition == "exceptional") {
    glyph = "\U000F0590";
  } else {
    // A state this table does not cover. Name it in the log: an unknown string
    // is the difference between "the panel is wrong" and "HA is reporting
    // something new".
    glyph = "\U000F0590";
  }

  // Digits and sign only; the degree is drawn separately. Clamped to -99..99:
  // the widest the panel holds, and what lets the buffer be provably large
  // (an unclamped "%.0f" lets the compiler assume up to 309 digits).
  char value_buf[8];
  if (outside_temperature->has_state()) {
    float value = outside_temperature->state;
    if (value < -99.0f)
      value = -99.0f;
    else if (value > 99.0f)
      value = 99.0f;
    snprintf(value_buf, sizeof(value_buf), "%.0f", value);
  } else {
    snprintf(value_buf, sizeof(value_buf), "--");
  }

  // Ink width of the digits: total advance, less the first glyph's left bearing
  // and the last glyph's right bearing. Without that correction the '-' and the
  // '1' leave gaps at the ends and the group looks shifted towards the icon.
  int digits_advance = 0;
  int digits_left = 0;
  int digits_right = 0;
  bool have_digit = false;
  for (size_t i = 0; value_buf[i] != '\0'; i++) {
    const font::Glyph *value_glyph = font_clock->find_glyph((uint32_t) (uint8_t) value_buf[i]);
    if (value_glyph == nullptr)
      continue;
    digits_advance += value_glyph->advance;
    if (!have_digit) {
      digits_left = value_glyph->offset_x;
      have_digit = true;
    }
    digits_right = value_glyph->advance - value_glyph->offset_x - value_glyph->width;
  }
  const int digits_ink = digits_advance - digits_left - digits_right;

  // Widest case measured from the generated glyph tables: "-99" is 31px of
  // digit ink, so 31 + 2 + 4 = 37 with the ring, plus the widest weather icon
  // (24px) and a 2px gutter gives 63 of the 64px panel. "100" would need the
  // whole 64px with nothing to spare, which is why the clamp is -99..99.
  const int DEGREE_W = 4;
  const int DEGREE_GAP = 2;
  const int ICON_GAP = 2;
  const int value_ink = digits_ink + DEGREE_GAP + DEGREE_W;

  size_t glyph_len = 0;
  const uint32_t glyph_cp = unicode::extract_unicode_codepoint(glyph, &glyph_len);
  const font::Glyph *icon_glyph = glyph_len > 0 ? font_icons->find_glyph(glyph_cp) : nullptr;
  const bool have_icon = icon_glyph != nullptr;
  const int icon_w = have_icon ? icon_glyph->width : 0;
  const int icon_gap = have_icon ? ICON_GAP : 0;

  const int group_w = icon_w + icon_gap + value_ink;
  const int group_left = (PANEL_W - group_w) / 2;

  // Ink placement: offset_x is the glyph's left bearing, so subtracting it puts
  // the ink itself at group_left. Without that, the 0-3px bearings make the
  // icons drift a pixel or two against each other as the condition changes.
  // Vertically the same: ink heights run 11-17px, so centring the ink box is
  // the only way the row looks level.
  //
  // No icon means no state yet: the temperature is then centred on its own,
  // which reads as "waiting" rather than as a wrong forecast.
  if (have_icon) {
    const int icon_x = group_left - icon_glyph->offset_x;
    const int icon_y = (PANEL_H - icon_glyph->height) / 2 - icon_glyph->offset_y;
    it.print(icon_x, icon_y, font_icons, foreground, display::TextAlign::TOP_LEFT, glyph);
  }

  const font::Glyph *value_zero = font_clock->find_glyph((uint32_t) '0');
  const int value_ink_top = value_zero != nullptr ? value_zero->offset_y : 6;
  const int value_ink_h = value_zero != nullptr ? value_zero->height : 15;
  // Same vertical centring the Clock uses: (panel - ink) / 2 minus the top
  // bearing, which lands the digits on ink rows 8..22.
  const int value_y = (PANEL_H - value_ink_h) / 2 - value_ink_top;

  const int digits_x = group_left + icon_w + icon_gap;
  int pen = digits_x - digits_left;
  char digit_buf[2] = {'\0', '\0'};
  for (size_t i = 0; value_buf[i] != '\0'; i++) {
    const font::Glyph *value_glyph = font_clock->find_glyph((uint32_t) (uint8_t) value_buf[i]);
    if (value_glyph == nullptr)
      continue;
    digit_buf[0] = value_buf[i];
    it.print(pen, value_y, font_clock, foreground, display::TextAlign::TOP_LEFT, digit_buf);
    pen += value_glyph->advance;
  }

  // Degree as a 4x4 ring with a 2x2 hole, top-aligned with the digit cap band
  // the way a real '°' is. The font's own '°' is a ragged 6x5 blob.
  const int degree_x = digits_x + digits_ink + DEGREE_GAP;
  const int degree_y = (PANEL_H - value_ink_h) / 2;
  static const uint8_t DEGREE[4][4] = {
      {0, 1, 1, 0},
      {1, 0, 0, 1},
      {1, 0, 0, 1},
      {0, 1, 1, 0},
  };
  for (int dy = 0; dy < 4; dy++) {
    for (int dx = 0; dx < 4; dx++) {
      if (DEGREE[dy][dx] != 0)
        it.draw_pixel_at(degree_x + dx, degree_y + dy, foreground);
    }
  }
}

// -----------------------------------------------------------------------------
// Scene 3 — Icon: one MDI glyph, centred on its ink box. MDI artwork has ~2px
// of padding built in, so em-square centring looks off.
// -----------------------------------------------------------------------------
inline void draw_icon(display::Display &it, Color foreground) {
  // Ordered to match `Panel Icon` in led-matrix.yaml: the select's option index
  // IS this table's index, so an icon is appended to both in the same place.
  static const uint32_t ICON_CODEPOINTS[] = {
    0x000F0004, 0x000F0849, 0x000F000E, 0x000F001D, 0x000F0020, 0x000F0026, 0x000F0028, 0x000F0029,
    0x000F0E7D, 0x000F0079, 0x000F0084, 0x000F02E3, 0x000F009A, 0x000F009E, 0x000F00A3, 0x000F00E2,
    0x000F00E7, 0x000F00E9, 0x000F00ED, 0x000F05E2, 0x000F010A, 0x000F010B, 0x000F0110, 0x000F0114,
    0x000F011C, 0x000F05E0, 0x000F0954, 0x000F0150, 0x000F0159, 0x000F0176, 0x000F0FEF, 0x000F01AD,
    0x000F0AAC, 0x000F081B, 0x000F081C, 0x000F01E6, 0x000F0290, 0x000F0297, 0x000F0298, 0x000F02A0,
    0x000F0E44, 0x000F0876, 0x000F1821, 0x000F02CB, 0x000F02D1, 0x000F05F6, 0x000F02D7, 0x000F0649,
    0x000F064D, 0x000F02FC, 0x000F0306, 0x000F0322, 0x000F032A, 0x000F033E, 0x000F06EE, 0x000F034E,
    0x000F06EF, 0x000F0381, 0x000F075A, 0x000F03D3, 0x000F1056, 0x000F0402, 0x000F0405, 0x000F0830,
    0x000F0422, 0x000F06A5, 0x000F0BBF, 0x000F0824, 0x000F070D, 0x000F070E, 0x000F046F, 0x000F048B,
    0x000F0565, 0x000F09A0, 0x000F0A70, 0x000F04B2, 0x000F0717, 0x000F04B7, 0x000F04B9, 0x000F04C3,
    0x000F04CE, 0x000F04DA, 0x000F0502, 0x000F050F, 0x000F13AB, 0x000F09AB, 0x000F052C, 0x000F054A,
    0x000F072A, 0x000F0590, 0x000F0591, 0x000F067E, 0x000F0594, 0x000F0595, 0x000F0596, 0x000F0597,
    0x000F0598, 0x000F0599, 0x000F059D, 0x000F05A9, 0x000F05B1,
  };
  // active_index() avoids string comparison, so renaming an option cannot break
  // the lookup.
  const auto selected = panel_icon->active_index();  if (!selected.has_value() || !panel_icon->has_index(selected.value()))
    return;

  const uint32_t codepoint = ICON_CODEPOINTS[selected.value()];
  const font::Glyph *glyph = font_icons->find_glyph(codepoint);
  if (glyph == nullptr)
    return;

  // Fonts are addressed by UTF-8, and every MDI icon is above U+FFFF, hence
  // always 4 bytes.
  char utf8[5];
  utf8[0] = (char) (0xF0 | ((codepoint >> 18) & 0x07));
  utf8[1] = (char) (0x80 | ((codepoint >> 12) & 0x3F));
  utf8[2] = (char) (0x80 | ((codepoint >> 6) & 0x3F));
  utf8[3] = (char) (0x80 | (codepoint & 0x3F));
  utf8[4] = '\0';

  // Centre on the ink box, not the em square.
  const int x = (PANEL_W - glyph->width) / 2 - glyph->offset_x;
  const int y = (PANEL_H - glyph->height) / 2 - glyph->offset_y;
  it.print(x, y, font_icons, foreground, display::TextAlign::TOP_LEFT, utf8);
}

// -----------------------------------------------------------------------------
// Scenes 4 and 5 — Message and Sensor share the text renderer.
//
// Sensor is Message plus a caption. HA pushes both strings with text.set_value,
// so any sensor can be shown with no reflash.
//
// Measured, not guessed:
//
//   * Ink, not line boxes. Font::measure() returns ink PLUS the left bearing,
// so     it drifts letters when used as a pen advance; the glyph table is read
//     directly instead.
//   * Tracking scales with font height, so it is not a constant.
//   * Text that fits is centred; text that does not scrolls 1px per frame, with
//     tracking walked back to 0 first so bold does not scroll on its own.
// -----------------------------------------------------------------------------

// Per-character glyph walks are too slow at 33fps, so measure once and cache it
// until an input changes.
struct TextLayout {
  std::vector<int> advances;
  std::vector<uint16_t> byte_offset;
  std::vector<uint8_t> byte_length;
  int ink_min = 0;
  int ink_width = 0;
  int ink_top = 0;
  int ink_bottom = 0;
  int tracking = 0;
  int scroll_x = 0;
  // True when every codepoint moves the pen, which makes one `it.print` of the
  // whole string draw exactly what the per-glyph loop would.
  bool all_advance = false;
};

inline TextLayout text_layout;
// The inputs the cache was measured for. `font` is part of the key because Bold
// at 10px substitutes a different face, so size+weight alone could cache a
// measurement taken with the wrong glyphs.
inline const font::Font *text_layout_font = nullptr;
inline std::string text_layout_text;
inline std::string text_layout_size;
inline std::string text_layout_weight;

inline void draw_text(display::Display &it, Color foreground, const StringRef &size, const StringRef &weight,
                      bool sensor_mode) {
  // Both faces are kept so bold can be compared against regular below.
  // To add a size: add a font pair in the YAML and a branch here.
  font::Font *regular = font_medium;  font::Font *bold_face = font_medium_bold;
  if (size == "Small") {
    regular = font_small;
    bold_face = font_small_bold;
  } else if (size == "Large") {
    regular = font_large;
    bold_face = font_large_bold;
  } else if (size == "X-Large") {
    regular = font_xlarge;
    bold_face = font_xlarge_bold;
  }

  const bool bold = (weight == "Bold");
  font::Font *font = bold ? bold_face : regular;
  const std::string &body = sensor_mode ? panel_sensor_value->state : display_text->state;

  const int max_tracking = bold ? std::max(1, font->get_height() / 10) : 0;

  // Fake bold where the face cannot express it. Below ~12px the bold stem is
  // the same 1px wide, so ask the fonts instead of hardcoding a size: if the
  // bold 'l' is no wider than the regular one, thicken at draw time.
  int embolden = 0;
  if (bold) {
    const font::Glyph *stem_bold = bold_face->find_glyph((uint32_t) 'l');
    const font::Glyph *stem_regular = regular->find_glyph((uint32_t) 'l');
    if (stem_bold != nullptr && stem_regular != nullptr && stem_bold->width == stem_regular->width)
      embolden = 1;
  }

  // Re-measure only when an input changed. Scroll position is kept: glyph
  // positions relative to each other do not move while scrolling.
  if (font != text_layout_font || body != text_layout_text || size != text_layout_size || weight != text_layout_weight) {
    text_layout_font = font;
    text_layout_text = body;
    text_layout_size = size;
    text_layout_weight = weight;
    text_layout.scroll_x = PANEL_W;

    // Decode once, by codepoint. Iterating bytes would split any multi-byte
    // character (umlauts are 2 bytes, CJK 3) into unrelated codepoints; the
    // offsets let the draw loop hand the font the original UTF-8 without
    // decoding again.
    std::vector<const font::Glyph *> glyphs;
    text_layout.byte_offset.clear();
    text_layout.byte_length.clear();
    for (size_t byte_index = 0; byte_index < body.length();) {
      size_t length = 0;
      const uint32_t codepoint = unicode::extract_unicode_codepoint(body.c_str() + byte_index, &length);
      if (length == 0)
        break;
      text_layout.byte_offset.push_back((uint16_t) byte_index);
      text_layout.byte_length.push_back((uint8_t) length);
      glyphs.push_back(font->find_glyph(codepoint));
      byte_index += length;
    }

    const size_t count = glyphs.size();
    text_layout.advances.assign(count, 0);

    for (text_layout.tracking = max_tracking;; text_layout.tracking--) {
      int pen = 0;
      int first_ink = 0;
      int last_ink = 0;
      int top = 0;
      int bottom = 0;
      bool has_ink = false;
      bool has_vertical = false;

      for (size_t i = 0; i < count; i++) {
        const font::Glyph *glyph = glyphs[i];
        if (glyph == nullptr) {
          // Unknown codepoints are drawn as a box one default advance wide.
          if (!font->get_glyphs().empty())
            text_layout.advances[i] = font->get_glyphs()[0].advance;
          pen += text_layout.advances[i] + text_layout.tracking;
          continue;
        }

        text_layout.advances[i] = glyph->advance;

        if (glyph->width > 0) {
          // offset_x + width is the exact ink; `advance` is the true pen
          // movement. Adding bearings to the advance would drift letters.
          const int left = pen + glyph->offset_x;
          const int right = left + glyph->width;
          if (!has_ink) {
            first_ink = left;
            last_ink = right;
            has_ink = true;
          } else {
            first_ink = std::min(first_ink, left);
            last_ink = std::max(last_ink, right);
          }

          if (!has_vertical) {
            top = glyph->offset_y;
            bottom = glyph->offset_y + glyph->height;
            has_vertical = true;
          } else {
            top = std::min(top, glyph->offset_y);
            bottom = std::max(bottom, glyph->offset_y + glyph->height);
          }
        }

        pen += glyph->advance + text_layout.tracking;
      }

      text_layout.ink_min = first_ink;
      text_layout.ink_width = last_ink - first_ink + embolden;
      text_layout.ink_top = top;
      text_layout.ink_bottom = bottom;

      if (text_layout.ink_width <= PANEL_W || text_layout.tracking == 0)
        break;
    }

    text_layout.all_advance =
        std::all_of(text_layout.advances.begin(), text_layout.advances.end(), [](int advance) { return advance > 0; });
  }

  // Sensor mode stacks a caption above the value, so the value is centred in
  // the rows BELOW it. The caption's lowest ink is row 12 (the 'p' in
  // "Temperature"), so row 13 is the first free row.
  const int caption_reserve = sensor_mode ? 13 : 0;
  const int text_y = (PANEL_H - caption_reserve - (text_layout.ink_bottom - text_layout.ink_top)) / 2 +
                     caption_reserve - text_layout.ink_top;

  if (sensor_mode)
    it.printf(PANEL_W / 2, 1, font_small, foreground, display::TextAlign::TOP_CENTER, "%.20s",
              panel_sensor_label->state.c_str());

  const bool scrolling = text_layout.ink_width > PANEL_W;
  int pen;
  if (scrolling) {
    text_layout.scroll_x--;
    // ink_min is negative when the first glyph has a left bearing; the text
    // must travel that much further before its right edge clears.
    if (text_layout.scroll_x < -(text_layout.ink_width + text_layout.ink_min))
      text_layout.scroll_x = PANEL_W;
    pen = text_layout.scroll_x - text_layout.ink_min;
  } else {
    pen = (PANEL_W - text_layout.ink_width) / 2 - text_layout.ink_min;
  }

  // Nothing per-glyph to do without tracking or culling, so one call draws the
  // whole string: one layout pass instead of one per codepoint.
  if (text_layout.tracking == 0 && !scrolling && text_layout.all_advance) {
    it.print(pen, text_y, font, foreground, display::TextAlign::TOP_LEFT, body.c_str());
    if (embolden)
      it.print(pen + 1, text_y, font, foreground, display::TextAlign::TOP_LEFT, body.c_str());
    return;
  }

  for (size_t i = 0; i < text_layout.advances.size(); i++) {
    // Print the original UTF-8 bytes: a single byte of a multi-byte codepoint
    // draws the wrong glyph.
    const bool visible = scrolling ? (pen > -8 && pen < PANEL_W) : true;
    if (text_layout.advances[i] > 0 && visible) {
      const std::string character = body.substr(text_layout.byte_offset[i], text_layout.byte_length[i]);
      it.print(pen, text_y, font, foreground, display::TextAlign::TOP_LEFT, character.c_str());
      if (embolden)
        it.print(pen + 1, text_y, font, foreground, display::TextAlign::TOP_LEFT, character.c_str());
    }
    pen += text_layout.advances[i] + text_layout.tracking;
  }
}

// -----------------------------------------------------------------------------
// Scene 6 — Halloween: pumpkin GIF plus two bats.
//
// The bats are primitives because they cross the whole panel; a sprite that
// wide would cost far more than two triangles. Bats first, so the pumpkin is
// in front.
// -----------------------------------------------------------------------------
inline void draw_halloween(display::Display &it, Color foreground) {
  for (int b = 0; b < 2; b++) {
    // Wraps across a span wider than the panel, so a bat is briefly off-screen
    // before re-entering instead of popping in.
    const int span = PANEL_W + 16;
    const int step = animation_step * 2;
    const int bx = (b == 0) ? (step % span) - 8 : 55 - (step % span);
    const int by = 4 + b * 3 + ((animation_step / 2 + b) % 3);
    const bool wings_up = ((animation_step + b) % 4) < 2;

    it.filled_circle(bx, by, 1, foreground);
    if (wings_up) {
      it.line(bx - 1, by, bx - 5, by - 3, foreground);
      it.line(bx + 1, by, bx + 5, by - 3, foreground);
    } else {
      it.line(bx - 1, by + 1, bx - 5, by + 4, foreground);
      it.line(bx + 1, by + 1, bx + 5, by + 4, foreground);
    }
  }

  it.image(SPRITE_X, 0, art_pumpkin, foreground);
}

// -----------------------------------------------------------------------------
// Scene 7 — Winter: gift sprite with snow falling around it.
//
// Snow must never land on the sprite: with chroma_key the flakes would blink in
// and out of the artwork's gaps, reading as stutter rather than depth.
// -----------------------------------------------------------------------------
inline void draw_winter(display::Display &it, Color foreground) {
  // Each flake falls on its own speed, and every speed divides PANEL_H so the
  // field loops seamlessly. The animation cycle is ANIM_STEPS (64), so a
  // modulus that does not divide it leaves flakes that never return to their
  // starting row and the whole field jumps once per cycle.
  for (int i = 0; i < 32; i++) {
    const int col = (i * 11 + 3) % PANEL_W;
    const int speed = 1 + (i % 4 == 3 ? 3 : i % 4);  // 1, 2, 4, 4
    const int phase = (i * 7) % PANEL_H;
    const int y = (animation_step * speed + phase) % PANEL_H;
    // Horizontal drift on its own slower period, so flakes weave instead of
    // stepping diagonally. +-1px.
    const int drift = ((animation_step / 4 + i) % 8 < 4) ? 0 : 1;
    const int x = col + drift;

    if (inside_sprite_box(x, y))
      continue;

    it.draw_pixel_at(x, y, foreground);
    // Every fourth flake is a 2x2 rather than a single pixel.
    if (i % 4 == 0) {
      it.draw_pixel_at(x + 1, y, foreground);
      it.draw_pixel_at(x, y + 1, foreground);
      it.draw_pixel_at(x + 1, y + 1, foreground);
    }
  }

  // Settled snow either side of the sprite, so the drift reads as ground rather
  // than as a border under the artwork.
  for (int x = 0; x < PANEL_W; x++) {
    if (inside_sprite_box(x, PANEL_H - 1))
      continue;
    if (x % 5 != 0)
      it.draw_pixel_at(x, PANEL_H - 1, foreground);
    if (x % 3 == 0)
      it.draw_pixel_at(x, PANEL_H - 2, foreground);
  }

  // Snowman to the left of the gift, two stars above it. Drawn small and out of
  // the way, so the sprite stays the focus.
  if (animation_step % 4 < 3) {
    it.line(6, 3, 6, 7, foreground);
    it.line(4, 5, 8, 5, foreground);
  }
  if (animation_step % 4 >= 1) {
    it.line(57, 2, 57, 5, foreground);
    it.line(55, 4, 59, 4, foreground);
  }

  it.image(SPRITE_X, 2, art_gift, foreground);
}

// -----------------------------------------------------------------------------
// Scene 8 — Ghost: the sprite over drifting fog, so it is not stranded on an
// empty panel. No caption, deliberately -- one at y=20 sat on the ghost's waist
// and leaked ~180 pixels per frame through its holes.
// -----------------------------------------------------------------------------
inline void draw_ghost(display::Display &it, Color foreground) {
  // Fog bands skip the sprite box, because fog behind a chroma-keyed sprite
  // shows through its gaps.
  for (int band = 0; band < 3; band++) {
    const int y = 8 + band * 8;
    const int offset = (animation_step + band * 7) % 16;
    for (int x = 0; x < PANEL_W; x++) {
      if (inside_sprite_box(x, y))
        continue;
      if ((x + offset) % 16 < 6)
        it.draw_pixel_at(x, y, foreground);
      if ((x + offset) % 16 < 3)
        it.draw_pixel_at(x, y + 1, foreground);
    }
  }

  it.image(SPRITE_X, 0, art_ghost, foreground);
}

// -----------------------------------------------------------------------------
// Occasion scenes — Birthday and New Year, one branch so adding an occasion is
// one row plus one select option.
//
// Motion is drawn across the whole panel BEFORE the sprite: the sprite is
// opaque where it has ink, so it occludes whatever passed behind it. Drawing
// particles after the sprite, inside its 32x32 box, shows through the
// transparent gaps and reads as flickering dots on the artwork.
// -----------------------------------------------------------------------------
inline void draw_occasion(display::Display &it, Color foreground, bool birthday) {
  // "Motion in the gutters, sprite on top": the gutters are the columns either
  // side of the sprite box, which lets the two scenes share the helpers.
  display::BaseImage *sprite = birthday ? art_birthday : art_newyear;

  if (birthday) {
    // Confetti in the gutters, then a balloon rising in each one.
    //
    // The balloon sprite is 32x32 with ink at frame x=9..23, so a blit at x
    // puts ink at x+9..x+23. These offsets keep that ink clear of x=16..47.
    const int BALLOON_LEFT_X = -8;
    const int BALLOON_RIGHT_X = 39;

    // Own speed per particle so the field never lines up; every fourth is a
    // 2px flake. Before the balloons, so they pass in front rather than being
    // speckled.
    for (int i = 0; i < 12; i++) {      const bool left = (i % 2) == 0;
      const int x = left ? 1 + (i * 5 + 2) % 14 : 47 + (i * 3 + 1) % 14;
      const int y = (animation_step * (1 + i % 3) + i * 7) % 32;
      if (inside_sprite_box(x, y))
        continue;
      it.draw_pixel_at(x, y, foreground);
      if (i % 4 == 0 && !inside_sprite_box(x + 1, y))
        it.draw_pixel_at(x + 1, y, foreground);
    }

    // 64 rows of travel is one animation cycle, so a balloon always climbs and
    // never pops: 32 is fully below the panel, -32 fully above. The halves are
    // half a cycle apart so they never rise in step.
    for (int b = 0; b < 2; b++) {
      const int y = 32 - (animation_step + b * 32) % 64;
      it.image(b == 0 ? BALLOON_LEFT_X : BALLOON_RIGHT_X, y, art_balloon, foreground);
    }
  } else {
    // New Year: two fireworks in the top corners. Precomputed unit offsets keep
    // <cmath> out of the renderer; 12 rays is dense enough without becoming a
    // blob when the burst is small.
    static const int8_t RAYS[12][2] = {{0, -1},  {1, -2},  {1, -1}, {2, -1}, {1, 0},  {2, 1},
                                       {1, 1},  {1, 2},   {0, 1},  {-1, 2}, {-1, 1}, {-2, 1}};
    const int cx[2] = {10, 53};
    const int reach = 2 + (animation_step % 5);
    for (int k = 0; k < 2; k++) {
      for (int r = 1; r <= reach; r++) {
        for (int ray = 0; ray < 12; ray++) {
          // Outer rings drop every other ray, so the burst thins out rather
          // than growing a solid shell.
          if (r == reach && (ray % 2 == 0))
            continue;
          it.draw_pixel_at(cx[k] + RAYS[ray][0] * r, 5 + RAYS[ray][1] * r, foreground);
        }
      }
    }
  }

  // Sprite last, so it sits in front of the motion.
  it.image(SPRITE_X, 0, sprite, foreground);
}

// -----------------------------------------------------------------------------
// Dispatch: one `if` chain over the `Panel Scene` select. These strings must
// match that select's options; an unmatched name draws only the background.
//
// Called from the display lambda, which passes the entities it reads, so this
// stays a pure function of its arguments apart from the animation clock and the
// text measurement cache.
// -----------------------------------------------------------------------------
inline void draw_scene(display::Display &it, Color foreground, const StringRef &scene, const StringRef &size,
                       const StringRef &weight) {
  if (scene == "Clock") {
    draw_clock(it, foreground);
  } else if (scene == "Weather") {
    draw_weather(it, foreground);
  } else if (scene == "Icon") {
    draw_icon(it, foreground);
  } else if (scene == "Message" || scene == "Sensor") {
    draw_text(it, foreground, size, weight, scene == "Sensor");
  } else if (scene == "Halloween") {
    draw_halloween(it, foreground);
  } else if (scene == "Winter") {
    draw_winter(it, foreground);
  } else if (scene == "Ghost") {
    draw_ghost(it, foreground);
  } else if (scene == "Birthday" || scene == "New Year") {
    draw_occasion(it, foreground, scene == "Birthday");
  }
}

}  // namespace esphome::panel
