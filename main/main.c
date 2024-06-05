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


#define DEVICE_NAME "ALEX"
#define WIFI_SSID "TechniSchools"
#define WIFI_PASSWD "TS2023!@%"
#define API_URL "http://192.168.88.14:5000/device"
#define CONNECT_ENDPOINT "/activate"
#define DISCONNECT_ENDPOINT "/deactivate/ip"
#define BUTTON_PIN GPIO_NUM_0
#define LIGHT_PIN GPIO_NUM_2
#define MAIN_LOOP_TICK_DELAY ((1000) / portTICK_PERIOD_MS)
#define ACTIVATE_BLINKS_DELAY_MS ((500) / portTICK_PERIOD_MS)
#define DEACTIVATE_BLINKS_DELAY_MS ((800) / portTICK_PERIOD_MS)
#define BUTTON_LOOP_DELAY_MS ((100) / portTICK_PERIOD_MS)
#define ACTIVATE_BLINKS_COUNT 1
#define DEACTIVATE_BLINKS_COUNT 3
#define ERROR_BLINKS_COUNT 5
#define CLICKS_CAP 20
#define ALREADY_ACTIVATED 409
#define ACTIVATED 201
#define DEACTIVATED 204
#define REQUEST_ERRROR 0
#define ALREADY_DEACTIVATED 404


typedef struct {
    int blinks_count;
    int delay_ms;
} blink_data;


typedef struct {
    esp_http_client_handle_t client;
    int status_code;
} http_task_params_t;



void run_server();

void connect_to_wifi();

void http_request_task(void *pvParameters);

void button_task();

void blink(blink_data props);

void switch_light();

void switch_light_with_dealy(int delay);

void get_current_status_json(char *response_str);

void light_switch_endpoint_func(char *response_str);

void handle_click_cap(int *clicks_ptr);

void register_endpoints(httpd_handle_t server, httpd_uri_t* handlers, size_t num_handlers);

void add_params(http_task_params_t *params, esp_http_client_method_t method);

void handle_status_code(int status_code);

void perform_request(esp_http_client_method_t method);

blink_data get_blink_properties();

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
bool is_activated = false;
bool is_wifi_connected = false;
bool is_request_pending = false;
bool is_request_error = false;

// MAIN

void app_main(void){
    gpio_set_direction(LIGHT_PIN, GPIO_MODE_OUTPUT);
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
    connect_to_wifi();
    run_server();
    xTaskCreate(button_task, "button_task", 4096, NULL, 10, NULL);


    while (true){
        vTaskDelay(MAIN_LOOP_TICK_DELAY);
        ESP_LOGE(TAG, "iter: %d", iterations);
        iterations += 1;
    }
}



// WIFI 

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
    is_wifi_connected = true;
}



// HTTP SERVER

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

    ESP_LOGI(TAG, "response: %s", response_str);
    httpd_resp_set_type(request, "application/json");
    httpd_resp_sendstr(request, response_str);

    return ESP_OK;
}



esp_err_t status_endpoint(httpd_req_t *request) { return endpoint(request, get_current_status_json); }
esp_err_t light_switch_endpoint(httpd_req_t *request) { return endpoint(request, light_switch_endpoint_func); }



void get_current_status_json(char *response_str) {
    sprintf(response_str, "{\"uptime\":%d, \"is_light_on\": %s}", iterations, is_light_on ? "true" : "false");
}



void light_switch_endpoint_func(char *response_str) {
    switch_light();
    get_current_status_json(response_str);
}



// HTTP REQUEST

void prepare_payload(char *payload) {
    sprintf(payload, "{\"name\": \"%s\", \"is_light_on\": %s, \"uptime\": %d}", 
        DEVICE_NAME, 
        is_light_on ? "true" : "false", 
        iterations
    );
}


void http_request_task(void *pvParameters) {
    http_task_params_t *params = (http_task_params_t *)pvParameters;
    esp_http_client_handle_t client = params->client;
    
    char payload[100];
    is_request_pending = true;

    ESP_LOGI(TAG, "Sending HTTP request...");
    prepare_payload(payload);

    esp_http_client_set_post_field(client, payload, strlen(payload));
    esp_http_client_perform(client);

    int status_code = esp_http_client_get_status_code(client);

    handle_status_code(status_code);
    ESP_LOGI(TAG, "HTTP request completed with status %d", status_code);
    
    is_request_pending = false;
    is_request_error = false;
    
    esp_http_client_cleanup(client);
    free(params);
    vTaskDelete(NULL);
}


void change_status(bool new_status, char *msg){
    is_activated = new_status;
    ESP_LOGW(TAG, "Device %s !", msg);
}


void handle_status_code(int status_code){
    switch (status_code) {
        case ACTIVATED:
            change_status(true, "activated");
            break;

        case DEACTIVATED:
            change_status(false, "deactivated");
            break;

        case ALREADY_ACTIVATED:
            change_status(true, "already activated");
            break;

        case ALREADY_DEACTIVATED:
            change_status(false, "deactivated");
            perform_request(HTTP_METHOD_POST); // activate again
            return;

        case REQUEST_ERRROR:
            is_request_error = true;
            ESP_LOGE(TAG, "Unknown status code: %d", status_code);
            break;
    }

    blink_data b_props = get_blink_properties();
    blink(b_props);
}



void join_path(char *uri, const char *base, const char *endpoint) {
    snprintf(uri, 256, "%s%s", base, endpoint);
}



esp_http_client_handle_t create_client(esp_http_client_method_t  method) {
    char *path = method == HTTP_METHOD_POST ? CONNECT_ENDPOINT : DISCONNECT_ENDPOINT;
    char url[256]; 

    join_path(url, API_URL, path);
    ESP_LOGI(TAG, "url: %s", url);
    
    esp_http_client_handle_t client = esp_http_client_init(&(esp_http_client_config_t){
        .url = url,
        .method = method
    });

    esp_http_client_set_header(client, "Content-Type", "application/json");
    return client;
}



void add_params(http_task_params_t *params, esp_http_client_method_t method) {
    params->client = create_client(method);
}



void perform_request(esp_http_client_method_t method){ 
    http_task_params_t *params = malloc(sizeof(http_task_params_t));
    
    add_params(params, method);
    xTaskCreate(http_request_task, "http_request_task", 4096, (void *)params, 10, NULL);    
}



// BUTTON

void button_task(){
    int clicks = 0;

    while (true) {
        vTaskDelay(BUTTON_LOOP_DELAY_MS);
        
        if (gpio_get_level(BUTTON_PIN) != 0) { continue; }
        if (!is_wifi_connected || is_request_pending) { continue; }
        
        clicks += 1;
        ESP_LOGI(TAG, "Button clicked");
        handle_click_cap(&clicks);
    } 
}



void handle_click_cap(int *clicks_ptr) {
    int clicks = *clicks_ptr;
    
    if (!(clicks == CLICKS_CAP)) { return; }
    ESP_LOGW(TAG, "click cap reached");

    *clicks_ptr = 0;
    is_activated ? perform_request(HTTP_METHOD_DELETE) : perform_request(HTTP_METHOD_POST);
}



// LIGHT 

blink_data get_blink_properties(){
    return (blink_data){
       .blinks_count = is_activated ? ACTIVATE_BLINKS_COUNT : is_request_error ? ERROR_BLINKS_COUNT : DEACTIVATE_BLINKS_COUNT,
        .delay_ms = is_activated ? ACTIVATE_BLINKS_DELAY_MS : DEACTIVATE_BLINKS_DELAY_MS
    };
}



void switch_light(){
    is_light_on = !is_light_on;
    gpio_set_level(LIGHT_PIN, is_light_on);
}



void blink(blink_data props){
    for (int i = 0; i < props.blinks_count; i++) {
        switch_light_with_dealy(props.delay_ms); // on
        switch_light_with_dealy(props.delay_ms); // off
    }
}



void switch_light_with_dealy(int delay){
    vTaskDelay(pdMS_TO_TICKS(delay));
    switch_light();
}