#include <stdio.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <esp_wifi.h>
#include <esp_http_server.h>
#include <string.h>
#include <stdlib.h>
#include <esp_http_client.h>


#define BLINK_PERIOD (1000)
#define TICK_DELAY (BLINK_PERIOD / portTICK_PERIOD_MS)
#define WIFI_SSID "WolfnetMesh"
#define WIFI_PASSWD "koziadupa6666"
#define API_URL "http://192.168.1.115:5010"
#define REGISTERED 200
#define UNREGISTERED 401


void run_server();
void connect_to_wifi();
void http_request_task(void *pvParameters);
void button_task();
void blink(int blinks_count, int delay_ms);
void switch_light();
void status_func(char *response_str);
void light_switch_func(char *response_str);
void handle_click_cap(int next_status, int *clicks, int *status);
void register_endpoints(httpd_handle_t server, httpd_uri_t* handlers, size_t num_handlers);

esp_err_t status_endpoint(httpd_req_t *request);
esp_err_t light_switch_endpoint(httpd_req_t *request);
esp_err_t endpoint(httpd_req_t *request, void (*func)(char *response_str));

httpd_uri_t* create_endpoints(size_t *num_handlers);
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
    xTaskCreate(button_task, "button_task", 2048, NULL, 10, NULL);


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
    size_t endpoints_count;
    httpd_uri_t* handlers = create_endpoints(&endpoints_count);
    httpd_config_t httpd_cfg = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t httpd_handle;

    httpd_start(&httpd_handle, &httpd_cfg);
    register_endpoints(httpd_handle, handlers, endpoints_count);
}



httpd_uri_t* create_endpoints(size_t *endpoints_count) {
    *endpoints_count = 2; 
    httpd_uri_t* endpoints = malloc(*endpoints_count * sizeof(httpd_uri_t));

    // tu mozna dodac w prosty sposob kolejne endpointy, trzeba!!!!! zmineic *endpoints_count
    endpoints[0] = create_endpoint_handler(HTTP_GET, "/status", status_endpoint);
    endpoints[1] = create_endpoint_handler(HTTP_POST, "/light", light_switch_endpoint);
    
    return endpoints;
}



httpd_uri_t create_endpoint_handler(
    httpd_method_t method,
    const char *uri,
    esp_err_t (*handler)(httpd_req_t *request)
) {
    return (httpd_uri_t){
        .method = method,
        .uri = uri,
        .handler = handler,
    };
}



void register_endpoints(httpd_handle_t server, httpd_uri_t* endpoints, size_t endpoints_count) {
    for (size_t i = 0; i < endpoints_count; i++) {
        httpd_register_uri_handler(server, &endpoints[i]);
    }
}



esp_err_t endpoint(httpd_req_t *request, void (*func)(char *response_str)) {
    char response_str[100];
    func(response_str);

    httpd_resp_set_type(request, "application/json");
    httpd_resp_sendstr(request, response_str);
    return ESP_OK;
}



esp_err_t status_endpoint(httpd_req_t *request) { return endpoint(request, status_func); }
esp_err_t light_switch_endpoint(httpd_req_t *request) { return endpoint(request, light_switch_func); }



void status_func(char *response_str) {
    sprintf(response_str, "{\"uptime\":%d, \"light\": %s}", iterations, is_light_on ? "true" : "false");
}



void light_switch_func(char *response_str) {
    switch_light();
    sprintf(response_str, "{\"light\": %s}", is_light_on ? "true" : "false");
}


void switch_light(){
    is_light_on = !is_light_on;
    gpio_set_level(GPIO_NUM_2, is_light_on);
}


typedef struct {
    esp_http_client_handle_t client;
    int *status_code;
} http_task_params_t;



void http_request_task(void *pvParameters) {
    http_task_params_t *params = (http_task_params_t *)pvParameters;
    esp_http_client_handle_t client = params->client;

    esp_http_client_perform(client);
    esp_http_client_cleanup(client);

    *params->status_code = esp_http_client_get_status_code(client);
    vTaskDelete(NULL);
}


esp_http_client_handle_t create_client(esp_http_client_method_t  method) {
    return (esp_http_client_handle_t)esp_http_client_init(&(esp_http_client_config_t){
        .url = API_URL,
        .method = method
    });
}



void add_params(http_task_params_t *params, int status_code, httpd_method_t method) {
    params->status_code = &status_code;
    params->client = create_client(method);
}



void button_task(){
    int status = 401;
    int clicks = 0;

    http_task_params_t *register_params = malloc(sizeof(http_task_params_t));
    http_task_params_t *unregister_params = malloc(sizeof(http_task_params_t));

    add_params(register_params, status, HTTP_POST);
    add_params(unregister_params, status, HTTP_DELETE);

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(100));
        if (gpio_get_level(GPIO_NUM_0) != 0) { continue; }

        clicks += 1;
        int next_status = status == REGISTERED ? UNREGISTERED : REGISTERED;
        
        ESP_LOGE(TAG, "%d", next_status);
        handle_click_cap(next_status, &clicks, &status);
    } 
}



void handle_click_cap(int next_status, int *clicks, int *status) {
    int clicks_int = *clicks;
    int blinks_count = next_status == REGISTERED ? 1 : 3;
    int delay_ms = next_status == REGISTERED ? 500 : 250;

    bool is_clcik_cap = clicks_int == 20;
    if (!is_clcik_cap) { return; }

    ESP_LOGW(TAG, "click cap reached");
    blink(blinks_count, delay_ms);

    *status = next_status;
    *clicks = 0;
}


void blink(int blinks_count, int delay_ms){
    for (int i = 0; i < blinks_count; i++) {
        switch_light();
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
        switch_light();
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}
