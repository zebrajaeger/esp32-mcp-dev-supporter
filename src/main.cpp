#include "gpio_service.h"
#include "mcp_server.h"
#include "mdns_responder.h"
#include "wifi_manager.h"

#include "esp_log.h"
#include "nvs_flash.h"

namespace {

constexpr char kLogTag[] = "main";

void start_network_services() {
  static bool services_started = false;
  if (services_started) {
    return;
  }

  mdns_responder::start();
  mcp_server::start();
  services_started = true;
  ESP_LOGI(kLogTag, "mDNS advertised as esp32-mcp.local");
}

}  // namespace

extern "C" void app_main() {
  ESP_LOGI(kLogTag, "ESP32 MCP server firmware 0.1.0 starting");

  esp_err_t nvs_result = nvs_flash_init();
  if (nvs_result == ESP_ERR_NVS_NO_FREE_PAGES || nvs_result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    nvs_result = nvs_flash_init();
  }
  ESP_ERROR_CHECK(nvs_result);

  gpio_service::initialize();
  ESP_LOGI(kLogTag, "Initial network state: disconnected");
  wifi_manager::initialize(&start_network_services);
}
