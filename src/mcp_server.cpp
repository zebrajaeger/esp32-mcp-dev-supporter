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
constexpr char kFirmwareVersion[] = "0.2.0";
constexpr char kMdnsHostname[] = "esp32-mcp.local";
constexpr size_t kMaxRequestBytes = 4096;

constexpr char kDashboardHtml[] = R"html(<!doctype html>
<html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>ESP32 GPIO</title>
<style>
*{box-sizing:border-box}body{font:15px system-ui,sans-serif;margin:2rem;background:#10151b;color:#e7edf4}h1{margin:0}.muted{color:#9aa8b6}#state{color:#8be9a0}.device{display:flex;flex-wrap:wrap;gap:.5rem 1rem;background:#18212b;padding:1rem;border-radius:.5rem;margin:1rem 0}.device span{white-space:nowrap}.pins{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:.75rem}.pin{padding:1rem;background:#18212b;border:1px solid #293846;border-radius:.5rem}.pin h2{font-size:1rem;margin:0 0 .25rem}.pin p{margin:.25rem 0}.pin.reserved{opacity:.58}.controls{display:grid;grid-template-columns:1fr 1fr;gap:.5rem;margin-top:.75rem}.controls label{display:grid;gap:.2rem;font-size:.8rem;color:#9aa8b6}select,button{font:inherit;padding:.38rem;border-radius:.3rem;border:1px solid #405365;background:#10151b;color:#e7edf4}button{cursor:pointer;background:#265f45;border-color:#3c9168}.warning{color:#ffc66d;font-size:.85rem}.error{color:#ff8b8b}@media(max-width:700px){body{margin:1rem}.pins{grid-template-columns:1fr}.controls{grid-template-columns:1fr}}
</style><body><h1>ESP32 GPIO</h1><p id="state">Loading...</p><div class="device" id="device"></div><main class="pins" id="pins"></main>
<script>
const d=document.querySelector('#device'),p=document.querySelector('#pins'),s=document.querySelector('#state');
const esc=v=>String(v).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const boot=g=>g.bootstrapping?'<p class="warning">Bootstrapping pin: changes can affect the next boot.</p>':'';
function choices(selected,values){return values.map(v=>`<option ${v===selected?'selected':''}>${v}</option>`).join('')}
function card(g){const inaccessible=!g.accessible, inputOnly=g.classification==='input-only',modes=inputOnly?['input']:['input','output'];const controls=inaccessible?'':`<div class="controls"><label>Mode<select data-mode>${choices(g.mode,modes)}</select></label><label>Pull<select data-pull ${g.mode==='output'||inputOnly?'disabled':''}>${choices(g.pull,['none','up','down'])}</select></label><label>Initial / output level<select data-level>${choices(g.level?'high':'low',['low','high'])}</select></label><button data-apply>Apply configuration</button>${g.writable?'<button data-write>Set output level</button>':''}</div>`;return `<section class="pin ${inaccessible?'reserved':''}" data-pin="${g.pin}"><h2>GPIO ${g.pin}</h2><p>${esc(g.classification)} | ${esc(g.mode||'unavailable')} | pull: ${esc(g.pull||'n/a')}</p><p>Level: ${g.level===null?'n/a':g.level?'high':'low'}</p>${inaccessible?'<p class="muted">This pin is unavailable for GPIO access.</p>':boot(g)+controls}<p class="error" data-error></p></section>`}
function show(x){d.innerHTML=`<span>Firmware ${esc(x.firmwareVersion)}</span><span>WiFi: ${x.wifiConnected?'connected':'disconnected'}</span><span>${esc(x.ipAddress||'n/a')}</span><span>Uptime ${x.uptimeSeconds}s</span><span>Heap ${x.freeHeapBytes} B</span>`;p.innerHTML=x.gpio.map(card).join('');s.textContent='Live: refreshed '+new Date().toLocaleTimeString();s.className='';}
async function request(path,data){const r=await fetch(path,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)});const x=await r.json();if(!r.ok)throw Error(x.error||'Request failed');return x}
p.addEventListener('click',async e=>{const button=e.target;if(!button.matches('[data-apply],[data-write]'))return;const card=button.closest('[data-pin]'),pin=Number(card.dataset.pin),error=card.querySelector('[data-error]'),mode=card.querySelector('[data-mode]').value,pull=card.querySelector('[data-pull]').value,level=card.querySelector('[data-level]').value==='high';error.textContent='';try{if(button.matches('[data-apply]')){if(card.querySelector('.warning')&&!confirm('This bootstrapping pin can affect the next boot. Continue?'))return;await request('/api/gpio/configure',mode==='input'?{pin,mode,pull}:{pin,mode,initialLevel:level})}else await request('/api/gpio/write',{pin,level});await poll()}catch(err){error.textContent=err.message}});
async function poll(){try{show(await fetch('/api/status',{cache:'no-store'}).then(r=>{if(!r.ok)throw Error();return r.json()}))}catch(_){s.textContent='Refresh failed; showing last successful values.';s.className='error'}}poll();setInterval(poll,2000)
</script></body></html>)html";

httpd_handle_t g_server = nullptr;

const char *classification_for(int pin) {
  if (gpio_service::is_reserved_pin(pin)) return "reserved";
  if (gpio_service::is_uart_pin(pin)) return "uart";
  if (pin == gpio_service::kOnboardLedPin) return "onboard-led";
  if (gpio_service::is_input_only_pin(pin)) return "input-only";
  if (gpio_service::is_bootstrapping_pin(pin)) return "bootstrapping";
  return "general-purpose";
}

void add_json_string(cJSON *object, const char *name, const char *value) {
  cJSON_AddStringToObject(object, name, value == nullptr ? "" : value);
}

void add_device_status(cJSON *status) {
  char ip_address[16] = "";
  wifi_manager::ip_address(ip_address, sizeof(ip_address));
  add_json_string(status, "firmwareVersion", kFirmwareVersion);
  cJSON_AddBoolToObject(status, "wifiConnected", wifi_manager::is_connected());
  add_json_string(status, "ipAddress", ip_address);
  add_json_string(status, "mdnsHostname", kMdnsHostname);
  cJSON_AddNumberToObject(status, "uptimeSeconds", esp_timer_get_time() / 1000000);
  cJSON_AddNumberToObject(status, "freeHeapBytes", esp_get_free_heap_size());
}

void add_pin_status(cJSON *entry, int pin) {
  const bool accessible = gpio_service::is_accessible_pin(pin);
  cJSON_AddNumberToObject(entry, "pin", pin);
  cJSON_AddStringToObject(entry, "classification", classification_for(pin));
  cJSON_AddBoolToObject(entry, "accessible", accessible);
  cJSON_AddBoolToObject(entry, "bootstrapping", gpio_service::is_bootstrapping_pin(pin));
  if (!accessible) {
    cJSON_AddStringToObject(entry, "direction", "unavailable");
    cJSON_AddBoolToObject(entry, "readable", false);
    cJSON_AddBoolToObject(entry, "configurable", false);
    cJSON_AddBoolToObject(entry, "writable", false);
    cJSON_AddBoolToObject(entry, "managed", false);
    cJSON_AddNullToObject(entry, "mode");
    cJSON_AddNullToObject(entry, "pull");
    cJSON_AddNullToObject(entry, "level");
    return;
  }

  gpio_service::PinState state = {};
  if (gpio_service::state(pin, &state) != gpio_service::Result::kOk) {
    cJSON_AddStringToObject(entry, "direction", "error");
    cJSON_AddBoolToObject(entry, "readable", false);
    cJSON_AddBoolToObject(entry, "configurable", false);
    cJSON_AddBoolToObject(entry, "writable", false);
    cJSON_AddBoolToObject(entry, "managed", false);
    cJSON_AddNullToObject(entry, "mode");
    cJSON_AddNullToObject(entry, "pull");
    cJSON_AddNullToObject(entry, "level");
    return;
  }
  cJSON_AddStringToObject(entry, "direction", gpio_service::mode_name(state.mode));
  cJSON_AddBoolToObject(entry, "readable", state.readable);
  cJSON_AddBoolToObject(entry, "configurable", state.configurable);
  cJSON_AddBoolToObject(entry, "writable", state.writable);
  cJSON_AddBoolToObject(entry, "managed", true);
  cJSON_AddStringToObject(entry, "mode", gpio_service::mode_name(state.mode));
  cJSON_AddStringToObject(entry, "pull", gpio_service::pull_name(state.pull));
  cJSON_AddBoolToObject(entry, "level", state.level);
}

void add_gpio_inventory(cJSON *status) {
  cJSON *pins = cJSON_AddArrayToObject(status, "gpio");
  for (int pin = 0; pin <= 39; ++pin) {
    if (!gpio_service::is_valid_pin(pin)) continue;
    cJSON *entry = cJSON_CreateObject();
    add_pin_status(entry, pin);
    cJSON_AddItemToArray(pins, entry);
  }
}

cJSON *new_status_snapshot(bool include_gpio) {
  cJSON *status = cJSON_CreateObject();
  add_device_status(status);
  if (include_gpio) add_gpio_inventory(status);
  return status;
}

cJSON *new_pin_snapshot(int pin) {
  cJSON *entry = cJSON_CreateObject();
  add_pin_status(entry, pin);
  return entry;
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

void send_http_error(httpd_req_t *request, const char *status, const char *message) {
  cJSON *response = cJSON_CreateObject();
  cJSON_AddStringToObject(response, "error", message);
  send_json(request, response, status);
}

cJSON *new_response(const cJSON *id) {
  cJSON *response = cJSON_CreateObject();
  cJSON_AddStringToObject(response, "jsonrpc", "2.0");
  if (id != nullptr) cJSON_AddItemToObject(response, "id", cJSON_Duplicate(id, true));
  else cJSON_AddNullToObject(response, "id");
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
  if (structured_content != nullptr) cJSON_AddItemToObject(result, "structuredContent", structured_content);
  if (is_error) cJSON_AddBoolToObject(result, "isError", true);
  send_json(request, response);
}

bool get_pin(const cJSON *arguments, int *pin) {
  const cJSON *value = cJSON_GetObjectItemCaseSensitive(arguments, "pin");
  if (!cJSON_IsNumber(value) || value->valuedouble != value->valueint) return false;
  *pin = value->valueint;
  return true;
}

bool get_boolean(const cJSON *arguments, const char *name, bool *value) {
  const cJSON *item = cJSON_GetObjectItemCaseSensitive(arguments, name);
  if (!cJSON_IsBool(item)) return false;
  *value = cJSON_IsTrue(item);
  return true;
}

bool get_string(const cJSON *arguments, const char *name, const char **value) {
  const cJSON *item = cJSON_GetObjectItemCaseSensitive(arguments, name);
  if (!cJSON_IsString(item)) return false;
  *value = item->valuestring;
  return true;
}

bool parse_pull(const char *name, gpio_service::Pull *pull) {
  if (strcmp(name, "none") == 0) *pull = gpio_service::Pull::kNone;
  else if (strcmp(name, "up") == 0) *pull = gpio_service::Pull::kUp;
  else if (strcmp(name, "down") == 0) *pull = gpio_service::Pull::kDown;
  else return false;
  return true;
}

cJSON *pin_schema() {
  cJSON *schema = cJSON_CreateObject();
  cJSON_AddStringToObject(schema, "type", "object");
  cJSON *properties = cJSON_AddObjectToObject(schema, "properties");
  cJSON *pin = cJSON_AddObjectToObject(properties, "pin");
  cJSON_AddStringToObject(pin, "type", "integer");
  cJSON_AddStringToObject(pin, "description", "ESP32 GPIO number; UART GPIO 1/3 and flash GPIO 6-11 are inaccessible.");
  cJSON *required = cJSON_AddArrayToObject(schema, "required");
  cJSON_AddItemToArray(required, cJSON_CreateString("pin"));
  cJSON_AddBoolToObject(schema, "additionalProperties", false);
  return schema;
}

cJSON *write_schema() {
  cJSON *schema = pin_schema();
  cJSON *properties = cJSON_GetObjectItemCaseSensitive(schema, "properties");
  cJSON *level = cJSON_AddObjectToObject(properties, "level");
  cJSON_AddStringToObject(level, "type", "boolean");
  cJSON_AddItemToArray(cJSON_GetObjectItemCaseSensitive(schema, "required"), cJSON_CreateString("level"));
  return schema;
}

cJSON *configure_schema() {
  cJSON *schema = pin_schema();
  cJSON *properties = cJSON_GetObjectItemCaseSensitive(schema, "properties");
  cJSON *mode = cJSON_AddObjectToObject(properties, "mode");
  cJSON_AddStringToObject(mode, "type", "string");
  cJSON *mode_values = cJSON_AddArrayToObject(mode, "enum");
  cJSON_AddItemToArray(mode_values, cJSON_CreateString("input"));
  cJSON_AddItemToArray(mode_values, cJSON_CreateString("output"));
  cJSON *pull = cJSON_AddObjectToObject(properties, "pull");
  cJSON_AddStringToObject(pull, "type", "string");
  cJSON *pull_values = cJSON_AddArrayToObject(pull, "enum");
  cJSON_AddItemToArray(pull_values, cJSON_CreateString("none"));
  cJSON_AddItemToArray(pull_values, cJSON_CreateString("up"));
  cJSON_AddItemToArray(pull_values, cJSON_CreateString("down"));
  cJSON *initial = cJSON_AddObjectToObject(properties, "initialLevel");
  cJSON_AddStringToObject(initial, "type", "boolean");
  cJSON_AddStringToObject(initial, "description", "Required when mode is output.");
  cJSON_AddItemToArray(cJSON_GetObjectItemCaseSensitive(schema, "required"), cJSON_CreateString("mode"));
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
  add_tool(tools, "gpio.configure", "Configure GPIO input/pull or output/initial level.", configure_schema());
  add_tool(tools, "gpio.read", "Read GPIO level and runtime configuration.", pin_schema());
  add_tool(tools, "gpio.write", "Set a GPIO already configured as output.", write_schema());
  send_json(request, response);
}

void send_gpio_result(httpd_req_t *request, const cJSON *id, int pin, gpio_service::Result result) {
  if (result != gpio_service::Result::kOk) {
    send_tool_result(request, id, nullptr, gpio_service::result_message(result), true);
    return;
  }
  send_tool_result(request, id, new_pin_snapshot(pin), "GPIO operation succeeded.");
}

void handle_gpio_read(httpd_req_t *request, const cJSON *id, const cJSON *arguments) {
  int pin = 0;
  if (!get_pin(arguments, &pin)) {
    send_tool_result(request, id, nullptr, "gpio.read requires an integer pin.", true);
    return;
  }
  gpio_service::PinState state = {};
  send_gpio_result(request, id, pin, gpio_service::state(pin, &state));
}

gpio_service::Result configure_from_arguments(const cJSON *arguments, int *pin) {
  const char *mode = nullptr;
  if (!get_pin(arguments, pin) || !get_string(arguments, "mode", &mode)) return gpio_service::Result::kInvalidPin;
  if (strcmp(mode, "input") == 0) {
    const char *pull_name = nullptr;
    gpio_service::Pull pull = gpio_service::Pull::kNone;
    if (!get_string(arguments, "pull", &pull_name) || !parse_pull(pull_name, &pull)) {
      return gpio_service::Result::kInvalidPull;
    }
    return gpio_service::configure_input(*pin, pull);
  }
  if (strcmp(mode, "output") == 0) {
    bool initial_level = false;
    if (cJSON_GetObjectItemCaseSensitive(arguments, "pull") != nullptr) return gpio_service::Result::kInvalidPull;
    if (!get_boolean(arguments, "initialLevel", &initial_level)) return gpio_service::Result::kDriverError;
    return gpio_service::configure_output(*pin, initial_level);
  }
  return gpio_service::Result::kInvalidPin;
}

void handle_gpio_configure(httpd_req_t *request, const cJSON *id, const cJSON *arguments) {
  int pin = 0;
  const gpio_service::Result result = configure_from_arguments(arguments, &pin);
  if (result == gpio_service::Result::kInvalidPin && !get_pin(arguments, &pin)) {
    send_tool_result(request, id, nullptr, "gpio.configure requires an integer pin and mode.", true);
    return;
  }
  send_gpio_result(request, id, pin, result);
}

void handle_gpio_write(httpd_req_t *request, const cJSON *id, const cJSON *arguments) {
  int pin = 0;
  bool level = false;
  if (!get_pin(arguments, &pin) || !get_boolean(arguments, "level", &level)) {
    send_tool_result(request, id, nullptr, "gpio.write requires an integer pin and Boolean level.", true);
    return;
  }
  send_gpio_result(request, id, pin, gpio_service::write(pin, level));
}

void handle_system_status(httpd_req_t *request, const cJSON *id) {
  send_tool_result(request, id, new_status_snapshot(false), "ESP32 status");
}

esp_err_t status_api_handler(httpd_req_t *request) {
  send_json(request, new_status_snapshot(true));
  return ESP_OK;
}

esp_err_t dashboard_handler(httpd_req_t *request) {
  httpd_resp_set_type(request, "text/html; charset=utf-8");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  httpd_resp_send(request, kDashboardHtml, HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

cJSON *receive_json(httpd_req_t *request) {
  if (request->content_len == 0 || request->content_len > kMaxRequestBytes) return nullptr;
  char *body = static_cast<char *>(malloc(request->content_len + 1));
  if (body == nullptr) return nullptr;
  size_t received = 0;
  while (received < request->content_len) {
    const int result = httpd_req_recv(request, body + received, request->content_len - received);
    if (result <= 0) {
      free(body);
      return nullptr;
    }
    received += static_cast<size_t>(result);
  }
  body[received] = '\0';
  cJSON *json = cJSON_ParseWithLength(body, received);
  free(body);
  return json;
}

esp_err_t gpio_configure_api_handler(httpd_req_t *request) {
  cJSON *arguments = receive_json(request);
  if (!cJSON_IsObject(arguments)) {
    cJSON_Delete(arguments);
    send_http_error(request, "400 Bad Request", "Expected a JSON object.");
    return ESP_OK;
  }
  int pin = 0;
  const gpio_service::Result result = configure_from_arguments(arguments, &pin);
  cJSON_Delete(arguments);
  if (result != gpio_service::Result::kOk) {
    send_http_error(request, "400 Bad Request", gpio_service::result_message(result));
    return ESP_OK;
  }
  send_json(request, new_pin_snapshot(pin));
  return ESP_OK;
}

esp_err_t gpio_write_api_handler(httpd_req_t *request) {
  cJSON *arguments = receive_json(request);
  int pin = 0;
  bool level = false;
  if (!cJSON_IsObject(arguments) || !get_pin(arguments, &pin) || !get_boolean(arguments, "level", &level)) {
    cJSON_Delete(arguments);
    send_http_error(request, "400 Bad Request", "gpio.write requires an integer pin and Boolean level.");
    return ESP_OK;
  }
  const gpio_service::Result result = gpio_service::write(pin, level);
  cJSON_Delete(arguments);
  if (result != gpio_service::Result::kOk) {
    send_http_error(request, "400 Bad Request", gpio_service::result_message(result));
    return ESP_OK;
  }
  send_json(request, new_pin_snapshot(pin));
  return ESP_OK;
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
  } else if (strcmp(name->valuestring, "system.status") == 0) {
    handle_system_status(request, id);
  } else if (!cJSON_IsObject(arguments)) {
    send_json_rpc_error(request, id, -32602, "GPIO tools require object arguments");
  } else if (strcmp(name->valuestring, "gpio.read") == 0) {
    handle_gpio_read(request, id, arguments);
  } else if (strcmp(name->valuestring, "gpio.configure") == 0) {
    handle_gpio_configure(request, id, arguments);
  } else if (strcmp(name->valuestring, "gpio.write") == 0) {
    handle_gpio_write(request, id, arguments);
  } else {
    send_json_rpc_error(request, id, -32601, "Unknown tool");
  }
}

esp_err_t mcp_handler(httpd_req_t *request) {
  cJSON *message = receive_json(request);
  if (message == nullptr) {
    send_json_rpc_error(request, nullptr, -32700, "Request body must contain valid JSON up to 4096 bytes");
    return ESP_OK;
  }
  const cJSON *id = cJSON_GetObjectItemCaseSensitive(message, "id");
  const cJSON *jsonrpc = cJSON_GetObjectItemCaseSensitive(message, "jsonrpc");
  const cJSON *method = cJSON_GetObjectItemCaseSensitive(message, "method");
  const cJSON *params = cJSON_GetObjectItemCaseSensitive(message, "params");
  if (!cJSON_IsString(jsonrpc) || strcmp(jsonrpc->valuestring, "2.0") != 0 || !cJSON_IsString(method)) {
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
  if (g_server != nullptr) return;
  httpd_config_t configuration = HTTPD_DEFAULT_CONFIG();
  configuration.server_port = 80;
  configuration.max_uri_handlers = 6;
  ESP_ERROR_CHECK(httpd_start(&g_server, &configuration));
  const httpd_uri_t endpoints[] = {
      {.uri = "/mcp", .method = HTTP_POST, .handler = mcp_handler, .user_ctx = nullptr},
      {.uri = "/", .method = HTTP_GET, .handler = dashboard_handler, .user_ctx = nullptr},
      {.uri = "/api/status", .method = HTTP_GET, .handler = status_api_handler, .user_ctx = nullptr},
      {.uri = "/api/gpio/configure", .method = HTTP_POST, .handler = gpio_configure_api_handler, .user_ctx = nullptr},
      {.uri = "/api/gpio/write", .method = HTTP_POST, .handler = gpio_write_api_handler, .user_ctx = nullptr},
  };
  for (const httpd_uri_t &endpoint : endpoints) ESP_ERROR_CHECK(httpd_register_uri_handler(g_server, &endpoint));
  ESP_LOGI(kLogTag, "MCP server listening on http://esp32-mcp.local/mcp");
}

}  // namespace mcp_server
