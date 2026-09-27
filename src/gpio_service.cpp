#include "gpio_service.h"

#include "driver/gpio.h"
#include "esp_log.h"

namespace {

constexpr char kLogTag[] = "gpio";
bool g_led_level = false;

}  // namespace

namespace gpio_service {

void initialize() {
  gpio_config_t configuration = {};
  configuration.pin_bit_mask = 1ULL << kOnboardLedPin;
  configuration.mode = GPIO_MODE_OUTPUT;
  configuration.pull_up_en = GPIO_PULLUP_DISABLE;
  configuration.pull_down_en = GPIO_PULLDOWN_DISABLE;
  configuration.intr_type = GPIO_INTR_DISABLE;

  ESP_ERROR_CHECK(gpio_config(&configuration));
  ESP_ERROR_CHECK(gpio_set_level(static_cast<gpio_num_t>(kOnboardLedPin), 0));
  g_led_level = false;
  ESP_LOGI(kLogTag, "GPIO %d configured as onboard LED output", kOnboardLedPin);
}

bool is_allowed_pin(int pin) {
  return pin == kOnboardLedPin;
}

bool write_led(bool level) {
  if (gpio_set_level(static_cast<gpio_num_t>(kOnboardLedPin), level ? 1 : 0) != ESP_OK) {
    return false;
  }

  g_led_level = level;
  return true;
}

bool read_led(bool *level) {
  if (level == nullptr) {
    return false;
  }

  // GPIO 2 is dedicated to output in this firmware. Track the commanded
  // output level instead of sampling an input register that board circuitry
  // can pull differently from the driven output.
  *level = g_led_level;
  return true;
}

bool reported_level(int pin, bool *level) {
  if (level == nullptr || pin < 0 || pin > 39) {
    return false;
  }

  if (pin == kOnboardLedPin) {
    *level = g_led_level;
    return true;
  }

  *level = gpio_get_level(static_cast<gpio_num_t>(pin)) != 0;
  return true;
}

}  // namespace gpio_service
