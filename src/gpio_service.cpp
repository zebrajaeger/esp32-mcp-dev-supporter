#include "gpio_service.h"

#include "driver/gpio.h"
#include "esp_log.h"

namespace {

constexpr char kLogTag[] = "gpio";
gpio_service::PinState g_states[40] = {};

bool is_valid_pin_number(int pin) {
  return pin >= 0 && pin <= 39 && pin != 20 && pin != 24 && (pin < 28 || pin > 31);
}

bool is_reserved_pin_number(int pin) {
  return pin >= 6 && pin <= 11;
}

bool is_uart_pin_number(int pin) {
  return pin == 1 || pin == 3;
}

bool is_input_only_pin_number(int pin) {
  return pin >= 34 && pin <= 39;
}

bool is_bootstrapping_pin_number(int pin) {
  return pin == 0 || pin == 2 || pin == 4 || pin == 5 || pin == 12 || pin == 15;
}

bool is_accessible_pin_number(int pin) {
  return is_valid_pin_number(pin) && !is_reserved_pin_number(pin) && !is_uart_pin_number(pin);
}

gpio_pullup_t pull_up(gpio_service::Pull pull) {
  return pull == gpio_service::Pull::kUp ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
}

gpio_pulldown_t pull_down(gpio_service::Pull pull) {
  return pull == gpio_service::Pull::kDown ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE;
}

gpio_service::Result validate_access(int pin) {
  if (!is_valid_pin_number(pin)) return gpio_service::Result::kInvalidPin;
  if (!is_accessible_pin_number(pin)) return gpio_service::Result::kInaccessiblePin;
  return gpio_service::Result::kOk;
}

gpio_service::Result configure(int pin, gpio_mode_t mode, gpio_service::Pull pull) {
  gpio_config_t configuration = {};
  configuration.pin_bit_mask = 1ULL << pin;
  configuration.mode = mode;
  configuration.pull_up_en = pull_up(pull);
  configuration.pull_down_en = pull_down(pull);
  configuration.intr_type = GPIO_INTR_DISABLE;
  return gpio_config(&configuration) == ESP_OK ? gpio_service::Result::kOk
                                                : gpio_service::Result::kDriverError;
}

}  // namespace

namespace gpio_service {

void initialize() {
  for (int pin = 0; pin <= 39; ++pin) {
    if (!is_accessible_pin_number(pin)) continue;
    ESP_ERROR_CHECK(configure(pin, GPIO_MODE_INPUT, Pull::kNone) == Result::kOk ? ESP_OK
                                                                                  : ESP_FAIL);
    g_states[pin] = {
        .pin = pin,
        .accessible = true,
        .readable = true,
        .configurable = true,
        .writable = false,
        .bootstrapping = is_bootstrapping_pin_number(pin),
        .mode = Mode::kInput,
        .pull = Pull::kNone,
        .level = false,
    };
  }
  ESP_LOGI(kLogTag, "Accessible GPIOs initialized as input with no pull");
}

bool is_valid_pin(int pin) {
  return is_valid_pin_number(pin);
}

bool is_accessible_pin(int pin) {
  return is_accessible_pin_number(pin);
}

bool is_reserved_pin(int pin) {
  return is_reserved_pin_number(pin);
}

bool is_uart_pin(int pin) {
  return is_uart_pin_number(pin);
}

bool is_input_only_pin(int pin) {
  return is_input_only_pin_number(pin);
}

bool is_bootstrapping_pin(int pin) {
  return is_bootstrapping_pin_number(pin);
}

Result read(int pin, bool *level) {
  if (level == nullptr) return Result::kDriverError;
  const Result access = validate_access(pin);
  if (access != Result::kOk) return access;

  *level = gpio_get_level(static_cast<gpio_num_t>(pin)) != 0;
  g_states[pin].level = *level;
  return Result::kOk;
}

Result configure_input(int pin, Pull pull) {
  const Result access = validate_access(pin);
  if (access != Result::kOk) return access;
  if (is_input_only_pin_number(pin) && pull != Pull::kNone) return Result::kInvalidPull;

  const Result result = configure(pin, GPIO_MODE_INPUT, pull);
  if (result != Result::kOk) return result;

  g_states[pin].mode = Mode::kInput;
  g_states[pin].pull = pull;
  g_states[pin].writable = false;
  return read(pin, &g_states[pin].level);
}

Result configure_output(int pin, bool initial_level) {
  const Result access = validate_access(pin);
  if (access != Result::kOk) return access;
  if (is_input_only_pin_number(pin)) return Result::kInputOnlyPin;

  if (gpio_set_level(static_cast<gpio_num_t>(pin), initial_level ? 1 : 0) != ESP_OK) {
    return Result::kDriverError;
  }
  const Result result = configure(pin, GPIO_MODE_OUTPUT, Pull::kNone);
  if (result != Result::kOk) return result;

  g_states[pin].mode = Mode::kOutput;
  g_states[pin].pull = Pull::kNone;
  g_states[pin].writable = true;
  g_states[pin].level = initial_level;
  return Result::kOk;
}

Result write(int pin, bool level) {
  const Result access = validate_access(pin);
  if (access != Result::kOk) return access;
  if (g_states[pin].mode != Mode::kOutput) return Result::kNotOutput;
  if (gpio_set_level(static_cast<gpio_num_t>(pin), level ? 1 : 0) != ESP_OK) {
    return Result::kDriverError;
  }

  g_states[pin].level = level;
  return Result::kOk;
}

Result state(int pin, PinState *state) {
  if (state == nullptr) return Result::kDriverError;
  const Result access = validate_access(pin);
  if (access != Result::kOk) return access;

  if (g_states[pin].mode == Mode::kOutput) {
    *state = g_states[pin];
    return Result::kOk;
  }

  bool level = false;
  const Result result = read(pin, &level);
  if (result != Result::kOk) return result;
  *state = g_states[pin];
  state->level = level;
  return Result::kOk;
}

const char *result_message(Result result) {
  switch (result) {
    case Result::kOk: return "GPIO operation succeeded.";
    case Result::kInvalidPin: return "GPIO pin is invalid for this ESP32.";
    case Result::kInaccessiblePin: return "GPIO pin is reserved and inaccessible.";
    case Result::kInputOnlyPin: return "GPIO pin supports input mode only.";
    case Result::kInvalidPull: return "GPIO pin does not support the requested pull resistor.";
    case Result::kNotOutput: return "GPIO pin must be configured as output before writing.";
    case Result::kDriverError: return "GPIO driver operation failed.";
  }
  return "GPIO operation failed.";
}

const char *mode_name(Mode mode) {
  return mode == Mode::kOutput ? "output" : "input";
}

const char *pull_name(Pull pull) {
  switch (pull) {
    case Pull::kNone: return "none";
    case Pull::kUp: return "up";
    case Pull::kDown: return "down";
  }
  return "none";
}

}  // namespace gpio_service
