/*
 * Quectel_mqtt.h
 *
 *  Created on: Jun 10, 2025
 *      Author: ERIC MULWA
 */

#ifndef QUECTEL_MQTT_H
#define QUECTEL_MQTT_H

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"
#include <math.h>
#include "esp_modbus_hub_app.h"
#include "driver/ledc.h"

// Definitions
#define MODEM_UART_NUM UART_NUM_1
#define TX_PIN 17
#define RX_PIN 16
#define RESET_PIN 19
#define PWR_PIN 18 
#define GSM_EN 4
#define ESP_BAUDRATE 115200
#define QUECTEL_BAUDRATE 115200
#define TAG_GSM "MODEM"
#define BUF_SIZE 832

// Thingsboard host
#define MQTT_BROKER "demo.thingsboard.io"
#define MQTT_PORT 1883

// Device provisioning credentials
extern char HUB_NAME[32];
extern char deviceSerial[12];		
#define DEVICE_NAME HUB_NAME
#define PROVISION_KEY ""
#define PROVISION_SECRET ""
#define CREDENTIALS_TYPE "MQTT_BASIC"
#define MQTT_USERNAME "" 
#define MQTT_PASSWORD "" 
#define MQTT_CLIENT_ID deviceSerial  		
// Device pre-provisioning credentials
#define MQTT_PROV_USERNAME "provision" 
#define MQTT_PROV_PASSWORD ""
extern bool prov_resub;
extern bool single_node;
extern bool node_change;
extern bool node_offline;
extern int slave_id;
extern int changed_node_id;
extern int32_t hub_number;

#define LEDC_TIMER            LEDC_TIMER_0
#define LEDC_CHANNEL_BUZZER   LEDC_CHANNEL_0

static const int RETRIES = 6;
static const int CMD_DELAY_MS = 200;

typedef struct {
    uint8_t slave_id;
    float Charger_InputVoltage;
    float Charger_OutputCurrent;
    uint8_t Charger_Enable;
    uint8_t MOSFET_Charge;
    uint8_t MOSFET_Discharge;
} ControlCommand;

// PUBLISHER RING BUFFER
#define MAX_COMMANDS  30   // Maximum commands in ring buffer
#define MAX_SLAVES        30          // Max expected slaves per scan
#define MAX_DATA_POINTS   MAX_SLAVES  // One entry per slave per read cycle
#define RS485_MAX_ADDRESS 30
typedef struct { 
	int   	Battery_Capacity;
	int     Mains_Status;
	int     Provision_Status;
	int     Battery_KWH;
	uint32_t Serial_Number;
	bms_common_data_t jk_data;
	charger_common_data_t charger;
} SlaveData;
// External definitions
extern QueueHandle_t commandQueue;
extern bool bufferReady;
extern char APN[32];
extern int dataCount;
extern int writeIndex; 
extern void resetWatchdog(int core_id);
extern void error(int count);
extern char* create_json_payloadi(int index);
extern char* create_json_payloadj(int index);
extern void create_node_reset_payload_split(char **jsonA, char **jsonB);
extern void playMqqtConfirmationTone(int ledc_channel, int ledc_timer);
extern void getChipIdString(char *deviceSerial, size_t size);
extern SlaveData ringBuffer[MAX_DATA_POINTS];
extern uint8_t valid_addresses[RS485_MAX_ADDRESS];
extern uint8_t valid_address_count;
// Thingsboard Provisioning
extern int32_t provision;
extern int load_provision_from_nvs();
extern void save_provision_to_nvs(int prov);

typedef struct {
    int request_id;
    char* response_payload;
} RpcResponse;

void uart_init();
void gsm_reset();
void gsm_poweron();
void hardware_poweroff();
void at_poweroff();
bool send_at_command(const char *command, const char *expected_response, int retries, uint32_t timeout_ms);
bool poweron_modem();
bool activate_pdp();
bool open_mqtts();
bool open_provision_mqtts();
bool mqtts_disconnect();
bool deactivate_pdp();
void powerdown_modem();
bool mqtts_init();
bool provision_mqtts_init();
void mqtts_deinit();
bool configure_mqtt_settings();
bool configure_mqtts_settings();
bool hex_to_ascii(const char* hex, char* output);
void handle_mqtt_error(int err_code);
void check_mqtt_link_status();
bool read_buffered_messages(int recv_id);
bool check_mqtt_urc();
bool mqtt_pubclient();
bool mqtt_provision_pubclient();
bool mqtt_dummypub();
bool mains_lost();
bool mqtt_subclient();
bool mqtt_provision_subclient();
bool mqtt_resubscribe();
void mqtts_error_reconnect();
void handle_rpc_state_request(int slave_id, int request_id, const char* param_name, RpcResponse* responses, int* response_count);
bool publish_rpc_responses(RpcResponse* responses, int count);
char* create_provision_json_payload();

#endif // QUECTEL_MQTT_H
