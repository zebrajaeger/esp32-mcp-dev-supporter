#pragma once

#include <stddef.h>

namespace wifi_manager {

using ConnectedCallback = void (*)();

void initialize(ConnectedCallback on_connected);
bool is_connected();
void ip_address(char *buffer, size_t buffer_size);

}  // namespace wifi_manager
