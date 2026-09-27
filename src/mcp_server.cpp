#include "mcp_server.h"

#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "gpio_service.h"
#include "wifi_manager.h"

namespace {

constexpr char kLogTag[] = "mcp";
constexpr char kFirmwareVersion[] = "0.1.0";
constexpr char kMdnsHostname[] = "esp32-mcp.local";
constexpr size_t kMaxRequestBytes = 4096;

httpd_handle_t g_server = nullptr;

void add_json_string(cJSON *object, const char *name, const char *value) {
  cJSON_AddStringToObject(object, name, value == nullptr ? "" : value);
}

void send_json(httpd_req_t *request, cJSON *response, const char *status = HTTPD_200) {
  char *serialized = cJSON_PrintUnformatted(response);
  cJSON_Delete(response);
  if (serialized == nullptr) {
    httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Response allocation failed");
    return;
  }

  httpd_resp_set_status(request, status);
  httpd_resp_set_type(request, "application/json");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  httpd_resp_send(request, serialized, HTTPD_RESP_USE_STRLEN);
  cJSON_free(serialized);
}

cJSON *new_response(const cJSON *id) {
  cJSON *response = cJSON_CreateObject();
  cJSON_AddStringToObject(response, "jsonrpc", "2.0");
  if (id != nullptr) {
    cJSON_AddItemToObject(response, "id", cJSON_Duplicate(id, true));
  } else {
    cJSON_AddNullToObject(response, "id");
  }
  return response;
}

void send_json_rpc_error(httpd_req_t *request, const cJSON *id, int code, const char *message) {
  cJSON *response = new_response(id);
  cJSON *error = cJSON_AddObjectToObject(response, "error");
  cJSON_AddNumberToObject(error, "code", code);
  cJSON_AddStringToObject(error, "message", message);
  send_json(request, response);
}

void send_tool_result(httpd_req_t *request, const cJSON *id, cJSON *structured_content,
                      const char *text, bool is_error = false) {
  cJSON *response = new_response(id);
  cJSON *result = cJSON_AddObjectToObject(response, "result");
  cJSON *content = cJSON_AddArrayToObject(result, "content");
  cJSON *item = cJSON_CreateObject();
  cJSON_AddStringToObject(item, "type", "text");
  cJSON_AddStringToObject(item, "text", text);
  cJSON_AddItemToArray(content, item);
  if (structured_content != nullptr) {
    cJSON_AddItemToObject(result, "structuredContent", structured_content);
  }
  if (is_error) {
    cJSON_AddBoolToObject(result, "isError", true);
  }
  send_json(request, response);
}

cJSON *gpio_schema(const bool include_level) {
  cJSON *schema = cJSON_CreateObject();
  cJSON_AddStringToObject(schema, "type", "object");
  cJSON *properties = cJSON_AddObjectToObject(schema, "properties");
  cJSON *pin = cJSON_AddObjectToObject(properties, "pin");
  cJSON_AddStringToObject(pin, "type", "integer");
  cJSON_AddNumberToObject(pin, "const", gpio_service::kOnboardLedPin);
  cJSON_AddStringToObject(pin, "description", "Only GPIO 2 is available in this release.");
  if (include_level) {
    cJSON *level = cJSON_AddObjectToObject(properties, "level");
    cJSON_AddStringToObject(level, "type", "boolean");
  }
  cJSON *required = cJSON_AddArrayToObject(schema, "required");
  cJSON_AddItemToArray(required, cJSON_CreateString("pin"));
  if (include_level) {
    cJSON_AddItemToArray(required, cJSON_CreateString("level"));
  }
  cJSON_AddBoolToObject(schema, "additionalProperties", false);
  return schema;
}

void add_tool(cJSON *tools, const char *name, const char *description, cJSON *input_schema) {
  cJSON *tool = cJSON_CreateObject();
  cJSON_AddStringToObject(tool, "name", name);
  cJSON_AddStringToObject(tool, "description", description);
  cJSON_AddItemToObject(tool, "inputSchema", input_schema);
  cJSON_AddItemToArray(tools, tool);
}

void handle_initialize(httpd_req_t *request, const cJSON *id) {
  cJSON *response = new_response(id);
  cJSON *result = cJSON_AddObjectToObject(response, "result");
  cJSON_AddStringToObject(result, "protocolVersion", "2025-03-26");
  cJSON_AddObjectToObject(result, "capabilities");
  cJSON *capabilities = cJSON_GetObjectItemCaseSensitive(result, "capabilities");
  cJSON_AddObjectToObject(capabilities, "tools");
  cJSON *server_info = cJSON_AddObjectToObject(result, "serverInfo");
  cJSON_AddStringToObject(server_info, "name", "esp32-mcp");
  cJSON_AddStringToObject(server_info, "version", kFirmwareVersion);
  send_json(request, response);
}

void handle_tools_list(httpd_req_t *request, const cJSON *id) {
  cJSON *response = new_response(id);
  cJSON *result = cJSON_AddObjectToObject(response, "result");
  cJSON *tools = cJSON_AddArrayToObject(result, "tools");

  cJSON *status_schema = cJSON_CreateObject();
  cJSON_AddStringToObject(status_schema, "type", "object");
  cJSON_AddObjectToObject(status_schema, "properties");
  cJSON_AddBoolToObject(status_schema, "additionalProperties", false);
  add_tool(tools, "system.status", "Return firmware and network status.", status_schema);
  add_tool(tools, "gpio.read", "Read the current level of the onboard LED on GPIO 2.",
           gpio_schema(false));
  add_tool(tools, "gpio.write", "Set the onboard LED on GPIO 2.", gpio_schema(true));
  send_json(request, response);
}

bool requested_led_pin(const cJSON *arguments) {
  const cJSON *pin = cJSON_GetObjectItemCaseSensitive(arguments, "pin");
  return cJSON_IsNumber(pin) && pin->valuedouble == gpio_service::kOnboardLedPin &&
         gpio_service::is_allowed_pin(pin->valueint);
}

void handle_system_status(httpd_req_t *request, const cJSON *id) {
  char ip_address[16] = "";
  wifi_manager::ip_address(ip_address, sizeof(ip_address));

  cJSON *status = cJSON_CreateObject();
  add_json_string(status, "firmwareVersion", kFirmwareVersion);
  cJSON_AddBoolToObject(status, "wifiConnected", wifi_manager::is_connected());
  add_json_string(status, "ipAddress", ip_address);
  add_json_string(status, "mdnsHostname", kMdnsHostname);
  cJSON_AddNumberToObject(status, "uptimeSeconds", esp_timer_get_time() / 1000000);
  cJSON_AddNumberToObject(status, "freeHeapBytes", esp_get_free_heap_size());
  send_tool_result(request, id, status, "ESP32 status");
}

void handle_gpio_read(httpd_req_t *request, const cJSON *id, const cJSON *arguments) {
  if (!requested_led_pin(arguments)) {
    send_tool_result(request, id, nullptr, "Only GPIO 2 is available.", true);
    return;
  }

  bool level = false;
  if (!gpio_service::read_led(&level)) {
    send_tool_result(request, id, nullptr, "Could not read GPIO 2.", true);
    return;
  }

  cJSON *result = cJSON_CreateObject();
  cJSON_AddNumberToObject(result, "pin", gpio_service::kOnboardLedPin);
  cJSON_AddBoolToObject(result, "level", level);
  send_tool_result(request, id, result, level ? "GPIO 2 is high." : "GPIO 2 is low.");
}

void handle_gpio_write(httpd_req_t *request, const cJSON *id, const cJSON *arguments) {
  const cJSON *level = cJSON_GetObjectItemCaseSensitive(arguments, "level");
  if (!requested_led_pin(arguments) || !cJSON_IsBool(level)) {
    send_tool_result(request, id, nullptr,
                     "gpio.write requires pin 2 and a Boolean level.", true);
    return;
  }

  const bool requested_level = cJSON_IsTrue(level);
  if (!gpio_service::write_led(requested_level)) {
    send_tool_result(request, id, nullptr, "Could not write GPIO 2.", true);
    return;
  }

  cJSON *result = cJSON_CreateObject();
  cJSON_AddNumberToObject(result, "pin", gpio_service::kOnboardLedPin);
  cJSON_AddBoolToObject(result, "level", requested_level);
  send_tool_result(request, id, result,
                   requested_level ? "GPIO 2 set high." : "GPIO 2 set low.");
}

void handle_tool_call(httpd_req_t *request, const cJSON *id, const cJSON *params) {
  if (!cJSON_IsObject(params)) {
    send_json_rpc_error(request, id, -32602, "tools/call requires object parameters");
    return;
  }

  const cJSON *name = cJSON_GetObjectItemCaseSensitive(params, "name");
  const cJSON *arguments = cJSON_GetObjectItemCaseSensitive(params, "arguments");
  if (!cJSON_IsString(name)) {
    send_json_rpc_error(request, id, -32602, "tools/call requires a tool name");
    return;
  }

  if (strcmp(name->valuestring, "system.status") == 0) {
    handle_system_status(request, id);
  } else if (strcmp(name->valuestring, "gpio.read") == 0) {
    if (!cJSON_IsObject(arguments)) {
      send_json_rpc_error(request, id, -32602, "gpio.read requires object arguments");
      return;
    }
    handle_gpio_read(request, id, arguments);
  } else if (strcmp(name->valuestring, "gpio.write") == 0) {
    if (!cJSON_IsObject(arguments)) {
      send_json_rpc_error(request, id, -32602, "gpio.write requires object arguments");
      return;
    }
    handle_gpio_write(request, id, arguments);
  } else {
    send_json_rpc_error(request, id, -32601, "Unknown tool");
  }
}

esp_err_t mcp_handler(httpd_req_t *request) {
  if (request->content_len == 0 || request->content_len > kMaxRequestBytes) {
    send_json_rpc_error(request, nullptr, -32600, "Request body must be 1-4096 bytes");
    return ESP_OK;
  }

  char *body = static_cast<char *>(malloc(request->content_len + 1));
  if (body == nullptr) {
    httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Request allocation failed");
    return ESP_FAIL;
  }

  size_t received = 0;
  while (received < request->content_len) {
    const int result = httpd_req_recv(request, body + received, request->content_len - received);
    if (result <= 0) {
      free(body);
      if (result == HTTPD_SOCK_ERR_TIMEOUT) {
        httpd_resp_send_408(request);
      }
      return ESP_FAIL;
    }
    received += static_cast<size_t>(result);
  }
  body[received] = '\0';

  cJSON *message = cJSON_ParseWithLength(body, received);
  free(body);
  if (message == nullptr) {
    send_json_rpc_error(request, nullptr, -32700, "Parse error");
    return ESP_OK;
  }

  const cJSON *id = cJSON_GetObjectItemCaseSensitive(message, "id");
  const cJSON *jsonrpc = cJSON_GetObjectItemCaseSensitive(message, "jsonrpc");
  const cJSON *method = cJSON_GetObjectItemCaseSensitive(message, "method");
  const cJSON *params = cJSON_GetObjectItemCaseSensitive(message, "params");
  if (!cJSON_IsString(jsonrpc) || strcmp(jsonrpc->valuestring, "2.0") != 0 ||
      !cJSON_IsString(method)) {
    send_json_rpc_error(request, id, -32600, "Invalid JSON-RPC request");
  } else if (strcmp(method->valuestring, "initialize") == 0) {
    handle_initialize(request, id);
  } else if (strcmp(method->valuestring, "tools/list") == 0) {
    handle_tools_list(request, id);
  } else if (strcmp(method->valuestring, "tools/call") == 0) {
    handle_tool_call(request, id, params);
  } else if (strcmp(method->valuestring, "notifications/initialized") == 0 && id == nullptr) {
    httpd_resp_set_status(request, "202 Accepted");
    httpd_resp_send(request, nullptr, 0);
  } else {
    send_json_rpc_error(request, id, -32601, "Method not found");
  }

  cJSON_Delete(message);
  return ESP_OK;
}

}  // namespace

namespace mcp_server {

void start() {
  if (g_server != nullptr) {
    return;
  }

  httpd_config_t configuration = HTTPD_DEFAULT_CONFIG();
  configuration.server_port = 80;
  configuration.max_uri_handlers = 2;

  ESP_ERROR_CHECK(httpd_start(&g_server, &configuration));
  const httpd_uri_t mcp_endpoint = {
      .uri = "/mcp",
      .method = HTTP_POST,
      .handler = mcp_handler,
      .user_ctx = nullptr,
  };
  ESP_ERROR_CHECK(httpd_register_uri_handler(g_server, &mcp_endpoint));
  ESP_LOGI(kLogTag, "MCP server listening on http://esp32-mcp.local/mcp");
}

}  // namespace mcp_server
