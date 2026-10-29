#pragma once

#include "esphome/components/light/light_output.h"
#include "esphome/core/component.h"

namespace esphome::virtual_light {

/// An RGB light with no hardware behind it.
///
/// ESPHome has no colour entity type of its own: `color:` is baked at compile time
/// and `light` normally drives an LED strip. A `light` is the only entity the API
/// can carry RGB values in, so this one exists purely so Home Assistant renders a
/// real colour wheel for the panel's colours.
///
/// Nothing is stored here. The state lives in the LightState that ESPHome already
/// keeps (and persists), and the display lambda reads it through
/// `id(light_background)->current_values_as_rgb(...)`. That keeps this class as
/// small as the interface allows.
class VirtualLightOutput final : public light::LightOutput {
 public:
  light::LightTraits get_traits() override {
    auto traits = light::LightTraits();
    traits.set_supported_color_modes({light::ColorMode::RGB});
    return traits;
  }

  void write_state(light::LightState *state) override {
    // There is no hardware to write to. Publishing is what matters: the state is
    // already stored in `state` and pushed to the frontend by the light component.
  }
};

}  // namespace esphome::virtual_light
