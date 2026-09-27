#pragma once

namespace gpio_service {

constexpr int kOnboardLedPin = 2;

void initialize();
bool is_allowed_pin(int pin);
bool write_led(bool level);
bool read_led(bool *level);
bool reported_level(int pin, bool *level);

}  // namespace gpio_service
