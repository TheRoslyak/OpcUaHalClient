//#include <iostream>
//#include "common.h"
#include <unistd.h>
#include <hal.h>
#include <signal.h>
#include <rtapi_mutex.h>
#include <yaml-cpp/yaml.h>
#include <open62541/client.h>
#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>
#include <open62541/client_subscriptions.h>
#include <open62541/plugin/log_stdout.h>
#include <limits.h>
#include <stdlib.h>

//MODULE_AUTHOR("Robert Dremor");
//MODULE_DESCRIPTION("Motion Controller for DCS");
//MODULE_LICENSE("GPL");

static char *pathYaml = NULL;
//RTAPI_MP_STRING(pathYaml, "Path of Yaml File");

static int comp_id;
static UA_Client *client = NULL;
std::string serverURL = "opc.tcp://192.168.26.134:4840";
UA_StatusCode globalConnectStatus;
UA_ClientConfig *cc;
UA_UInt32 subId;
typedef struct {
    void *valuePtr;
    hal_type_t type;
} DataSourceContext;

struct SubscriptionContext {
    int namespaceIndex;
    std::string identifier;
    DataSourceContext *dataSourceContext; 
};

std::vector<SubscriptionContext> subscriptionContexts;

extern "C" {
    int rtapi_app_main(void);
    void rtapi_app_exit(void);   
}

static void
onConnect(UA_Client *client, UA_SecureChannelState channelState,
          UA_SessionState sessionState, UA_StatusCode connectStatus) {
    globalConnectStatus = connectStatus;
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                "Client connection status changed. Channel: %d, Session: %d, Status: %s",
                channelState, sessionState, UA_StatusCode_name(connectStatus));
}

static void onDataChange(UA_Client *client, UA_UInt32 monId, void *monContext,
                         UA_UInt32 subId, void *subContext, UA_DataValue *value) {
    DataSourceContext *context = (DataSourceContext*)subContext;
    
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                "Received data change notification for subscription %u", subId);
    
    if (!value || !value->hasValue) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                    "Received empty value in data change notification");
        return;
    }

    switch(context->type) {
        case HAL_FLOAT:
        {
            UA_Float uaValue = *(UA_Float*)value->value.data;
            *((hal_float_t*)context->valuePtr) = uaValue;
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                       "Updated HAL_FLOAT value: %f", uaValue);
            break;
        }
        case HAL_BIT:
        {
            UA_Boolean uaValue = *(UA_Boolean*)value->value.data;
            *((hal_bit_t*)context->valuePtr) = uaValue;
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                       "Updated HAL_BIT value: %d", uaValue);
            break;
        }
        case HAL_S32:
        {
            UA_Int32 uaValue = *(UA_Int32*)value->value.data;
            *((hal_s32_t*)context->valuePtr) = uaValue;
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                       "Updated HAL_S32 value: %d", uaValue);
            break;
        }
        case HAL_U32:
        {
            UA_UInt32 uaValue = *(UA_UInt32*)value->value.data;
            *((hal_u32_t*)context->valuePtr) = uaValue;
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                       "Updated HAL_U32 value: %u", uaValue);
            break;
        }
    }
}

static long long lastConnectAttempt = 0;
static bool running = true;

void check_connection_status() {
    static int reconnectCount = 0;
    long long now = rtapi_get_clocks();

    if (globalConnectStatus != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                    "Connection status not good: %s", UA_StatusCode_name(globalConnectStatus));
        
        if (now - lastConnectAttempt >= 5000000000) { // Reduced interval to 5 seconds
            reconnectCount++;
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                       "Attempting to reconnect (attempt %d)", reconnectCount);
            
            UA_StatusCode retval = UA_Client_connect(client, serverURL.c_str());
            lastConnectAttempt = now;
            
            if (retval == UA_STATUSCODE_GOOD) {
                UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                           "Successfully reconnected to server");
                
                UA_CreateSubscriptionRequest request = UA_CreateSubscriptionRequest_default();
                UA_CreateSubscriptionResponse response = 
                    UA_Client_Subscriptions_create(client, request, NULL, NULL, NULL);
                
                if (response.responseHeader.serviceResult == UA_STATUSCODE_GOOD) {
                    subId = response.subscriptionId;
                    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                               "Recreated subscription with ID %u", subId);
                    
                    // Recreate monitoring for all variables
                    for (auto& subContext : subscriptionContexts) {
                        UA_NodeId nodeId = UA_NODEID_STRING(subContext.namespaceIndex, 
                                                          (char*)subContext.identifier.c_str());
                        UA_MonitoredItemCreateRequest monRequest = 
                            UA_MonitoredItemCreateRequest_default(nodeId);
                        monRequest.requestedParameters.samplingInterval = 100.0;
                        
                        UA_MonitoredItemCreateResult monResponse = 
                            UA_Client_MonitoredItems_createDataChange(client, subId,
                                UA_TIMESTAMPSTORETURN_BOTH, monRequest,
                                subContext.dataSourceContext, onDataChange, NULL);
                        
                        if (monResponse.statusCode == UA_STATUSCODE_GOOD) {
                            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                                       "Recreated monitoring for %s", 
                                       subContext.identifier.c_str());
                        } else {
                            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                                       "Failed to recreate monitoring for %s: %s",
                                       subContext.identifier.c_str(),
                                       UA_StatusCode_name(monResponse.statusCode));
                        }
                    }
                }
            } else {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                           "Reconnection failed: %s", UA_StatusCode_name(retval));
            }
        }
    }
    
    UA_StatusCode retval = UA_Client_run_iterate(client, 100);
    if (retval != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                    "Client iteration failed: %s", UA_StatusCode_name(retval));
    }
}

void signal_handler(int signo) {
    if (signo == SIGINT || signo == SIGTERM) {
        running = false;
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        rtapi_print_msg(RTAPI_MSG_ERR, "Usage: %s <yaml_config_file>\n", argv[0]);
        return -1;
    }
    
    rtapi_print_msg(RTAPI_MSG_ERR, "Starting OPC UA HAL Client\n");
    
    // Get absolute path
    char abs_path[PATH_MAX];
    char *res = realpath(argv[1], abs_path);
    if (res == NULL) {
        rtapi_print_msg(RTAPI_MSG_ERR, "Failed to resolve path: %s\n", argv[1]);
        return -1;
    }
    pathYaml = strdup(abs_path);
    
    rtapi_print_msg(RTAPI_MSG_ERR, "Using YAML config file: %s\n", pathYaml);
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    rtapi_print_msg(RTAPI_MSG_ERR, "Creating OPC UA client\n");
    client = UA_Client_new();
    if (!client) {
        rtapi_print_msg(RTAPI_MSG_ERR, "Failed to create client\n");
        return -1;
    }

    cc = UA_Client_getConfig(client);
    cc->stateCallback = onConnect;
    
    rtapi_print_msg(RTAPI_MSG_ERR, "Initializing HAL component\n");
    comp_id = hal_init("opcuaclient");
    if(comp_id < 0) {
        rtapi_print_msg(RTAPI_MSG_ERR, "Failed to initialize HAL component\n");
        return comp_id;
    }

    try {
        rtapi_print_msg(RTAPI_MSG_ERR, "Loading YAML configuration\n");
        YAML::Node config = YAML::LoadFile(pathYaml);
        
        if (!config["opcua"]) {
            rtapi_print_msg(RTAPI_MSG_ERR, "No OPC UA configuration found in YAML\n");
            UA_Client_delete(client);
            hal_exit(comp_id);
            return -1;
        }

        YAML::Node opcua = config["opcua"];
        serverURL = opcua["serverURL"].as<std::string>();
        rtapi_print_msg(RTAPI_MSG_ERR, "Connecting to server: %s\n", serverURL.c_str());
        
        UA_StatusCode retval = UA_Client_connect(client, serverURL.c_str());
        if(retval != UA_STATUSCODE_GOOD) {
            rtapi_print_msg(RTAPI_MSG_ERR, "Failed to connect to server. Status code: %s\n", 
                          UA_StatusCode_name(retval));
            UA_Client_delete(client);
            hal_exit(comp_id);
            return -1;
        }
        
        rtapi_print_msg(RTAPI_MSG_ERR, "Successfully connected to server\n");
        
        rtapi_print_msg(RTAPI_MSG_ERR, "Creating subscription\n");
        UA_CreateSubscriptionRequest request = UA_CreateSubscriptionRequest_default();
        UA_CreateSubscriptionResponse response = UA_Client_Subscriptions_create(client, request, NULL, NULL, NULL);
        if (response.responseHeader.serviceResult != UA_STATUSCODE_GOOD) {
            rtapi_print_msg(RTAPI_MSG_ERR, "Failed to create subscription\n");
            UA_Client_disconnect(client);
            UA_Client_delete(client);
            hal_exit(comp_id);
            return -1;
        }
        
        subId = response.subscriptionId;
        rtapi_print_msg(RTAPI_MSG_ERR, "Created subscription with ID %u\n", subId);

        if (config["variables"]) {
            for (const auto& variable : config["variables"]) {
                std::string name = variable["name"].as<std::string>();
                std::string type = variable["type"].as<std::string>();
                std::string dir = variable["dir"].as<std::string>();
                int namespaceIndex = variable["namespaceIndex"].as<int>();
                std::string identifier = variable["identifier"].as<std::string>();

                hal_pin_dir_t pin_dir = HAL_OUT;  // default direction
                if (dir == "HAL_IN") {
                    pin_dir = HAL_IN;
                } else if (dir == "HAL_OUT") {
                    pin_dir = HAL_OUT;
                } else if (dir == "HAL_IO") {
                    pin_dir = HAL_IO;
                }

                DataSourceContext *context = new DataSourceContext;

                if (type == "bool") {
                    hal_bit_t** hal_var = (hal_bit_t**)hal_malloc(sizeof(hal_bit_t*));
                    hal_pin_bit_newf(pin_dir, hal_var, comp_id, "%s", name.c_str());
                    context->valuePtr = (void*)(*hal_var);
                    context->type = HAL_BIT;
                    
                } else if (type == "float") {
                    hal_float_t** hal_var = (hal_float_t**)hal_malloc(sizeof(hal_float_t*));
                    hal_pin_float_newf(pin_dir, hal_var, comp_id, "%s", name.c_str());
                    context->valuePtr = (void*)(*hal_var);
                    context->type = HAL_FLOAT;
                    
                } else if (type == "s32") {
                    hal_s32_t** hal_var = (hal_s32_t**)hal_malloc(sizeof(hal_s32_t*));
                    hal_pin_s32_newf(pin_dir, hal_var, comp_id, "%s", name.c_str());
                    context->valuePtr = (void*)(*hal_var);
                    context->type = HAL_S32;
                    
                } else if (type == "u32") {
                    hal_u32_t** hal_var = (hal_u32_t**)hal_malloc(sizeof(hal_u32_t*));
                    hal_pin_u32_newf(pin_dir, hal_var, comp_id, "%s", name.c_str());
                    context->valuePtr = (void*)(*hal_var);
                    context->type = HAL_U32; 
                }

                SubscriptionContext subContext;
                subContext.namespaceIndex = namespaceIndex;
                subContext.identifier = identifier;
                subContext.dataSourceContext = context;
                subscriptionContexts.push_back(subContext);

                UA_NodeId nodeId = UA_NODEID_STRING(namespaceIndex, (char*)identifier.c_str());
                UA_MonitoredItemCreateRequest monRequest = UA_MonitoredItemCreateRequest_default(nodeId);
                monRequest.requestedParameters.samplingInterval = 100.0;
                
                UA_MonitoredItemCreateResult monResponse = 
                    UA_Client_MonitoredItems_createDataChange(client, subId, 
                        UA_TIMESTAMPSTORETURN_BOTH, monRequest, 
                        context, onDataChange, NULL);
                
                if(monResponse.statusCode != UA_STATUSCODE_GOOD) {
                    rtapi_print_msg(RTAPI_MSG_ERR, "Failed to create MonitoredItem for %s. ns=%d,s=%s\n",
                                  name.c_str(), namespaceIndex, identifier.c_str());
                } else {
                    rtapi_print_msg(RTAPI_MSG_ERR, "Successfully created MonitoredItem for %s\n", name.c_str());
                }
            }
        }

                         UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                         "Not connected. Retrying to connect in 1 second");
        rtapi_print_msg(RTAPI_MSG_ERR, "HAL component ready, entering main loop\n");
        hal_ready(comp_id);
        while(running) {
            check_connection_status(); // Increase timeout to 1000ms
            usleep(10000); // Increase delay to 10ms
        }

        rtapi_print_msg(RTAPI_MSG_ERR, "Cleaning up...\n");
        UA_Client_disconnect(client);
        UA_Client_delete(client);
        hal_exit(comp_id);
        return 0;

    } catch (const YAML::Exception &e) {
        rtapi_print_msg(RTAPI_MSG_ERR, "Error parsing YAML: %s\n", e.what());
        return -1;
    }
}

