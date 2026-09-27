#pragma once

namespace gpio_service {

constexpr int kOnboardLedPin = 2;

enum class Mode { kInput, kOutput };
enum class Pull { kNone, kUp, kDown };
enum class Result {
  kOk,
  kInvalidPin,
  kInaccessiblePin,
  kInputOnlyPin,
  kInvalidPull,
  kNotOutput,
  kDriverError,
};

struct PinState {
  int pin;
  bool accessible;
  bool readable;
  bool configurable;
  bool writable;
  bool bootstrapping;
  Mode mode;
  Pull pull;
  bool level;
};

void initialize();
bool is_valid_pin(int pin);
bool is_accessible_pin(int pin);
bool is_reserved_pin(int pin);
bool is_uart_pin(int pin);
bool is_input_only_pin(int pin);
bool is_bootstrapping_pin(int pin);
Result read(int pin, bool *level);
Result configure_input(int pin, Pull pull);
Result configure_output(int pin, bool initial_level);
Result write(int pin, bool level);
Result state(int pin, PinState *state);
const char *result_message(Result result);
const char *mode_name(Mode mode);
const char *pull_name(Pull pull);

}  // namespace gpio_service
