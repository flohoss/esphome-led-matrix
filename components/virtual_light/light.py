import esphome.codegen as cg
from esphome.components import light
import esphome.config_validation as cv
from esphome.const import CONF_OUTPUT_ID
from esphome.types import ConfigType

CODEOWNERS = ["@flohoss"]

virtual_light_ns = cg.esphome_ns.namespace("virtual_light")
VirtualLightOutput = virtual_light_ns.class_("VirtualLightOutput", light.LightOutput)

# CONF_OUTPUT_ID, not CONF_ID: the light component already declares the id for the
# LightState, so the output needs its own generated id.
CONFIG_SCHEMA = light.RGB_LIGHT_SCHEMA.extend(
    {
        cv.GenerateID(CONF_OUTPUT_ID): cv.declare_id(VirtualLightOutput),
    }
)


async def to_code(config: ConfigType) -> None:
    var = cg.new_Pvariable(config[CONF_OUTPUT_ID])
    await light.register_light(var, config)
