/*
 * Quectel_mqtt.c
 *
 *  Created: 10/06/2025
 *	Modified: 20/06/2025
 *    
 *		Author: ERIC MULWA
 */

#include "Quectel_mqtt.h"
char urcbuffer[BUF_SIZE];
static bool uart_installed = false;

/**
*  @brief UART Configuration
*/
void uart_init() {
    if (!uart_installed) {
	    uart_config_t uart_config = {
	        .baud_rate = QUECTEL_BAUDRATE,
	        .data_bits = UART_DATA_8_BITS,
	        .parity = UART_PARITY_DISABLE,
	        .stop_bits = UART_STOP_BITS_1,
	        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
	    };
	
	    uart_driver_install(MODEM_UART_NUM, BUF_SIZE * 2, 0, 0, NULL, 0);
	    uart_param_config(MODEM_UART_NUM, &uart_config);
	    uart_set_pin(MODEM_UART_NUM, TX_PIN, RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

        uart_installed = true;  // mark driver as installed
        ESP_LOGI("UART", "UART driver installed.");
    } else {
        ESP_LOGI("UART", "UART driver already installed, skipping.");
    }
}

/**
*  @brief Hardware Reset
*/
void gsm_reset() {
	esp_rom_gpio_pad_select_gpio(RESET_PIN);
    gpio_set_direction(RESET_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(RESET_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(500));
    gpio_set_level(RESET_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(500));
    gpio_set_level(RESET_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(5000));
}

/**
*  @brief Hardware Power On
*/
void gsm_poweron() {
	// connect to power
    esp_rom_gpio_pad_select_gpio(GSM_EN);
	gpio_set_direction(GSM_EN, GPIO_MODE_OUTPUT);
	gpio_set_level(GSM_EN, 1);
	vTaskDelay(pdMS_TO_TICKS(1000));
	// power on with PWR 
	esp_rom_gpio_pad_select_gpio(PWR_PIN);
    gpio_set_direction(PWR_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(PWR_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(500)); // minimum of 500ms
    gpio_set_level(PWR_PIN, 1); 
    vTaskDelay(pdMS_TO_TICKS(500)); 
    gpio_set_level(PWR_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(5000)); // Wait for the module to initialize (minimum of 10s)
}

/**
*  @brief Hardware Power Down
*/
void hardware_poweroff() {
	esp_rom_gpio_pad_select_gpio(PWR_PIN);
    gpio_set_direction(PWR_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(PWR_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(500)); // minimum of 500ms
    gpio_set_level(PWR_PIN, 1); 
    vTaskDelay(pdMS_TO_TICKS(500));
    gpio_set_level(PWR_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(500)); 
	// Disconnect power from Quectel   
	gpio_set_level(GSM_EN, 0);
}

/**
*  @brief AT Command Power Down
*/
void at_poweroff() {
	bool power_down = false;
    for (int attempt = 0; attempt < 3; attempt++) {
        if (!send_at_command("AT+QPOWD=1", "OK", RETRIES, CMD_DELAY_MS)) {
            ESP_LOGW(TAG_GSM, "AT command power down failed on attempt %d, retrying...\n", attempt + 1);
        } else {
            power_down = true;
            ESP_LOGI(TAG_GSM, "Modem powered down successfully!\n");       
			gpio_set_level(GSM_EN, 0); // Disconnect power from Quectel    
			break;     
        }
        
    } if (!power_down) {
		error(2);
        ESP_LOGE(TAG_GSM, "AT command power down failed, exiting task...\n");
    }
  return;
}

/**
*  @brief Boolean to send AT Commands with response check
*/
bool send_at_command(const char *command, const char *expected_response, int retries, uint32_t timeout_ms) {

    char response[BUF_SIZE];
    for (int attempt = 0; attempt < retries; attempt++) {
		uart_flush_input(MODEM_UART_NUM);
        uart_write_bytes(MODEM_UART_NUM, command, strlen(command));
        uart_write_bytes(MODEM_UART_NUM, "\r\n", 2);

        uint64_t start_time = esp_timer_get_time();
        int index = 0;

        while ((esp_timer_get_time() - start_time) < (timeout_ms * 1000)) {
            uint8_t data;
            if (uart_read_bytes(MODEM_UART_NUM, &data, 1, 10 / portTICK_PERIOD_MS) > 0) {
                if (index < BUF_SIZE - 1) {
                    response[index++] = (char)data;
                }
            }
        }
        response[index] = '\0';
        if (strstr(command, "AT+QMTRECV=0")) {
			memset(urcbuffer, 0, BUF_SIZE);
			strcpy(urcbuffer, response);
        } 
        
		ESP_LOGI(TAG_GSM, "Command: %s, Response: %s", command, response);			

        if (strstr(response, expected_response)) {
            return true;
        }

        if (strstr(command, "AT+QMTRECV=0")) {
			// Skip showing ufs slot scans
		} else {
        ESP_LOGW(TAG_GSM, "Attempt %d: Command '%s' failed. Retrying...\n", attempt + 1, command);
        vTaskDelay(pdMS_TO_TICKS(10));
        }
        
       vTaskDelay(pdMS_TO_TICKS(timeout_ms * (attempt + 1))); // progressive back-off for AT commands
    }
    
    if (strstr(command, "AT+QMTRECV=0")) {
		// Skip showing ufs slot scans
	} else {
		error(2);
    	ESP_LOGE(TAG_GSM, "Command '%s' failed after %d retries.\n", command, retries);
    }
    return false;
}

/**
*  @brief Publisher Client 
*/
bool mqtt_pubclient() {	
if (single_node) {
		// Meta & Charger Data
		int indexi;
		if (node_change) {
			indexi = changed_node_id;
		} else {
			indexi = slave_id;
		}
		
		char *payloadi = create_json_payloadi(indexi - 1);
	    bool qpubi = false;
		char pub_cmdi[100];
		sprintf(pub_cmdi, "AT+QMTPUBEX=0,0,0,0,\"ksc/uplink/telemetry\",%d", strlen(payloadi)); 
		    for (int attempt = 0; attempt < 2; attempt++) {
				if (!send_at_command(pub_cmdi, ">", RETRIES, CMD_DELAY_MS)) {
					error(2);
				    ESP_LOGE(TAG_GSM, "MQTT publish command failed!");
				    
	                continue;
				} else {
			        uart_write_bytes(MODEM_UART_NUM, payloadi, strlen(payloadi));
			        vTaskDelay(pdMS_TO_TICKS(10));
			        uart_write_bytes(MODEM_UART_NUM, "\x1A", 1); // terminate
			        vTaskDelay(pdMS_TO_TICKS(3));
			        qpubi = true;	        
			        ESP_LOGI(TAG_GSM, "Payload published successfully!\n");   
			        free(payloadi);
			        break; 
				}
		    }
	    if (!qpubi) {
			error(2);
		    ESP_LOGE(TAG_GSM, "MQTT publish command failed after 3 attempts and exited.");
		    free(payloadi);
		    return false;
	    }
	   resetWatchdog(0);
	
		// BMS Data
		int indexj;
		if (node_change) {
			indexj = changed_node_id;
		} else {
			indexj = slave_id;
		}
		
		char *payloadj = create_json_payloadj(indexj - 1); 
	    bool qpubj = false;
		char pub_cmdj[100];
		sprintf(pub_cmdj, "AT+QMTPUBEX=0,0,0,0,\"ksc/uplink/telemetry\",%d", strlen(payloadj)); 
		    for (int attempt = 0; attempt < 2; attempt++) {
				if (!send_at_command(pub_cmdj, ">", RETRIES, CMD_DELAY_MS)) {
					error(2);
				    ESP_LOGE(TAG_GSM, "MQTT publish command failed!");
				    
	                continue;
				} else {
					uart_write_bytes(MODEM_UART_NUM, payloadj, strlen(payloadj));
			        vTaskDelay(pdMS_TO_TICKS(10));
			        uart_write_bytes(MODEM_UART_NUM, "\x1A", 1); // terminate
			        vTaskDelay(pdMS_TO_TICKS(3));
			        qpubj = true;	        
			        ESP_LOGI(TAG_GSM, "Payload published successfully!\n");   
			        printf("Node %d payload published! \n", indexj);
			        free(payloadj);
			        break; 
				}
		    }
	    if (!qpubj) {
			error(2);
		    ESP_LOGE(TAG_GSM, "MQTT publish command failed after 3 attempts and exited.");
		    free(payloadj);
		    return false;
	    }
	
	   node_offline = false;
	   node_change = false;
	   single_node = false;	    
	   resetWatchdog(0);
	   return true;	   	
	}

    for (int i = 0; i < valid_address_count; i++) {
        if (valid_addresses[i] == 0)
        {
            error(2);
            continue;
        } 
        
		// Meta & Charger Data
		char *payloadi = create_json_payloadi(valid_addresses[i] - 1);
	    bool qpubi = false;
		char pub_cmdi[100];
		sprintf(pub_cmdi, "AT+QMTPUBEX=0,0,0,0,\"ksc/uplink/telemetry\",%d", strlen(payloadi)); 
		    for (int attempt = 0; attempt < 2; attempt++) {
				if (!send_at_command(pub_cmdi, ">", RETRIES, CMD_DELAY_MS)) {
					error(2);
				    ESP_LOGE(TAG_GSM, "MQTT publish command failed!");
				    
                    continue;
				} else {
			        uart_write_bytes(MODEM_UART_NUM, payloadi, strlen(payloadi));
			        vTaskDelay(pdMS_TO_TICKS(12));
			        uart_write_bytes(MODEM_UART_NUM, "\x1A", 1); // terminate
			        vTaskDelay(pdMS_TO_TICKS(3));
			        qpubi = true;	        
			        ESP_LOGI(TAG_GSM, "Node: %d payload published successfully!\n", valid_addresses[i]); 
			        free(payloadi); 
			        break; 
				}
		    }
	    if (!qpubi) {
			error(2);
		    ESP_LOGE(TAG_GSM, "MQTT publish command failed after 3 attempts and exited.");
		    free(payloadi);
		    return false;
	    }
	   resetWatchdog(0);
	
		// BMS Data
		char *payloadj = create_json_payloadj(valid_addresses[i] - 1); 
	    bool qpubj = false;
		char pub_cmdj[100];
		sprintf(pub_cmdj, "AT+QMTPUBEX=0,0,0,0,\"ksc/uplink/telemetry\",%d", strlen(payloadj)); 
		    for (int attempt = 0; attempt < 2; attempt++) {
				if (!send_at_command(pub_cmdj, ">", RETRIES, CMD_DELAY_MS)) {
					error(2);
				    ESP_LOGE(TAG_GSM, "MQTT publish command failed!");
				    
                    continue;
				} else {
			        uart_write_bytes(MODEM_UART_NUM, payloadj, strlen(payloadj));
			        vTaskDelay(pdMS_TO_TICKS(12));
			        uart_write_bytes(MODEM_UART_NUM, "\x1A", 1); // terminate
			        vTaskDelay(pdMS_TO_TICKS(3));
			        qpubj = true;	        
			        ESP_LOGI(TAG_GSM, "Payload: %d published successfully!\n", i);
			        printf("%d, ",valid_addresses[i]);   
			        free(payloadj);
			        break; 
				}
		    }
	    if (!qpubj) {
			error(2);
		    ESP_LOGE(TAG_GSM, "MQTT publish command failed after 3 attempts and exited.");
		    free(payloadj);
		    return false;
	    }
	    
	   resetWatchdog(0);
  }
	printf("node(s) payloads published. \n\n"); 
	
	// Publish split JSON payloads (payloadA first, then payloadB)
	char *payloadA, *payloadB;
	create_node_reset_payload_split(&payloadA, &payloadB);
	char *payloads[2] = { payloadA, payloadB };
	for (int i = 0; i < 2; i++) {
	    bool qpubn = false;
	    char pub_cmdn[150];
	    // Select which payload to publish
	    char *currentPayload = payloads[i];
	    int payloadLen = strlen(currentPayload);
	    // Build PUBEX command for this payload
	    sprintf(pub_cmdn,
	            "AT+QMTPUBEX=0,0,0,0,\"ksc/uplink/telemetry\",%d",
	            payloadLen);
	    for (int attempt = 0; attempt < 2; attempt++) {
	        if (!send_at_command(pub_cmdn, ">", RETRIES, CMD_DELAY_MS)) {
	            error(2);
	            ESP_LOGE(TAG_GSM, "MQTT publish command failed!");
	            
	            continue;
	        }
	        uart_write_bytes(MODEM_UART_NUM, currentPayload, payloadLen);
	        vTaskDelay(pdMS_TO_TICKS(12));
	        uart_write_bytes(MODEM_UART_NUM, "\x1A", 1); // terminate
	        vTaskDelay(pdMS_TO_TICKS(3));
	        qpubn = true;
	        ESP_LOGI(TAG_GSM, "Node SN Reset Payload %d published successfully!\n", i + 1);
	        printf("SN%d, ",i);
	        break;
	    }
	    if (!qpubn) {
	        error(2);
	        ESP_LOGE(TAG_GSM, "MQTT Node SN Reset publish command failed after 2 attempts.");
	        free(payloadA);
	        free(payloadB);
	        return true;
	    }
	
	    resetWatchdog(0);
	}
	// Free memory AFTER both sends succeed
	free(payloadA);
	free(payloadB);
	
	memset(ringBuffer, 0, sizeof(ringBuffer));
	bufferReady = false;
	writeIndex = 0;
    dataCount = 0; 	
	return true;
}

/**
*  @brief provisioning Publisher Client
*/
bool mqtt_provision_pubclient() {	
	char *payload = create_provision_json_payload();
    bool qpub = false;
	char pub_cmdi[100];
	ESP_LOGI(TAG_GSM, "Generated provisioning JSON Payload: %s\n", payload);
	vTaskDelay(pdMS_TO_TICKS(10));
	sprintf(pub_cmdi, "AT+QMTPUBEX=0,0,0,0,\"/provision/request\",%d", strlen(payload)); 
	    for (int attempt = 0; attempt < 2; attempt++) {
			if (!send_at_command(pub_cmdi, ">", RETRIES, CMD_DELAY_MS)) {
				error(2);
			    ESP_LOGE(TAG_GSM, "provisioning MQTT publish command failed!");
			    
                continue;
			} else {
		        uart_write_bytes(MODEM_UART_NUM, payload, strlen(payload));
		        vTaskDelay(pdMS_TO_TICKS(200));
		        uart_write_bytes(MODEM_UART_NUM, "\x1A", 1); // terminate
		        vTaskDelay(pdMS_TO_TICKS(50));
		        qpub = true;	        
		        printf("provisioning Payload: published successfully!\n");   
		        free(payload);
		        break; 
			}
	    }
    if (!qpub) {
		error(2);
	    ESP_LOGE(TAG_GSM, "provisioning MQTT publish command failed after 3 attempts and exited.");
	    free(payload);
	    return false;
    }
       
 return true;
}

/**
 *  @brief Dummy Publisher Client to keep subscriber connection active
 *  Sends a dummy payload that the subscriber can safely ignore
 */
bool mqtt_dummypub() {	 
    bool dummypub = false;
	char pub_cmd[100];
	const char *payload = "ping";
	sprintf(pub_cmd, "AT+QMTPUBEX=0,0,0,0,\"ksc/uplink/telemetry\",%d", strlen(payload));
	for (int attempt = 0; attempt < 3; attempt++) {
		if (!send_at_command(pub_cmd, ">", RETRIES, CMD_DELAY_MS)) {
			error(2);
		    ESP_LOGE(TAG_GSM, "MQTT dummy publish failed on attempt %d", attempt + 1);
            continue;
		} else {
	        uart_write_bytes(MODEM_UART_NUM, payload, strlen(payload));
	        vTaskDelay(pdMS_TO_TICKS(12));
	        uart_write_bytes(MODEM_UART_NUM, "\x1A", 1); // terminate
	        vTaskDelay(pdMS_TO_TICKS(3));
	        dummypub = true;	         
	        ESP_LOGI(TAG_GSM, "Dummy payload published successfully!");
	        break; 
		}
    }

    if (!dummypub) {
		error(2);
	    ESP_LOGE(TAG_GSM, "MQTT dummy publish failed after 3 attempts.");
	    return false;
    }

    return true;
}


/**
 *  @brief Mains Power Lost Publisher Client to notify about blackouts
 */
bool mains_lost() {	 
	// Publish split JSON payloads (payloadA first, then payloadB)
	char *payloadA, *payloadB;
	create_node_reset_payload_split(&payloadA, &payloadB);
	char *payloads[2] = { payloadA, payloadB };
	for (int i = 0; i < 2; i++) {
	    bool mains_lost = false;
	    char mains_cmd[150];
	    // Select which payload to publish
	    char *currentMainsPayload = payloads[i];
	    int payloadLen = strlen(currentMainsPayload);
	    // Build PUBEX command for this payload
	    sprintf(mains_cmd,
	            "AT+QMTPUBEX=0,0,0,0,\"ksc/uplink/telemetry\",%d",
	            payloadLen);
	    for (int attempt = 0; attempt < 2; attempt++) {
	        if (!send_at_command(mains_cmd, ">", RETRIES, CMD_DELAY_MS)) {
	            error(2);
	            ESP_LOGE(TAG_GSM, "MQTT publish command failed!");
	            
	            continue;
	        }
	        uart_write_bytes(MODEM_UART_NUM, currentMainsPayload, payloadLen);
	        vTaskDelay(pdMS_TO_TICKS(12));
	        uart_write_bytes(MODEM_UART_NUM, "\x1A", 1); // terminate
	        vTaskDelay(pdMS_TO_TICKS(3));
	        mains_lost = true;
	        ESP_LOGI(TAG_GSM, "Node SN Reset Payload %d published successfully!\n", i + 1);
	        printf("SN%d, ",i);
	        break;
	    }
	    if (!mains_lost) {
	        error(2);
	        ESP_LOGE(TAG_GSM, "MQTT Node SN Reset publish command failed after 2 attempts.");
	        free(payloadA);
	        free(payloadB);
	        return false;
	    }
	    resetWatchdog(0);
	}
	// Free memory AFTER both sends succeed
	free(payloadA);
	free(payloadB);
    return true;
}

/**
*  @brief MQTTs Main Subscribe Client
*/
bool mqtt_subclient() {	
	// init mqtts
    bool mqttinit = false;
    for (int attempt = 0; attempt < 2; attempt++) {
		if (!mqtts_init()) {
		    ESP_LOGW(TAG_GSM, "MQTT initialization failed on attempt %d. Retrying...", attempt + 1);
        } else {
            mqttinit = true;
            printf("MQTT initialization successful!\n");
            break; 
        }
    }
    if (!mqttinit) {
		error(2);
        ESP_LOGE(TAG_GSM, "MQTT initialization failed after %d retries.", RETRIES);
        powerdown_modem();
        return false;
    } 
   
	// subscribe to a topic 
    bool qsub = false;
	char sub_cmd[100];
	sprintf(sub_cmd, "AT+QMTSUB=0,1,\"v1/devices/me/rpc/request/+\",0");
    for (int attempt = 0; attempt < 3; attempt++) {
		if (!send_at_command(sub_cmd, "+QMTSUB: 0,1,0,0", RETRIES, CMD_DELAY_MS)) {
			error(2);
		    ESP_LOGE(TAG_GSM, "Subscription to set topic failed!");
		} else {
	        qsub = true;	        
	        printf("Subscribed to set topic successfully!\n"); 
	        // off-loading to the message reception handler  
	        break; 
		}
    }
    if (!qsub) {
		error(2);
	    ESP_LOGE(TAG_GSM, "Subscription to set topic failed after 3 attempts and exited!.");
	    return false;
    }
 return true;
}

/**
 * @brief Thingsboard Hub provisioning 
 * @return true 
 * @return false 
 */
bool mqtt_provision_subclient() {	
	// init provisioning mqtts
    bool prov_mqttinit = false;
    for (int attempt = 0; attempt < 2; attempt++) {
		if (!provision_mqtts_init()) {
		    ESP_LOGW(TAG_GSM, "Provisioning MQTT initialization failed on attempt %d. Retrying...", attempt + 1);
        } else {
            prov_mqttinit = true;
            printf("Provisioning MQTT initialization successful!\n");
            break; 
        }
    }
    if (!prov_mqttinit) {
		error(2);
        ESP_LOGE(TAG_GSM, "Provisioning MQTT initialization failed after %d retries.", RETRIES);
        powerdown_modem();
        return false;
    } 
 

	// subscribe to the provisioning topic 
    bool qsub = false;
	char sub_cmd[100];
    sprintf(sub_cmd, "AT+QMTSUB=0,1,\"/provision/response\",0");
    for (int attempt = 0; attempt < 3; attempt++) {
		if (!send_at_command(sub_cmd, "+QMTSUB: 0,1,1,1", RETRIES, CMD_DELAY_MS)) {
			error(2);
		    ESP_LOGE(TAG_GSM, "Subscription to provisioning topic failed!");
		} else {
	        qsub = true;	
	        prov_resub = true;    
	        printf("Subscribed to provisioning topic successfully!\n"); 
	        // off-loading to the message reception handler  
	        break; 
		}
    }
    if (!qsub) {
		error(2);
	    ESP_LOGE(TAG_GSM, "Subscription to provisioning topic failed after 3 attempts and exited!.");
	    return false;
    }
 return true;
}

/**
*  @brief MQTTs Re-subscribe to the same topic
*/
bool mqtt_resubscribe() {	
    bool qsub = false;
	char sub_cmd[100];
	
	if(prov_resub) { 
		sprintf(sub_cmd, "AT+QMTSUB=0,1,\"/provision/response\",0");
	} else {
		sprintf(sub_cmd, "AT+QMTSUB=0,1,\"v1/devices/me/rpc/request/+\",0");
	}
	
    for (int attempt = 0; attempt < 2; attempt++) {
		if (!send_at_command(sub_cmd, "+QMTSUB: 0,1,0,0", RETRIES, CMD_DELAY_MS)) {
			error(2);
		    ESP_LOGE(TAG_GSM, "Subscription to set topic failed!");
		} else {
	        qsub = true;	        
	        printf("Subscribed to set topic successfully!\n"); 
	        // off-loading to the message reception handler  
	        break; 
		}
    }
    if (!qsub) {
		error(2);
	    ESP_LOGE(TAG_GSM, "Subscription to set topic failed after 3 attempts and exited!.");
	    return false;
    }
 return true;
}

/**
*  @brief Init uart and power on Modem
*/
bool poweron_modem() {
    uart_init();
    vTaskDelay(pdMS_TO_TICKS(10));
	esp_rom_gpio_pad_select_gpio(PWR_PIN);
    gpio_set_direction(PWR_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(PWR_PIN, 0);
	esp_rom_gpio_pad_select_gpio(RESET_PIN);
    gpio_set_direction(RESET_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(RESET_PIN, 0);
	gsm_poweron();
 
 	// Quectel GSM Power On Status Check
    bool power = false;
    for (int attempt = 0; attempt < 2; attempt++) {
        if (!send_at_command("AT", "OK", RETRIES, CMD_DELAY_MS)) {
            ESP_LOGW(TAG_GSM, "Modem is Off on check %d. Retrying...", attempt + 1);
            gsm_poweron();
            vTaskDelay(pdMS_TO_TICKS(10 * (attempt + 1)));
            continue;
        } else {
            power = true;
            printf("Modem is On, Proceeding...\n");
            break; 
        }
    }
    if (!power) {
		error(2);
        ESP_LOGE(TAG_GSM, "GSM Power Status Check failed after %d retries.", RETRIES);
        return false;
    } 
  return true;	
}

/**
*  @brief Latch to Network, Activate PDP & configure MQTTs
*/
bool activate_pdp() {
	// GSM Network Latch Check
    bool latch = false;
    for (int attempt = 0; attempt < 5; attempt++) {
        if (!send_at_command("AT+CGREG?", "5", RETRIES, CMD_DELAY_MS)) {
            ESP_LOGW(TAG_GSM, "No Network Latch on attempt %d. Retrying...", attempt + 1);
            vTaskDelay(pdMS_TO_TICKS(10 * (attempt + 1)));
            continue;
        } else {
            latch = true;
            printf("GSM Network Latch successful!\n");
            break; 
        }
    }
    if (!latch) {
		error(2);
        ESP_LOGE(TAG_GSM, "GSM Network Latch failed after %d retries.", RETRIES);
        return false;
    }
 	// PDP Context configuration and PDP Activation
	char apncommand[60];
	sprintf(apncommand, "AT+QICSGP=1,1,\"%s\",\"\",\"\",1", APN);
    bool success = false;
    for (int attempt = 0; attempt < RETRIES; attempt++) {
        if (!send_at_command(apncommand, "OK", RETRIES, CMD_DELAY_MS) ||
            !send_at_command("AT+QIACT=1", "OK", RETRIES, CMD_DELAY_MS)) {
            ESP_LOGW(TAG_GSM, "GSM setup failed on attempt %d. Retrying...\n", attempt + 1);
            vTaskDelay(pdMS_TO_TICKS(10 * (attempt + 1)));
            continue;
        } else {
            success = true;
            ESP_LOGI(TAG_GSM, "GSM setup (PDP Configuration and Activation) successful!\n");
            break; 
        }
    }
    if (!success) {
		error(2);
        ESP_LOGE(TAG_GSM, "GSM setup failed after %d retries.", RETRIES);
        return false;
    }
    
 	// Configure MQTT SSL
    bool sslmode = false;
    for (int attempt = 0; attempt < 3; attempt++) {
		if (!configure_mqtt_settings()) {
			error(2);
		    ESP_LOGE(TAG_GSM, "MQTT SSL configuration failed");
	        return false;
        } else {
            sslmode = true;
            printf("SSL configuration successful.\n");
            break; 
        }
    }
    if (!sslmode) {
		error(2);
        ESP_LOGE(TAG_GSM, "MQTT SSL configuration failed after %d retries.", RETRIES);
        return false;
    }
  return true;	
}

/**
*  @brief Open and connect to mqtts 
*/
bool open_mqtts() {
 	// Open MQTT connection
    bool openq = false;
    char mqtt_open_cmd[100];
    sprintf(mqtt_open_cmd, "AT+QMTOPEN=0,\"%s\",%d", MQTT_BROKER, MQTT_PORT);
    for (int attempt = 0; attempt < 3; attempt++) {
	    if (!send_at_command(mqtt_open_cmd, "+QMTOPEN: 0,0", RETRIES, 2000)) {
			error(2);
	        ESP_LOGE(TAG_GSM, "MQTT open failed\n");
	        return false;
        } else {
            openq = true;
            ESP_LOGI(TAG_GSM, "MQTT opened successfully!\n");
            break; 
        }
    }
    if (!openq) {
		error(2);
        ESP_LOGE(TAG_GSM, "MQTT open failed after %d retries.", RETRIES);
        return false;
    }
   
 	// Connect to MQTT broker (with credentials if required)
    bool conn = false;
	char mqtt_conn_cmd[150];
	
	if(prov_resub) { 
		sprintf(mqtt_conn_cmd, "AT+QMTCONN=0,\"%s\",\"%s\"", MQTT_PROV_USERNAME, MQTT_PROV_PASSWORD);
	} else {
		sprintf(mqtt_conn_cmd, "AT+QMTCONN=0,\"%s\",\"%s\",\"%s\"",  MQTT_CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD);
	}
	
    for (int attempt = 0; attempt < 3; attempt++) {
    	if (!send_at_command(mqtt_conn_cmd, "+QMTCONN: 0,0", RETRIES, 2000)) {
			error(2);
	        ESP_LOGE(TAG_GSM, "MQTT connect failed!");
	        return false;
        } else {
            conn = true;
            ESP_LOGI(TAG_GSM, "MQTT connect successful!");
            break; 
        }
    }
    if (!conn) {
		error(2);
        ESP_LOGE(TAG_GSM, "MQTT connect failed after %d retries.", RETRIES);
        return false;
    } 	
	
  return true;	
}

/**
*  @brief Open and connect to mqtts
*/
bool open_provision_mqtts() { 
 	// Open MQTT connection
    bool openq = false;
    char mqtt_open_cmd[100];
    sprintf(mqtt_open_cmd, "AT+QMTOPEN=0,\"%s\",%d", MQTT_BROKER, MQTT_PORT);
    for (int attempt = 0; attempt < 3; attempt++) {
	    if (!send_at_command(mqtt_open_cmd, "+QMTOPEN: 0,0", RETRIES, 2000)) {
			error(2);
	        ESP_LOGE(TAG_GSM, "provisioning MQTT open failed\n");
	        return false;
        } else {
            openq = true;
            ESP_LOGI(TAG_GSM, "provisioning MQTT opened successfully!\n");
            break; 
        }
    }
    if (!openq) {
		error(2);
        ESP_LOGE(TAG_GSM, "provisioning MQTT open failed after %d retries.", RETRIES);
        return false;
    }
 
 	// Connect to MQTT broker 
    bool conn = false;
	char mqtt_conn_cmd[150];
	sprintf(mqtt_conn_cmd, "AT+QMTCONN=0,\"%s\",\"%s\"", MQTT_PROV_USERNAME, MQTT_PROV_PASSWORD);
	
    for (int attempt = 0; attempt < 3; attempt++) {
    	if (!send_at_command(mqtt_conn_cmd, "+QMTCONN: 0,0", RETRIES, CMD_DELAY_MS)) {
			error(2);
	        ESP_LOGE(TAG_GSM, "provisioning MQTT connect failed!");
	        return false;
        } else {
            conn = true;
            ESP_LOGI(TAG_GSM, "provisioning MQTT connect successful!");
            break; 
        }
    }
    if (!conn) {
		error(2);
        ESP_LOGE(TAG_GSM, "provisioning MQTT connect failed after %d retries.", RETRIES);
        return false;
    } 	
	
  return true;	
}

/**
*  @brief Disconnect MQTTs
*/
bool mqtts_disconnect() {
	bool disconnect = false;
	for (int attempt = 0; attempt < 2; attempt++) {
	    if (!send_at_command("AT+QMTDISC=0", "+QMTDISC: 0,0", RETRIES, CMD_DELAY_MS)) {
	        ESP_LOGW(TAG_GSM, "Failed to disconnect from mqtt on attempt %d, retrying...", attempt + 1);
	    } else {
	        ESP_LOGI(TAG_GSM, "Disconnected from mqtt successfully on attempt %d.", attempt + 1);
	        disconnect = true;
	        break;
	    }    
	} if (!disconnect) {
		error(2);
	    ESP_LOGE(TAG_GSM, "Failed to disconnect from mqtt after 2 attempts.");
	    return false;
	}
  return true;	
}

/**
*  @brief Deactivate pdp context
*/
bool deactivate_pdp() {
	const int retries_pdp_deactivate = 2;
	bool pdp_deactivated = false;
	for (int attempt = 0; attempt < retries_pdp_deactivate; attempt++) {
	    if (!send_at_command("AT+QIDEACT=1", "OK", RETRIES, CMD_DELAY_MS)) {
	        ESP_LOGW(TAG_GSM, "Failed to deactivate PDP context on attempt %d, retrying...", attempt + 1);
	    } else {
	        ESP_LOGI(TAG_GSM, "PDP context deactivated successfully on attempt %d.", attempt + 1);
	        pdp_deactivated = true;
	        break;
	    }    
	} if (!pdp_deactivated) {
		error(2);
	    ESP_LOGE(TAG_GSM, "Failed to deactivate PDP context after %d attempts.", retries_pdp_deactivate);
	    return false;
	}
  return true;	
}

/**
*  @brief Power down modem
*/
void powerdown_modem() {
    const int retries_modem_power_down = 2;
    bool modem_powered_down = false;
    for (int attempt = 0; attempt < retries_modem_power_down; attempt++) {
        if (!send_at_command("AT+QPOWD=1", "OK", RETRIES, CMD_DELAY_MS)) {
            ESP_LOGW(TAG_GSM, "Failed to AT power down modem on attempt %d, retrying...\n", attempt + 1);
        } else {
            modem_powered_down = true;
            printf("Modem powered down successfully!\n");
            break;
        }
        
    } if (!modem_powered_down) {
		error(2);
        ESP_LOGE(TAG_GSM, "AT command power down failed after %d retries. Initiating Hardware Power Down...\n", retries_modem_power_down);
        hardware_poweroff();
    }

	// Clean up
    if (uart_installed) {
        //uart_driver_delete(MODEM_UART_NUM); // Disable de-initialization due to uart_driver_error issues
        uart_installed = false; // mark driver as uninstalled
        //ESP_LOGI("UART", "UART driver deleted.");
    } else {
        ESP_LOGI("UART", "UART driver not installed, skipping deletion.");
    }
    
  return;	
}

/**
*  @brief Initialize MQTTs
*/
bool mqtts_init() {
	if (!poweron_modem() || !activate_pdp() || !open_mqtts()) {
		error(2);
	    ESP_LOGE(TAG_GSM, "MQTTs initialization failed!");
	    powerdown_modem();
        return false;
    } else {
        printf("MQTTs initialized successfully!\n");
    }
   return true;
}

/**
*  @brief Initialize Provisioning MQTTs 
*/
bool provision_mqtts_init() {
	if (!poweron_modem() || !activate_pdp() || !open_provision_mqtts()) {
		error(2);
	    ESP_LOGE(TAG_GSM, "provisioning MQTTs initialization failed!");
	    powerdown_modem();
        return false;
    } else {
        printf("provisioning MQTTs initialized successfully!\n");
    }
   return true;
}

/**
*  @brief Deinitialize MQTTs
*/
void mqtts_deinit() {
	if (!mqtts_disconnect() || !deactivate_pdp()) {
		error(2);
	    ESP_LOGE(TAG_GSM, "MQTTs deinitialization failed!");
	    powerdown_modem();
    } else {
        printf("MQTTs deinitialized successfully!\n");
        powerdown_modem();
    }
   return;
}

/**
 * @brief Check subscriber MQTT state after publisher task
 */
void check_mqtt_link_status() {
    if (!send_at_command("AT+QMTCONN?", "3", RETRIES, CMD_DELAY_MS)) {
		error(2);
        ESP_LOGE(TAG_GSM, "MQTTs link disconnected! Reconnecting...");
        mqtts_error_reconnect();
    } else {
        //printf("MQTTs link still active.\n");
    }
  return;
}

/**
*  @brief Configure MQTT with Optimal Settings 
*/
bool configure_mqtts_settings() {
	char deviceSerial[12]; 
	getChipIdString(deviceSerial, sizeof(deviceSerial));
	char payload[128];
	snprintf(payload, sizeof(payload), "{\"serial_number\":\"%s\",\"link_status\":0}", deviceSerial);
	char will_cmd[256];
	snprintf(will_cmd, sizeof(will_cmd), "AT+QMTCFG=\"will\",0,1,1,0,\"ksc/link/status\",\"%s\"", payload);

    if (!send_at_command("ATE0", "OK", RETRIES, CMD_DELAY_MS) ||
		!send_at_command("AT+QMTCFG=\"recv/mode\",0,1,1", "OK", RETRIES, CMD_DELAY_MS) ||
        !send_at_command("AT+QMTCFG=\"keepalive\",0,30", "OK", RETRIES, CMD_DELAY_MS) ||
        !send_at_command("AT+QMTCFG=\"timeout\",0,300,3,1", "OK", RETRIES, CMD_DELAY_MS) ||
        !send_at_command("AT+QMTCFG=\"dataformat\",0,1,1", "OK", RETRIES, CMD_DELAY_MS) ||
        !send_at_command("AT+QMTCFG=\"qmtping\",0,30", "OK", RETRIES, CMD_DELAY_MS) ||
        !send_at_command("AT+QMTCFG=\"SSL\",0,1,2", "OK", RETRIES, CMD_DELAY_MS) ||
        !send_at_command("AT+QSSLCFG=\"sslversion\",0,4", "OK", RETRIES, CMD_DELAY_MS) ||
        !send_at_command("AT+QSSLCFG=\"ciphersuite\",0,\"0xFFFF\"", "OK", RETRIES, CMD_DELAY_MS) ||
        !send_at_command("AT+QSSLCFG=\"seclevel\",0,2", "OK", RETRIES, CMD_DELAY_MS) ||
        !send_at_command("AT+QSSLCFG=\"ignorelocaltime\",0,1", "OK", RETRIES, CMD_DELAY_MS) ||
        !send_at_command("AT+QMTCFG=\"session\",0,1", "OK", RETRIES, CMD_DELAY_MS) ||
        !send_at_command(will_cmd, "OK", RETRIES, CMD_DELAY_MS)) { 
		error(2);
        ESP_LOGE(TAG_GSM, "Failed to set Core configurations!\n");
        return false;
    } else {
       printf("MQTTs Core configurations set successfully!\n"); 
    }
    return true;
}

bool configure_mqtt_settings() {
    if (!send_at_command("AT+QMTCFG=\"recv/mode\",0,1,1", "OK", RETRIES, CMD_DELAY_MS)) { 
		error(2);
        ESP_LOGE(TAG_GSM, "Failed to set Core configurations!\n");
        return false;
    } else {
       printf("MQTTs Core configurations set successfully!\n"); 
    }
    return true;
}

/**
* @brief Converts hex string to ASCII
*/
bool hex_to_ascii(const char* hex, char* output) {
    if (strlen(hex) % 2 != 0) return false;
    
    size_t len = strlen(hex);
    for (size_t i = 0; i < len; i += 2) {
        char hex_byte[3] = {hex[i], hex[i+1], '\0'};
        *output++ = (char)strtol(hex_byte, NULL, 16);
    }
    *output = '\0';
    return true;
}

/**
* @brief Reads specific buffered message based on URC notification 
* @param recv_id The message slot ID from +QMTRECV URC
* Loop through slots 0 to recv_id inclusive
*/
bool read_buffered_messages(int recv_id) {
	RpcResponse responses[7];  // Buffer for up to 7 responses
	int response_count = 0;	
	
    for (int i = 0; i <= recv_id; i++) {
        char cmd[32];
        sprintf(cmd, "AT+QMTRECV=0,%d", i);
		uart_flush_input(MODEM_UART_NUM);  // Clear garbage/leftovers
		vTaskDelay(pdMS_TO_TICKS(10));

        if (send_at_command(cmd, "+QMTRECV:", 1, CMD_DELAY_MS)) { 
            // Parse format: +QMTRECV: 0,0,"topic",60,"{...}"
            char* topic_start = strchr(urcbuffer, '\"');
            if (!topic_start) continue;

            char* topic_end = strchr(topic_start + 1, '\"');
            if (!topic_end) continue;

            char* len_start = strchr(topic_end + 1, ',');
            char* payload_start = strchr(len_start ? len_start : topic_end, '\"');
            if (!payload_start) continue;

		    if (provision == 0) {
		    // Move forward from payload_start to find the 10th / 24th quote (end of JSON)
		    char* ptr = payload_start + 1;
		    int quote_count = 1; // payload_start points to first quote
		
		    char* q10_pos = NULL; // FAILURE response → 10 quotes
		    char* q24_pos = NULL; // SUCCESS response → 24 quotes
		
		    while (*ptr) {
		        if (*ptr == '\"') {
		            quote_count++;
		            if (quote_count == 10)  q10_pos = ptr;
		            if (quote_count == 24)  { q24_pos = ptr; break; }
		        }
		
		        ptr++;
		    }
		
		    char* payload_end = NULL;
		    if (q24_pos) {
		        // SUCCESS JSON
		        payload_end = q24_pos;
		    } 
		    else if (q10_pos) {
		        // FAILURE JSON
		        payload_end = q10_pos;
		    } 
		    else {
		        ESP_LOGW(TAG_GSM, "Invalid provisioning JSON. Restarting...");
                error(4);
		        vTaskDelay(pdMS_TO_TICKS(1000));
		        esp_restart();
		    }
		    // Terminate strings safely
			*topic_end = *payload_end = '\0';
		} else {
				// Move forward from payload_start to find the 6th quote (end of JSON)
				char* ptr = payload_start + 1;
				int quote_count = 1;  // payload_start is first quote
				
				while (*ptr && quote_count < 8) {
				    if (*ptr == '\"') {
				        quote_count++;
				        if (quote_count == 8) break;
				    }
				    ptr++;
				}
				if (quote_count < 8 || !*ptr) {
				    ESP_LOGW(TAG_GSM, "Failed to locate closing quote of JSON payload");
				    continue;
				}
					char* payload_end = ptr;
		            if (!payload_end) continue;	
		            // Extract 
		            *topic_end = *payload_end = '\0';			
			}


            char topic[64];
            char topic_id[64];
            char json_str[512];

            strncpy(topic, topic_start + 1, sizeof(topic) - 1);
            topic[sizeof(topic) - 1] = '\0';
            
            strncpy(json_str, payload_start + 1, sizeof(json_str) - 1);
            json_str[sizeof(json_str) - 1] = '\0';
			            
			// ➤ Extract request ID from topic
			const char* id_str = strrchr(topic, '/');
			int request_id = -1;
			
			if (id_str && strlen(id_str + 1) < sizeof(topic_id)) {
			    strncpy(topic_id, id_str + 1, sizeof(topic_id) - 1);
			    topic_id[sizeof(topic_id) - 1] = '\0';
			
			    request_id = atoi(topic_id);  // Convert to integer
			    if (request_id < 0) {
			        ESP_LOGW(TAG_GSM, "Invalid request ID in topic: %s", topic);
			        continue;
			    }
			} else {
			    ESP_LOGW(TAG_GSM, "Failed to extract request ID from topic: %s", topic);
			    continue;
			}
			            
            printf("RX [%s]: %s, %s\n", topic, json_str, topic_id); 

            // Parse the JSON
            cJSON* root = cJSON_Parse(json_str);
            if (root == NULL) {
                ESP_LOGE(TAG_GSM, "Failed to parse JSON payload: %s", json_str);
                continue;
            }


			// -------------------- PROVISIONING RESPONSE HANDLING -------------------- 
			if (provision == 0) {
			    // Extract status field
			    cJSON* status_item = cJSON_GetObjectItem(root, "status");
			    if (!status_item || !cJSON_IsString(status_item)) {
			        ESP_LOGW(TAG_GSM, "Provisioning response missing 'status' field");       
			        cJSON_Delete(root);
                    error(4);
			        vTaskDelay(pdMS_TO_TICKS(1000));
			        esp_restart();
			    }
			
			    const char* status_str = status_item->valuestring;
			    printf("Provisioning status: %s", status_str);
			
			    // Check if provisioning was successful
			    if (strcmp(status_str, "SUCCESS") == 0) { 
			
			        // Extract credentialsValue object
			        cJSON* cred_obj = cJSON_GetObjectItem(root, "credentialsValue");
			        if (!cred_obj || !cJSON_IsObject(cred_obj)) {
			            ESP_LOGW(TAG_GSM, "Provisioning response missing 'credentialsValue'");
			            cJSON_Delete(root);
                        error(4);
			            vTaskDelay(pdMS_TO_TICKS(1000));
			            esp_restart();
			        }
			
			        // Extract individual fields
			        cJSON* clientId_item = cJSON_GetObjectItem(cred_obj, "clientId");
			        cJSON* user_item     = cJSON_GetObjectItem(cred_obj, "userName");
			        cJSON* pass_item     = cJSON_GetObjectItem(cred_obj, "password");
				
					if (!clientId_item || !user_item || !pass_item ||
					    !cJSON_IsString(clientId_item) ||
					    !cJSON_IsString(user_item) ||
					    !cJSON_IsString(pass_item) ||
					    strcmp(clientId_item->valuestring, MQTT_CLIENT_ID) != 0) // Confirm the provisioning success was for this particular hub
					{					
						
			            ESP_LOGW(TAG_GSM, "Invalid credentialsValue structure");
			            cJSON_Delete(root);
                        error(4);
			            vTaskDelay(pdMS_TO_TICKS(1000));
			            esp_restart();
			        }
			
			        // Log extracted credentials
			        printf("Provisioned ClientID: %s", clientId_item->valuestring);
			        printf("Provisioned Username: %s", user_item->valuestring);
			        printf("Provisioned Password: %s", pass_item->valuestring);
			
			        // Update provision flag
			        provision = 1;
			        save_provision_to_nvs(1);
			        playMqqtConfirmationTone(LEDC_CHANNEL_BUZZER, LEDC_TIMER); 
			        vTaskDelay(pdMS_TO_TICKS(1000));
			
			        printf("Provisioning successful. Restarting device...");
			        cJSON_Delete(root);
			        vTaskDelay(pdMS_TO_TICKS(1000));
			        esp_restart();
			    } 
			    // ---------------- FAILURE CASE ----------------
			    else if (strcmp(status_str, "FAILURE") == 0) {
			 
			        // Extract error message
			        cJSON* errorMsg_item = cJSON_GetObjectItem(root, "errorMsg");
			        if (!errorMsg_item || !cJSON_IsString(errorMsg_item)) {
			            ESP_LOGE(TAG_GSM, "Provisioning FAILURE but missing errorMsg");
			            cJSON_Delete(root);
                        error(4);
			            vTaskDelay(pdMS_TO_TICKS(1000));
			            esp_restart();
			        }
			
			        const char* error_msg = errorMsg_item->valuestring;
			        ESP_LOGE(TAG_GSM, "Provisioning error: %s", error_msg);
			
			
			        // Case 1: Device already provisioned
			        if (strstr(error_msg, "Failed to provision device!")) {
			
			            provision = 1;
			            save_provision_to_nvs(1);
			
			            printf("Device already provisioned / Thingsboard failed. Check Platform!\n");
			            printf("Restarting Device...");
			            cJSON_Delete(root);
                        error(4);
			            vTaskDelay(pdMS_TO_TICKS(1000));
			            esp_restart();
			        }
			
			        // Case 2: Real provisioning failure
			        ESP_LOGE(TAG_GSM, "Provisioning failed. Retrying from scratch...");
			        cJSON_Delete(root);
                    error(4);
			        vTaskDelay(pdMS_TO_TICKS(1000));
			        esp_restart();
			
			    } 
			    // ---------------- UNKNOWN STATUS ----------------
			    else {
			        ESP_LOGE(TAG_GSM, "Provisioning FAILED. Status: %s", status_str);
			        cJSON_Delete(root);
                    error(4);
			        vTaskDelay(pdMS_TO_TICKS(1000));
			        esp_restart();
			    }
			}
			// -------------------- END PROVISIONING HANDLING --------------------


            // Extract method and params
            cJSON* method = cJSON_GetObjectItem(root, "method");
            cJSON* params = cJSON_GetObjectItem(root, "params");

            if (!method || !params || !cJSON_IsString(method)) {
                ESP_LOGE(TAG_GSM, "Missing or invalid method/params in JSON");
                cJSON_Delete(root);
                continue;
            }
            // Validate method format 
            const char* method_str = method->valuestring;
            if (strlen(method_str) < 7 || method_str[0] != 'H') {
                ESP_LOGE(TAG_GSM, "Invalid method format: %s", method_str);
                cJSON_Delete(root);
                continue;
            }
            //Extract Hub ID to distinguish commands for other hubs
             uint32_t hub_id = atoi(&method_str[1]); // e.g., H01S01 → 1
            if (hub_id != hub_number) {
                ESP_LOGE(TAG_GSM, "Rejected! Command for different hub: HUB%ld", hub_id);
                cJSON_Delete(root);
                continue;
            }             
            // Extract slave ID and command
            int slave_id = atoi(&method_str[4]); // e.g., H01S01SPCC → 1
            const char* cmd_type = &method_str[6]; // e.g., "SPCV", "CS", etc.

            ControlCommand cmd = {0};
            cmd.slave_id = slave_id;
            bool valid = false;

            // Handle different command types
            if (strcmp(cmd_type, "SPCV") == 0 && cJSON_IsNumber(params)) {
                cmd.Charger_InputVoltage = (float)params->valuedouble;
                valid = true;
                printf("Slave %d SPCV: %.2f", slave_id, cmd.Charger_InputVoltage);
            } 
            else if (strcmp(cmd_type, "SPCC") == 0 && cJSON_IsNumber(params)) {
                cmd.Charger_OutputCurrent = (float)params->valuedouble;
                valid = true;
                printf("Slave %d SPCC: %.2f", slave_id, cmd.Charger_OutputCurrent);
            } 
            else if (strcmp(cmd_type, "CS") == 0 && cJSON_IsNumber(params)) {
                cmd.Charger_Enable = params->valueint;
                valid = true;
                printf("Slave %d CS: %d", slave_id, cmd.Charger_Enable);
            } 
            else if (strcmp(cmd_type, "CMFET") == 0 && cJSON_IsNumber(params)) {
                cmd.MOSFET_Charge = params->valueint;
                valid = true;
                printf("Slave %d CMFET: %d", slave_id, cmd.MOSFET_Charge);
            } 
            else if (strcmp(cmd_type, "DMFET") == 0 && cJSON_IsNumber(params)) {
                cmd.MOSFET_Discharge = params->valueint;
                valid = true;
                printf("Slave %d DMFET: %d", slave_id, cmd.MOSFET_Discharge);
            } 
			else if (cJSON_IsNull(params)) {
			    // Handle null RPC calls requests 
			    printf("State query received for Slave %d [%s]\n", slave_id, cmd_type);
                handle_rpc_state_request(slave_id, request_id, cmd_type, responses, &response_count);
			    valid = false;
			} 
			else {
			    ESP_LOGE(TAG_GSM, "Unknown command type or invalid params: %s", cmd_type);
			}

            if (valid) {
		        playMqqtConfirmationTone(LEDC_CHANNEL_BUZZER, LEDC_TIMER); 				
                BaseType_t status = xQueueSend(commandQueue, &cmd, pdMS_TO_TICKS(10));               
                if (status != pdPASS) {
                    ESP_LOGE(TAG_GSM, "Command queue full, command to slave %d dropped", slave_id);
                }
            }

            cJSON_Delete(root);
			memset(topic, 0, sizeof(topic));
			memset(topic_id, 0, sizeof(topic_id));
			memset(json_str, 0, sizeof(json_str));
            
        } else {
            //printf("No message in slot %d\n", i);
            continue;
        }
        
    }
    // After processing all messages, send any buffered responses
    if (response_count > 0) {
        publish_rpc_responses(responses, response_count); 
    }
    return true;
}

/**
* @brief URC handler
*/
bool check_mqtt_urc() {
    static char urc_buffer[192];
    static int urc_index = 0;
    uint8_t data;
    
    while (uart_read_bytes(MODEM_UART_NUM, &data, 1, 20 / portTICK_PERIOD_MS) > 0) {
        if (urc_index < sizeof(urc_buffer)-1) {
            urc_buffer[urc_index++] = data;
            
            if (data == '\n') {
                urc_buffer[urc_index] = '\0';
                ESP_LOGI(TAG_GSM, "RAW URC: %s", urc_buffer);
                           
                // Handle +QMTRECV
                if (strstr(urc_buffer, "+QMTRECV:")) {
                    int client_idx, recv_id;
                    if (sscanf(urc_buffer, "+QMTRECV: %d,%d", &client_idx, &recv_id) == 2) {
                        //if (!read_buffered_messages(recv_id)) {
						if (!read_buffered_messages(4)) {
							error(3);
                            ESP_LOGE(TAG_GSM, "Failed to read message from slot %d", recv_id);
                        }						
						
                    }
                    urc_index = 0;
                    return true;
                }
                                            
                // Handle +QMTSTAT
                else if (strstr(urc_buffer, "+QMTSTAT:")) {
                    int client_idx, err_code;
                    if (sscanf(urc_buffer, "+QMTSTAT: %d,%d", &client_idx, &err_code) == 2) {
						error(3);
                        ESP_LOGE(TAG_GSM, "MQTT Error: %d", err_code);
                        handle_mqtt_error(err_code);
                    }
                }
                
                urc_index = 0;
                return true;
            }
        } else {
            urc_index = 0; // Prevent overflow
        }
    }
    return false;
}

/**
* @brief Handles MQTT connection errors
* @param err_code From +QMTSTAT URC
*/
void handle_mqtt_error(int err_code) {
    switch(err_code) {
        case 1: // Connection closed by peer
            ESP_LOGI(TAG_GSM, "Reconnecting MQTTs...");
            mqtts_error_reconnect();
          break;
            
        case 2: // Ping timeout
            ESP_LOGI(TAG_GSM, "Ping timeout, resetting PDP context...");
		    gsm_reset();		   
			if (!activate_pdp()) {
				error(2);
			    ESP_LOGE(TAG_GSM, "Failed to activate pdp after gsm reset, initiating mcu restart...!");
			    powerdown_modem();
			    esp_restart();
		    } else {	
			    printf("Pdp activated successfully after gsm reset!");
		    }
			if (!open_mqtts()) {
				error(2);
			    ESP_LOGE(TAG_GSM, "Failed to open mqqts after gsm reset, initiating mcu restart...!");
			    powerdown_modem();
			    esp_restart();
		    } else {	
			    printf("Mqtts reopened successfully after gsm reset!\n");
		    }
			if (!mqtt_resubscribe()) { 
				error(2);
			    ESP_LOGE(TAG_GSM, "mqtts re-subscription failed after gsm reset, initiating mcu restart...!");
			    powerdown_modem();
			    esp_restart();
		    } else {	
			    printf("mqtts re-subscribed successfully after gsm reset!\n");
		    }
          break;
            
        case 3: // CONNECT timeout
            ESP_LOGI(TAG_GSM, "Reconnecting MQTTs...");
			mqtts_error_reconnect();
          break;
        case 4: // CONNACK timeout
            ESP_LOGI(TAG_GSM, "Retrying MQTTs connection...");
            mqtts_error_reconnect();
          break;
        case 6: // CONNECT timeout
            ESP_LOGI(TAG_GSM, "Reconnecting MQTTs...");
            mqtts_error_reconnect();
          break;            
        case 7: // Link dead
            ESP_LOGI(TAG_GSM, "power off modem and on again...");
		    powerdown_modem();
		    esp_restart();
          break; 
        default:
        	error(2);
            ESP_LOGE(TAG_GSM, "Unhandled error: %d - mcu restarting...", err_code);
		    powerdown_modem();
		    esp_restart();
    }
}


/**
* @brief +QMSTAT URC mqtts reconnection 
*/
void mqtts_error_reconnect() {
	if (!open_mqtts()) {
		error(2);
	    ESP_LOGE(TAG_GSM, "mqtts reopen failed, restarting mqtts...");
	    gsm_reset();		   
		if (!activate_pdp()) {
			error(2);
		    ESP_LOGE(TAG_GSM, "Failed to activate pdp after gsm reset, initiating mcu restart...!");
		    powerdown_modem();
		    esp_restart();
	    } else {	
		    printf("Pdp activated successfully after gsm reset!");
	    }
		if (!open_mqtts()) {
			error(2);
		    ESP_LOGE(TAG_GSM, "Failed to open mqqts after gsm reset, initiating mcu restart...!");
		    powerdown_modem();
		    esp_restart();
	    } else {	
		    printf("Mqtts reopened successfully after gsm reset!\n");
	    }
		if (!mqtt_resubscribe()) {
			error(2);
		    ESP_LOGE(TAG_GSM, "mqtts re-subscription failed after gsm reset, initiating mcu restart...!");
		    powerdown_modem();
		    esp_restart();
	    } else {	
		    printf("mqtts re-subscribed successfully after gsm reset!\n");
	    }
	    return;
    } else {	
	    printf("mqtts reopened successfully!\n");
    }

	if (!mqtt_resubscribe()) {
		error(2);
	    ESP_LOGE(TAG_GSM, "mqtts re-subscription failed, restarting mqtts...");
	    gsm_reset();		   
		if (!activate_pdp()) {
			error(2);
		    ESP_LOGE(TAG_GSM, "Failed to activate pdp after gsm reset, initiating mcu restart...!");
		    powerdown_modem();
		    esp_restart();
	    } else {	
		    printf("Pdp activated successfully after gsm reset!");
	    }
		if (!open_mqtts()) {
			error(2);
		    ESP_LOGE(TAG_GSM, "Failed to open mqqts after gsm reset, initiating mcu restart...!");
		    powerdown_modem();
		    esp_restart();
	    } else {	
		    printf("Mqtts reopened successfully after gsm reset!\n");
	    }
		if (!mqtt_resubscribe()) {
			error(2);
		    ESP_LOGE(TAG_GSM, "mqtts re-subscription failed after gsm reset, initiating mcu restart...!");
		    powerdown_modem();
		    esp_restart();
	    } else {	
		    printf("mqtts re-subscribed successfully after gsm reset!\n");
	    }
    } else {	
	    printf("mqtts re-subscribed successfully!\n");
    }	
}

/**
 *  @brief RPC Requests handler
 *  @param responses Array to store responses (must have space for 7 elements)
 *  @param response_count Pointer to track number of responses stored
 */
void handle_rpc_state_request(int slave_id, int request_id, const char* param_name, RpcResponse* responses, int* response_count) {
    if (slave_id < 1 || slave_id > MAX_SLAVES) {
        ESP_LOGE(TAG_GSM, "Invalid RPC state request: slave_id=%d, request ID=%d, param=%s", slave_id, request_id, param_name);
        return;
    }

    int index = slave_id - 1;

    // Handle SPCV and SPCC separately: respond with pure numeric value
    if (strcmp(param_name, "SPCV") == 0) {
        double value = roundf(ringBuffer[index].charger.setpoint.charge_mv) / 1000;
        char response_str[16];
        snprintf(response_str, sizeof(response_str), "%.2f", value);
        // Store in buffer
        ESP_LOGI(TAG_GSM, "Buffered: S%02dSPCV (ID %d): %s", slave_id, request_id, response_str);
        responses[*response_count].request_id = request_id;
        responses[*response_count].response_payload = strdup(response_str);
        (*response_count)++;
        return;
    }

    if (strcmp(param_name, "SPCC") == 0) {
        double value = roundf(ringBuffer[index].charger.setpoint.charge_ma) / 1000;
        char response_str[16];
        snprintf(response_str, sizeof(response_str), "%.2f", value);
        // Store in buffer
        ESP_LOGI(TAG_GSM, "Buffered: S%02dSPCC (ID %d): %s", slave_id, request_id, response_str);
        responses[*response_count].request_id = request_id;
        responses[*response_count].response_payload = strdup(response_str);
        (*response_count)++;
        return;
    }

    // All others respond with {"method":"...", "params":...}
    cJSON *response = cJSON_CreateObject();
    bool valid = false;

    // Construct method string: e.g., "S01CS"
    char method_str[16];
    snprintf(method_str, sizeof(method_str), "S%02d%s", slave_id, param_name);
    cJSON_AddStringToObject(response, "method", method_str);

    if (strcmp(param_name, "CS") == 0) {
        int val = (ringBuffer[index].charger.setpoint.en);
        cJSON_AddNumberToObject(response, "params", val == 1 ? 2 : 1);
        valid = true;
    } else if (strcmp(param_name, "CMFET") == 0) {
        int val = ringBuffer[index].jk_data.charge_stat;
        cJSON_AddNumberToObject(response, "params", val == 1 ? 2 : 1);
        valid = true;
    } else if (strcmp(param_name, "DMFET") == 0) {
        int val = ringBuffer[index].jk_data.discharge_stat;
        cJSON_AddNumberToObject(response, "params", val == 1 ? 2 : 1);
        valid = true;
    } else {
        ESP_LOGW(TAG_GSM, "Unsupported param in RPC state request: %s", param_name);
    }

    if (valid) {
        char *response_str = cJSON_PrintUnformatted(response);
        // Store in buffer
        ESP_LOGI(TAG_GSM, "Buffered: [%s] (ID %d): %s", method_str, request_id, response_str);
        responses[*response_count].request_id = request_id;
        responses[*response_count].response_payload = strdup(response_str);
        (*response_count)++;
        free(response_str);
    }

    cJSON_Delete(response);
}

/**
 *  @brief RPC Requests batch reply
 *  @param responses Array of responses to publish
 *  @param count Number of responses in the array
 */
bool publish_rpc_responses(RpcResponse* responses, int count) {
    if (responses == NULL || count <= 0) {
        ESP_LOGW(TAG_GSM, "Invalid arguments to publish_rpc_responses");
        return false;
    }

    bool all_success = true;
    
    for (int i = 0; i < count; i++) {
        bool respub = false;
        char pub_cmd[128];
        char response_topic[64];
        
        snprintf(response_topic, sizeof(response_topic), "v1/devices/me/rpc/response/%d", responses[i].request_id);
        snprintf(pub_cmd, sizeof(pub_cmd), "AT+QMTPUBEX=0,0,0,0,\"%s\",%d", 
                response_topic, strlen(responses[i].response_payload));
                
        for (int attempt = 0; attempt < 3; attempt++) {
            if (!send_at_command(pub_cmd, ">", RETRIES, CMD_DELAY_MS)) {
                ESP_LOGE(TAG_GSM, "MQTT RPC response publish failed on attempt %d", attempt + 1);
                error(2);
                vTaskDelay(pdMS_TO_TICKS(10 * (attempt + 1)));
                continue;                
            } else {
                uart_write_bytes(MODEM_UART_NUM, responses[i].response_payload, strlen(responses[i].response_payload));
                vTaskDelay(pdMS_TO_TICKS(10));
                uart_write_bytes(MODEM_UART_NUM, "\x1A", 1);  // End of message
                vTaskDelay(pdMS_TO_TICKS(3));
                printf("RPC response published successfully to %s\n", response_topic);
                respub = true;
                break;
            }
        }

        if (!respub) {
            ESP_LOGE(TAG_GSM, "MQTT RPC response publish failed after 3 attempts.");
            error(2);
            all_success = false;
        }
        
        // Free the allocated memory
        free(responses[i].response_payload);
    }
    
    return all_success;
}

/**
 * @brief Provisioning credentials json creation
 */
char* create_provision_json_payload() { 
	printf("creating the JSON data...\n");
    cJSON *root = cJSON_CreateObject();

    cJSON_AddStringToObject(root, "deviceName", DEVICE_NAME);
    cJSON_AddStringToObject(root, "provisionDeviceKey", PROVISION_KEY);
    cJSON_AddStringToObject(root, "provisionDeviceSecret", PROVISION_SECRET);
    cJSON_AddStringToObject(root, "credentialsType", CREDENTIALS_TYPE);
    cJSON_AddStringToObject(root, "username", MQTT_USERNAME);
    cJSON_AddStringToObject(root, "password", MQTT_PASSWORD);
    cJSON_AddStringToObject(root, "clientId", MQTT_CLIENT_ID);

    // char *jsonString = cJSON_Print(root);
    char *jsonString = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);  
    return jsonString;
}