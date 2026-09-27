#include "mdns_responder.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "lwip/err.h"

#include "wifi_manager.h"

namespace {

constexpr char kLogTag[] = "mdns";
constexpr uint16_t kMdnsPort = 5353;
constexpr char kHostname[] = "esp32-mcp";
constexpr uint8_t kQueryHeaderBytes = 12;
constexpr size_t kMaxPacketBytes = 512;

bool g_started = false;

uint16_t read_u16(const uint8_t *data) {
  return static_cast<uint16_t>((data[0] << 8) | data[1]);
}

void write_u16(uint8_t *data, uint16_t value) {
  data[0] = static_cast<uint8_t>(value >> 8);
  data[1] = static_cast<uint8_t>(value);
}

void write_u32(uint8_t *data, uint32_t value) {
  data[0] = static_cast<uint8_t>(value >> 24);
  data[1] = static_cast<uint8_t>(value >> 16);
  data[2] = static_cast<uint8_t>(value >> 8);
  data[3] = static_cast<uint8_t>(value);
}

bool hostname_matches(const uint8_t *packet, size_t packet_length, size_t *offset) {
  const size_t hostname_length = strlen(kHostname);
  if (*offset + hostname_length + 8 > packet_length || packet[*offset] != hostname_length) {
    return false;
  }

  ++*offset;
  if (memcmp(packet + *offset, kHostname, hostname_length) != 0) {
    return false;
  }
  *offset += hostname_length;
  if (*offset + 7 > packet_length || packet[*offset] != 5 ||
      memcmp(packet + *offset + 1, "local", 5) != 0) {
    return false;
  }
  *offset += 6;
  if (packet[*offset] != 0) {
    return false;
  }
  ++*offset;
  return true;
}

void respond_to_query(int socket, const uint8_t *query, size_t query_length,
                      const sockaddr_in *sender) {
  if (query_length < kQueryHeaderBytes || read_u16(query + 4) == 0) {
    return;
  }

  size_t question_end = kQueryHeaderBytes;
  if (!hostname_matches(query, query_length, &question_end) || question_end + 4 > query_length) {
    return;
  }
  const uint16_t question_type = read_u16(query + question_end);
  const uint16_t question_class = read_u16(query + question_end + 2) & 0x7fff;
  if ((question_type != 1 && question_type != 255) || question_class != 1) {
    return;
  }
  question_end += 4;

  char address_text[16] = "";
  wifi_manager::ip_address(address_text, sizeof(address_text));
  const uint32_t address = inet_addr(address_text);
  if (address == IPADDR_NONE) {
    return;
  }

  uint8_t response[kMaxPacketBytes] = {};
  memcpy(response, query, question_end);
  write_u16(response + 2, 0x8400);  // Authoritative DNS response.
  write_u16(response + 4, 1);
  write_u16(response + 6, 1);
  write_u16(response + 8, 0);
  write_u16(response + 10, 0);

  size_t offset = question_end;
  response[offset++] = 0xc0;
  response[offset++] = 0x0c;
  write_u16(response + offset, 1);
  offset += 2;
  write_u16(response + offset, 0x8001);  // Cache flush plus IN class.
  offset += 2;
  write_u32(response + offset, 120);
  offset += 4;
  write_u16(response + offset, 4);
  offset += 2;
  memcpy(response + offset, &address, 4);
  offset += 4;

  sockaddr_in destination = {};
  destination.sin_family = AF_INET;
  destination.sin_port = htons(kMdnsPort);
  destination.sin_addr.s_addr = inet_addr("224.0.0.251");
  if ((ntohs(sender->sin_port) != kMdnsPort) && sender->sin_addr.s_addr != INADDR_ANY) {
    destination = *sender;
  }
  sendto(socket, response, offset, 0, reinterpret_cast<const sockaddr *>(&destination),
         sizeof(destination));
}

void task(void *) {
  const int socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
  if (socket < 0) {
    ESP_LOGE(kLogTag, "Could not create mDNS socket");
    vTaskDelete(nullptr);
    return;
  }

  int reuse = 1;
  setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
  sockaddr_in listen_address = {};
  listen_address.sin_family = AF_INET;
  listen_address.sin_port = htons(kMdnsPort);
  listen_address.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(socket, reinterpret_cast<const sockaddr *>(&listen_address), sizeof(listen_address)) < 0) {
    ESP_LOGE(kLogTag, "Could not bind mDNS socket");
    close(socket);
    vTaskDelete(nullptr);
    return;
  }

  ip_mreq membership = {};
  membership.imr_multiaddr.s_addr = inet_addr("224.0.0.251");
  membership.imr_interface.s_addr = htonl(INADDR_ANY);
  if (setsockopt(socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, &membership, sizeof(membership)) < 0) {
    ESP_LOGE(kLogTag, "Could not join the mDNS multicast group");
    close(socket);
    vTaskDelete(nullptr);
    return;
  }

  ESP_LOGI(kLogTag, "Advertising %s.local", kHostname);
  while (true) {
    uint8_t query[kMaxPacketBytes];
    sockaddr_in sender = {};
    socklen_t sender_length = sizeof(sender);
    const int received = recvfrom(socket, query, sizeof(query), 0,
                                  reinterpret_cast<sockaddr *>(&sender), &sender_length);
    if (received > 0) {
      respond_to_query(socket, query, static_cast<size_t>(received), &sender);
    }
  }
}

}  // namespace

namespace mdns_responder {

void start() {
  if (g_started) {
    return;
  }
  g_started = true;
  xTaskCreate(task, "mdns_responder", 4096, nullptr, 5, nullptr);
}

}  // namespace mdns_responder
