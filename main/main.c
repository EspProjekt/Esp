#include <stdio.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <esp_wifi.h>
#include <esp_http_server.h>



#define BLINK_PERIOD (1000)
#define TICK_DELAY (BLINK_PERIOD / portTICK_PERIOD_MS)
#define WIFI_SSID "WolfnetMesh"
#define WIFI_PASSWD "koziadupa6666"



void run_server();

void connect_to_wifi();

void switch_light();

void register_endpoints(httpd_handle_t server, httpd_uri_t* handlers, size_t num_handlers);

esp_err_t status_endpoint(httpd_req_t *request);

esp_err_t light_switch_endpoint(httpd_req_t *request);

httpd_uri_t* create_handlers(size_t *num_handlers);

httpd_uri_t create_endpoint_handler(
    httpd_method_t method,
    const char *uri, esp_err_t (*handler)(httpd_req_t *request)
);



static const char *TAG = "MAIN";
int iterations = 0;
bool is_light_on = false;



void app_main(void){
    gpio_set_direction(GPIO_NUM_2, GPIO_MODE_OUTPUT);
    esp_log_level_set(TAG, ESP_LOG_DEBUG);

    connect_to_wifi();
    run_server();

    while (true){
        vTaskDelay(TICK_DELAY);
        ESP_LOGE(TAG, "iter: %d", iterations);
        iterations += 1;
        
    }
}



void connect_to_wifi(){
    wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    wifi_config_t wifi_cfg = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWD
        }
    };
    
    nvs_flash_init();
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    esp_wifi_init(&wifi_init_cfg);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
    esp_wifi_start();
    esp_wifi_connect();
}



void run_server(){
    httpd_config_t httpd_cfg = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t httpd_handle;

    httpd_start(&httpd_handle, &httpd_cfg);

    size_t handlers_count;
    httpd_uri_t* handlers = create_handlers(&handlers_count);
    register_endpoints(httpd_handle, handlers, handlers_count);
}



httpd_uri_t* create_handlers(size_t *handlers_count) {
    *handlers_count = 2; 
    httpd_uri_t* handlers = malloc(*handlers_count * sizeof(httpd_uri_t));

    if (handlers == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for handlers");
        return NULL;
    }

    // tu mozna dodac w prosty sposob kolejne endpointy, trzeba zmineic *handlers_count
    handlers[0] = create_endpoint_handler(HTTP_GET, "/health", status_endpoint);
    handlers[1] = create_endpoint_handler(HTTP_POST, "/light", light_switch_endpoint);
    
    return handlers;
}



httpd_uri_t create_endpoint_handler(httpd_method_t method, const char *uri, esp_err_t (*handler)(httpd_req_t *request)) {
    return (httpd_uri_t){
        .method = method,
        .uri = uri,
        .handler = handler
    };
}



void register_endpoints(httpd_handle_t server, httpd_uri_t* handlers, size_t num_handlers) {
    for (size_t i = 0; i < num_handlers; i++) {
        httpd_register_uri_handler(server, &handlers[i]);
    }
}



esp_err_t status_endpoint(httpd_req_t *request){
    char response_str[100];

    sprintf(response_str, "{\"uptime\":%d, \"light\": %s}", iterations, is_light_on ? "true" : "false");

    httpd_resp_set_type(request, "application/json");
    httpd_resp_sendstr(request, response_str);
    return ESP_OK;
}



esp_err_t light_switch_endpoint(httpd_req_t *request){
    char response_str[100];
    
    switch_light();
    sprintf(response_str, "{\"status\": %s}", is_light_on ? "true" : "false");
    
    httpd_resp_set_type(request, "application/json");
    httpd_resp_sendstr(request, response_str);
    return ESP_OK;
}



void switch_light(){
    is_light_on = !is_light_on;
    gpio_set_level(GPIO_NUM_2, is_light_on);
}