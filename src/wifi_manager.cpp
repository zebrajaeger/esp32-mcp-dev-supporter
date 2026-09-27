#include "wifi_manager.h"

#include <string.h>

#if __has_include("wifi_config.h")
#include "wifi_config.h"
#else
#error "Missing include/wifi_config.h. Copy include/wifi_config.example.h and provide trusted-LAN credentials."
#endif

#ifndef WIFI_SSID
#error "WIFI_SSID must be defined in include/wifi_config.h."
#endif

#ifndef WIFI_PASSWORD
#error "WIFI_PASSWORD must be defined in include/wifi_config.h."
#endif

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "lwip/inet.h"

namespace {

constexpr char kLogTag[] = "wifi";
constexpr int kMaxReconnectAttempts = 10;

bool g_connected = false;
int g_reconnect_attempts = 0;
char g_ip_address[INET_ADDRSTRLEN] = "";
wifi_manager::ConnectedCallback g_on_connected = nullptr;

void connect() {
  const esp_err_t result = esp_wifi_connect();
  if (result != ESP_OK) {
    ESP_LOGE(kLogTag, "WiFi connect request failed: %s", esp_err_to_name(result));
  }
}

void event_handler(void *, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    ESP_LOGI(kLogTag, "Station started; connecting to configured network");
    connect();
    return;
  }

  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    g_connected = false;
    g_ip_address[0] = '\0';
    if (g_reconnect_attempts < kMaxReconnectAttempts) {
      ++g_reconnect_attempts;
      ESP_LOGW(kLogTag, "Disconnected; reconnect attempt %d/%d", g_reconnect_attempts,
               kMaxReconnectAttempts);
      connect();
    } else {
      ESP_LOGE(kLogTag, "WiFi reconnect limit reached");
    }
    return;
  }

  if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    const auto *event = static_cast<const ip_event_got_ip_t *>(event_data);
    inet_ntoa_r(event->ip_info.ip, g_ip_address, sizeof(g_ip_address));
    g_connected = true;
    g_reconnect_attempts = 0;
    ESP_LOGI(kLogTag, "Connected with IP %s", g_ip_address);
    if (g_on_connected != nullptr) {
      g_on_connected();
    }
  }
}

}  // namespace

namespace wifi_manager {

void initialize(ConnectedCallback on_connected) {
  g_on_connected = on_connected;

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  if (esp_netif_create_default_wifi_sta() == nullptr) {
    ESP_LOGE(kLogTag, "Could not create the WiFi station network interface");
    abort();
  }

  wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&init_config));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler,
                                                       nullptr, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler,
                                                       nullptr, nullptr));

  wifi_config_t wifi_config = {};
  strncpy(reinterpret_cast<char *>(wifi_config.sta.ssid), WIFI_SSID,
          sizeof(wifi_config.sta.ssid) - 1);
  strncpy(reinterpret_cast<char *>(wifi_config.sta.password), WIFI_PASSWORD,
          sizeof(wifi_config.sta.password) - 1);
  wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
  wifi_config.sta.pmf_cfg.capable = true;
  wifi_config.sta.pmf_cfg.required = false;

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());
}

bool is_connected() {
  return g_connected;
}

void ip_address(char *buffer, size_t buffer_size) {
  if (buffer_size == 0) {
    return;
  }

  strncpy(buffer, g_ip_address, buffer_size - 1);
  buffer[buffer_size - 1] = '\0';
}

}  // namespace wifi_manager
