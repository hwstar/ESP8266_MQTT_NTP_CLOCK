/*
* Configuration parameters set with the Makefile using a Python patching
* utility which is avalable on my github site. This allows the configurations
* to differ between nodes and also protects the WIFI login credentials by
* removing them from the source.
*
* Copyright (C) 2015, Stephen Rodgers <steve at rodgers 619 dot com>
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* 
* Redistributions of source code must retain the above copyright notice,
* this list of conditions and the following disclaimer.
* Redistributions in binary form must reproduce the above copyright
* notice, this list of conditions and the following disclaimer in the
* documentation and/or other materials provided with the distribution.
* Neither the name of Redis nor the names of its contributors may be used
* to endorse or promote products derived from this software without
* specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
* ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
* LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
* INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
* CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
* ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
* POSSIBILITY OF SUCH DAMAGE.
 */

// API Includes
#include "ets_sys.h"
#include "osapi.h"
#include "debug.h"
#include "gpio.h"
#include "user_interface.h"
#include "mem.h"
#include "jsonparse.h"
// Project includes
#include "driver/uart.h"
#include "mqtt.h"
#include "wifi.h"
#include "sntp.h"
#include "time_utils.h"
#include "util.h"
#include "kvstore.h"

#define MAX_TIME_SERVERS 4

#define MAX_INFO_ELEMENTS 16
#define INFO_BLOCK_MAGIC 0x3F2A6C17
#define INFO_BLOCK_SIG "ESP8266HWSTARSR"
#define CONFIG_FLD_REQD 0x01

#define DSPL_COLON 0x10
#define DSPL_DP4 0x08

// Definition for a patcher config element

struct config_info_element_tag{
	uint8_t flags;
	const char key[15];
	char value[80];
}  __attribute__((__packed__));

typedef struct config_info_element_tag config_info_element;

struct config_info_block_tag{
	uint8_t signature[16];
	uint32_t magic;
	uint8_t numelements;
	uint8_t recordLength;
	uint8_t pad[10];
	config_info_element e[MAX_INFO_ELEMENTS];
}  __attribute__((__packed__));


// Definition of a common element for MQTT command parameters

typedef struct config_info_block_tag config_info_block;

typedef union {
	char* sp;
	unsigned u;
	int i;
} pu;


// Definition of an MQTT command element

typedef struct {
	const char *command;
	uint8_t type;
	pu p;
} command_element;


enum {WIFISSID=0, WIFIPASS, MQTTHOST, MQTTPORT, MQTTSECUR, MQTTDEVID, MQTTUSER, MQTTPASS, MQTTKPALIV, MQTTDEVPATH, SNTPHOSTS, UTCOFFSET, SNTPPOLL, TIME24};


/* Configuration block */

LOCAL config_info_block configInfoBlock = {
	.signature = INFO_BLOCK_SIG,
	.magic = INFO_BLOCK_MAGIC,
	.numelements = MAX_INFO_ELEMENTS,
	.recordLength = sizeof(config_info_element),
	.e[WIFISSID] = {.flags = CONFIG_FLD_REQD, .key = "WIFISSID", .value="your_ssid_here"},
	.e[WIFIPASS] = {.flags = CONFIG_FLD_REQD, .key = "WIFIPASS", .value="its_a_secret"},
	.e[MQTTHOST] = {.flags = CONFIG_FLD_REQD, .key = "MQTTHOST", .value="your_mqtt_broker_hostname_here"}, // May also be an IP address
	.e[MQTTPORT] = {.key = "MQTTPORT", .value="1883"}, // destination Port for mqtt broker
	.e[MQTTSECUR] = {.key = "MQTTSECUR",.value="0"}, // Security 0 - no encryption
	.e[MQTTDEVID] = {.key = "MQTTDEVID", .value="dev_id"}, // Unique device ID
	.e[MQTTUSER] = {.key = "MQTTUSER", .value="your_mqtt_user_name_here"},  // MQTT user if ACL's are used
	.e[MQTTPASS] = {.key = "MQTTPASS", .value="its_a_secret"}, // MQTT password if ACL's are used
	.e[MQTTKPALIV] = {.key = "MQTTKPALIV", .value="120"}, // Keepalive interval
	.e[MQTTDEVPATH] = {.flags = CONFIG_FLD_REQD, .key = "MQTTDEVPATH", .value = "/home/lab/clock"}, // Device path
	.e[SNTPHOSTS] = {.key = "SNTPHOSTS", .value = "pool.ntp.org"}, // Comma separated list of SNTP hosts
	.e[UTCOFFSET] = {.key = "UTCOFFSET", .value = "-28800"}, // UTC offset in seconds
	.e[SNTPPOLL] = {.key = "SNTPPOLL", .value = "3600000"}, // 1 Hour polling interval
	.e[TIME24] = {.key = "TIME24", .value = "0"} // 1 = 24 hour display
	
	
};
// Definition of command codes and types
enum {CP_NONE= 0, CP_INT, CP_BOOL, CP_QSTRING, CP_QINT, CP_QBOOL};
enum {CMD_TIME24 = 0, CMD_UTCOFFSET, CMD_SURVEY, CMD_SSID, CMD_WIFIPASS, CMD_RESTART};

LOCAL command_element commandElements[] = {
	{.command = "time24", .type = CP_QBOOL},
	{.command = "utcoffset", .type = CP_QINT},
	{.command = "survey", .type = CP_NONE},
	{.command = "ssid", .type = CP_QSTRING},
	{.command = "wifipass", .type = CP_QSTRING},
	{.command = "restart", .type = CP_NONE},
	{.command = ""}
};
	 
/* Local storage */

	 
LOCAL os_timer_t display_timer;

LOCAL MQTT_Client mqttClient;

LOCAL char *sntpServerList[MAX_TIME_SERVERS + 1];


LOCAL pollingInterval;
LOCAL wifiStatus;
LOCAL char *commandTopic;
LOCAL char *statusTopic;
LOCAL char *controlTopic = "/node/control";
LOCAL char *infoTopic = "/node/info";
LOCAL flash_handle_s *configHandle;


/**
 * Publish connection info
 */
 
LOCAL void ICACHE_FLASH_ATTR publishConnInfo(MQTT_Client *client)
{
	struct ip_info ipConfig;
	char *buf = util_zalloc(256);	
		
	// Publish who we are and where we live
	wifi_get_ip_info(STATION_IF, &ipConfig);
	os_sprintf(buf, "{\"muster\":{\"connstate\":\"online\",\"device\":\"%s\",\"ip4\":\"%d.%d.%d.%d\",\"schema\":\"hwstar_ntpclock\",\"ssid\":\"%s\"}}",
			configInfoBlock.e[MQTTDEVPATH].value,
			*((uint8_t *) &ipConfig.ip.addr),
			*((uint8_t *) &ipConfig.ip.addr + 1),
			*((uint8_t *) &ipConfig.ip.addr + 2),
			*((uint8_t *) &ipConfig.ip.addr + 3),
			commandElements[CMD_SSID].p.sp);

	INFO("MQTT Node info: %s\r\n", buf);

	// Publish
	MQTT_Publish(client, infoTopic, buf, os_strlen(buf), 0, 0);
	
	// Free the buffer
	util_free(buf);
	
}


/**
 * Handle qstring command
 */
 
LOCAL void ICACHE_FLASH_ATTR handleQstringCommand(char *new_value, command_element *ce)
{
	char *buf = util_zalloc(128);
	
	
	if(!new_value){
		const char *cur_value = kvstore_get_string(configHandle, ce->command);
		os_sprintf(buf, "{\"%s\":\"%s\"}", ce->command, cur_value);
		util_free(cur_value);
		INFO("Query Result: %s\r\n", buf );
		MQTT_Publish(&mqttClient, statusTopic, buf, os_strlen(buf), 0, 0);
	}
	else{
		util_free(ce->p.sp); // Free old value
		ce->p.sp = new_value; // Save reference to new value
		kvstore_put(configHandle, ce->command, ce->p.sp);
		
	}

	util_free(buf);

}

/**
 * Wifi connect callback
 */


void ICACHE_FLASH_ATTR wifiConnectCb(uint8_t status)
{
	static bool sntpInitialized = FALSE;
	
	INFO("WifiConnectCb called: Status = %d\r\n", status);
	wifiStatus = status;
	if(status == STATION_GOT_IP){
			MQTT_Connect(&mqttClient);
			if(!sntpInitialized){
				sntp_init(sntpServerList, pollingInterval); // Init SNTP one time only
				sntpInitialized = TRUE;
			}
	}
}

/**
 * Survey complete,
 * publish results
 */


LOCAL void ICACHE_FLASH_ATTR
survey_complete_cb(void *arg, STATUS status)
{
	struct bss_info *bss = arg;
	
	#define SURVEY_CHUNK_SIZE 256
	
	if(status == OK){
		uint8_t i;
		char *buf = util_zalloc(SURVEY_CHUNK_SIZE);
		bss = bss->next.stqe_next; //ignore first
		for(i = 2; (bss); i++){
			if(2 == i)
				os_sprintf(strlen(buf) + buf,"{\"access_points\":[");
			else
				os_strcat(buf,",");
			os_sprintf(strlen(buf)+ buf, "\"%s\":{\"chan\":\"%d\",\"rssi\":\"%d\"}", bss->ssid, bss->channel, bss->rssi);
			bss = bss->next.stqe_next;
			buf = util_str_realloc(buf, i * SURVEY_CHUNK_SIZE); // Grow buffer
		}
		if(buf[0])
			os_strcat(buf,"]}");
		
		INFO("Survey Results:\r\n", buf);
		INFO(buf);
		MQTT_Publish(&mqttClient, statusTopic, buf, os_strlen(buf), 0, 0);
		util_free(buf);
	}

}


/**
 * MQTT Connect call back
 */
 
void ICACHE_FLASH_ATTR mqttConnectedCb(uint32_t *args)
{

	MQTT_Client* client = (MQTT_Client*)args;
	
	INFO("MQTT: Connected\r\n");
	
	publishConnInfo(client);
	
	// Subscribe to the control topic
	MQTT_Subscribe(client, controlTopic, 0);

	// Subscribe to the command topic
    MQTT_Subscribe(client, commandTopic, 0);


}

/**
 * MQTT Disconnect call back
 */
 

void ICACHE_FLASH_ATTR mqttDisconnectedCb(uint32_t *args)
{
	MQTT_Client* client = (MQTT_Client*)args;
	INFO("MQTT: Disconnected\r\n");
}

/**
 * MQTT published call back
 */

void ICACHE_FLASH_ATTR mqttPublishedCb(uint32_t *args)
{
	MQTT_Client* client = (MQTT_Client*)args;
	INFO("MQTT: Published\r\n");
}

/**
 * MQTT Data call back
 */

void ICACHE_FLASH_ATTR mqttDataCb(uint32_t *args, const char* topic, uint32_t topic_len, const char *data, uint32_t data_len)
{
	char *topicBuf,*dataBuf;
	struct jsonparse_state state;
	char command[32];
	int i;
	
	MQTT_Client* client = (MQTT_Client*)args;
	
	
	topicBuf = util_strndup(topic, topic_len);
	dataBuf = util_strndup(data, data_len);
	char *buf = util_zalloc(256); // Working buffer

	INFO("Receive topic: %s, data: %s \r\n", topicBuf, dataBuf);
	
	// Control Message?
	if(!os_strcmp(topicBuf, controlTopic)){
		jsonparse_setup(&state, dataBuf, data_len);
		if (util_parse_json_param(&state, "control", command, sizeof(command)) != 2)
			return; /* Command not present in json object */
		if(!os_strcmp(command, "muster")){
			publishConnInfo(&mqttClient);
		}
	}
	
	// Command Message?
	else if (!os_strcmp(topicBuf, commandTopic)){ // Check for match to command topic
		// Parse command
		jsonparse_setup(&state, dataBuf, data_len);
		if (util_parse_json_param(&state, "command", command, sizeof(command)) != 2)
			return; /* Command not present in json object */
				
		for(i = 0; commandElements[i].command[0]; i++){
			command_element *ce = &commandElements[i];
			uint8_t cmd_len = os_strlen(ce->command);
			//INFO("Trying %s\r\n", ce->command);
			if(CP_NONE == ce->type){ // Parameterless commands
				if(!os_strcmp(dataBuf, ce->command)){
					if(CMD_SURVEY == i){ // SURVEY? 
						wifi_station_scan(NULL, survey_complete_cb);
						break;
					}
					if(CMD_RESTART == i){ // RESTART?
						util_restart();
					}
				}	
			}
		
			if((CP_QINT == ce->type) || (CP_QBOOL == ce->type)){ // Integer and bool
				int res = util_parse_param_qint(command, ce->command, dataBuf, &ce->p.i);

				if(2 == res){
					if(CP_QBOOL == ce->type)
						ce->p.i = (ce->p.i) ? 1: 0;
					//INFO("%s = %d\r\n", ce->command, ce->p.i);
					if(!kvstore_update_number(configHandle, ce->command, ce->p.i))
						INFO("Error storing integer parameter");
					break;
				}
				else if (0 == res){ // Return current setting
					os_sprintf(buf, "{\"%s\":\"%d\"}", ce->command, ce->p.i);
					MQTT_Publish(&mqttClient, statusTopic, buf, os_strlen(buf), 0, 0);
				}

			}
			if(CP_QSTRING == ce->type){ // Query strings
				char *val;
				if(util_parse_command_qstring(command, ce->command, dataBuf, &val)){
					if((CMD_SSID == i) || (CMD_WIFIPASS == i)){ // SSID or WIFIPASS?
						handleQstringCommand(val, ce);
					}
				}
			}
			
		}
		kvstore_flush(configHandle); // Flush any changes back to the kvs		
	}
	util_free(buf);
	util_free(topicBuf);
	util_free(dataBuf);
}

/**
 * Callback to update LED display
 */

void ICACHE_FLASH_ATTR displayTimerExpireCb(void *notUsed)
{
	static uint8_t clkStr[9] = {0x77,0x00,0x79,0x00};
	uint64_t now = (uint64_t) sntp_get_time();
	
	
	if(now){ // If time non-zero, display it
		clkStr[1] ^= DSPL_COLON; // Colon flash
		// Set DP4 if SNTP connection is successful to a listed host
		if(STATION_GOT_IP == wifiStatus){
			// Flash DP4 if time server connection issues
			// DP4 solid if everything ok
			if(sntp_conn_established())
				clkStr[1] |= DSPL_DP4;
			else
				clkStr[1] ^= DSPL_DP4;
		}
		else{
			// DP4 off if no IP address
			clkStr[1] &= ~DSPL_DP4;
		}
		
		now += commandElements[CMD_UTCOFFSET].p.i; // Adjust time for locale
		epoch_to_clock_str(now, clkStr + 4, commandElements[CMD_TIME24].p.i); // Make string
		if (0 == now % 60){
			INFO("Time = %s\r\n", clkStr + 4);
		}
	}

	else{ // If time invalid
			clkStr[1] &= ~DSPL_COLON; // Colon off
			clkStr[1] &= ~DSPL_DP4; // DP4 off
			clkStr[4] = clkStr[5] = clkStr[6] = clkStr[7] = 0x2D; // All dashes
	}

	uart1_tx_buffer(clkStr, 8);
}

/**
 * Initialization
 */

void ICACHE_FLASH_ATTR clock_init(void)
{
	char *buf = util_zalloc(256); // Working buffer
	
	// I/O initialization
	gpio_init();

	// Uart init
	uart1_init(BIT_RATE_9600);
	uart0_init(BIT_RATE_115200);
	
	// Initialize display to minimize garbage display time at power on
	displayTimerExpireCb(NULL);

	os_delay_us(2000000); // Wait for gtkterm to come up

	// Read in the config sector from flash
	configHandle = kvstore_open(KVS_DEFAULT_LOC);
	
	const char *ssidKey = commandElements[CMD_SSID].command;
	const char *WIFIPassKey = commandElements[CMD_WIFIPASS].command;
	
	// Check for default configuration overrides
		// Check for default configuration overrides
	if(!kvstore_exists(configHandle, ssidKey)){ // if no ssid, assume the rest of the defaults need to be set as well
		kvstore_put(configHandle, ssidKey, configInfoBlock.e[WIFISSID].value);
		kvstore_put(configHandle, WIFIPassKey, configInfoBlock.e[WIFIPASS].value);
		kvstore_put(configHandle, commandElements[CMD_UTCOFFSET].command, configInfoBlock.e[UTCOFFSET].value);
		kvstore_put(configHandle, commandElements[CMD_TIME24].command, configInfoBlock.e[TIME24].value);

		// Write the KVS back out to flash	
	
		kvstore_flush(configHandle);
	}
	
	
	// Get the configurations we need from the KVS
	
	kvstore_get_integer(configHandle,  commandElements[CMD_UTCOFFSET].command, &commandElements[CMD_UTCOFFSET].p.i); // Retrieve UTC offset
	kvstore_get_integer(configHandle, commandElements[CMD_TIME24].command, &commandElements[CMD_TIME24].p.i); // Retrieve 12/24 hour time flag
	commandElements[CMD_SSID].p.sp = kvstore_get_string(configHandle, ssidKey); // Retrieve SSID
	commandElements[CMD_WIFIPASS].p.sp = kvstore_get_string(configHandle, WIFIPassKey); // Retrieve WIFI Pass
	
	
	// Set Non KVS configurations 
	pollingInterval = atoi(configInfoBlock.e[SNTPPOLL].value);


	// Initialize MQTT connection 
	
	uint8_t *host = configInfoBlock.e[MQTTHOST].value;
	uint32_t port = (uint32_t) atoi(configInfoBlock.e[MQTTPORT].value);
	

	//MQTT setup 
	
	MQTT_InitConnection(&mqttClient, host, port,
	(uint8_t) atoi(configInfoBlock.e[MQTTSECUR].value));

	MQTT_InitClient(&mqttClient, configInfoBlock.e[MQTTDEVID].value, 
	configInfoBlock.e[MQTTUSER].value, configInfoBlock.e[MQTTPASS].value,
	atoi(configInfoBlock.e[MQTTKPALIV].value), 1);

	// Last will and testament

	os_sprintf(buf, "{\"muster\":{\"connstate\":\"offline\",\"device\":\"%s\"}}", configInfoBlock.e[MQTTDEVPATH].value);
	MQTT_InitLWT(&mqttClient, "/node/info", buf, 0, 0);


	// MQTT callback setup

	MQTT_OnConnected(&mqttClient, mqttConnectedCb);
	MQTT_OnDisconnected(&mqttClient, mqttDisconnectedCb);
	MQTT_OnPublished(&mqttClient, mqttPublishedCb);
	MQTT_OnData(&mqttClient, mqttDataCb);
	
	// Subtopics 
	
	commandTopic = util_make_sub_topic(configInfoBlock.e[MQTTDEVPATH].value, "command");
	statusTopic = util_make_sub_topic(configInfoBlock.e[MQTTDEVPATH].value, "status");

	// Parse list of time servers
	
	char *h = util_string_split(configInfoBlock.e[SNTPHOSTS].value, sntpServerList, ',', MAX_TIME_SERVERS + 1);
	
	// Display timer setup

	os_timer_disarm(&display_timer);
	os_timer_setfn(&display_timer, (os_timer_func_t *)displayTimerExpireCb, (void *)0);

	/* Attempt WIFI connection */
	
	char *ssid = commandElements[CMD_SSID].p.sp;
	char *wifipass = commandElements[CMD_WIFIPASS].p.sp;
	
	INFO("Attempting connection with: %s\r\n", ssid);
	INFO("Root topic: %s\r\n", configInfoBlock.e[MQTTDEVPATH].value);
	
	// Start the connection process
	
	WIFI_Connect(ssid, wifipass, wifiConnectCb);

	// Arm the display timer
	
	os_timer_arm(&display_timer, 1000, 1);
	
	// Free working buffer
	util_free(buf);

	INFO("\r\nSystem started ...\r\n");
	
}

