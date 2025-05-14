#include <iostream>
//#include "common.h"
#include <unistd.h>
#include <hal.h>
#include <signal.h>
#include <rtapi.h>
#include <rtapi_mutex.h>
#include <yaml-cpp/yaml.h>
#include <open62541/client.h>
#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>
#include <open62541/client_subscriptions.h>
#include <open62541/plugin/log_stdout.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>

//MODULE_AUTHOR("Robert Dremor");
//MODULE_DESCRIPTION("Motion Controller for DCS");
//MODULE_LICENSE("GPL");

static char *pathYaml = NULL;
//RTAPI_MP_STRING(pathYaml, "Path of Yaml File");

static int comp_id;
static int rtapi_id;
static UA_Client *client = NULL;
std::string serverURL = "opc.tcp://192.168.26.135:4840";
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
    std::cerr << "Client connection status changed. Channel: " << channelState 
              << ", Session: " << sessionState << ", Status: " << UA_StatusCode_name(connectStatus) << std::endl;
}

static void onDataChange(UA_Client *client, UA_UInt32 monId, void *monContext,
                         UA_UInt32 subId, void *subContext, UA_DataValue *value) {
    DataSourceContext *context = (DataSourceContext*)subContext;
    
    std::cerr << "Received data change for subscription " << subId << std::endl;
    
    if (!value || !value->hasValue) {
        std::cerr << "Error: Empty value received" << std::endl;
        return;
    }

    switch(context->type) {
        case HAL_FLOAT:
        {
            UA_Float uaValue = *(UA_Float*)value->value.data;
            *((hal_float_t*)context->valuePtr) = uaValue;
            std::cerr << "Updated HAL_FLOAT value: " << uaValue << std::endl;
            break;
        }
        case HAL_BIT:
        {
            UA_Boolean uaValue = *(UA_Boolean*)value->value.data;
            *((hal_bit_t*)context->valuePtr) = uaValue;
            std::cerr << "Updated HAL_BIT value: " << uaValue << std::endl;
            break;
        }
        case HAL_S32:
        {
            UA_Int32 uaValue = *(UA_Int32*)value->value.data;
            *((hal_s32_t*)context->valuePtr) = uaValue;
            std::cerr << "Updated HAL_S32 value: " << uaValue << std::endl;
            break;
        }
        case HAL_U32:
        {
            UA_UInt32 uaValue = *(UA_UInt32*)value->value.data;
            *((hal_u32_t*)context->valuePtr) = uaValue;
            std::cerr << "Updated HAL_U32 value: " << uaValue << std::endl;
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
        std::cerr << "Error: Connection status not good: " << UA_StatusCode_name(globalConnectStatus) << std::endl;
        
        if (now - lastConnectAttempt >= 5000000000) {
            reconnectCount++;
            std::cerr << "Info: Attempting to reconnect (attempt " << reconnectCount << ")" << std::endl;
            
            UA_StatusCode retval = UA_Client_connect(client, serverURL.c_str());
            lastConnectAttempt = now;
            
            if (retval == UA_STATUSCODE_GOOD) {
                std::cerr << "Successfully reconnected to server" << std::endl;
                
                UA_CreateSubscriptionRequest request = UA_CreateSubscriptionRequest_default();
                UA_CreateSubscriptionResponse response = 
                    UA_Client_Subscriptions_create(client, request, NULL, NULL, NULL);
                
                if (response.responseHeader.serviceResult == UA_STATUSCODE_GOOD) {
                    subId = response.subscriptionId;
                    std::cerr << "Recreated subscription with ID " << subId << std::endl;
                    
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
                            std::cerr << "Recreated monitoring for " << subContext.identifier << std::endl;
                        } else {
                            std::cerr << "Failed to recreate monitoring for " << subContext.identifier 
                                    << ": " << UA_StatusCode_name(monResponse.statusCode) << std::endl;
                        }
                    }
                }
            } else {
                std::cerr << "Reconnection failed: " << UA_StatusCode_name(retval) << std::endl;
            }
        }
    }
    
    UA_StatusCode retval = UA_Client_run_iterate(client, 100);
    if (retval != UA_STATUSCODE_GOOD) {
        std::cerr << "Client iteration failed: " << UA_StatusCode_name(retval) << std::endl;
    }
}

void signal_handler(int signo) {
    if (signo == SIGINT || signo == SIGTERM) {
        running = false;
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <yaml_config_file>" << std::endl;
        return -1;
    }

    rtapi_id = hal_init("OPCUACLIENT");
    if (rtapi_id < 0) {
        std::cerr << "Error: RTAPI init failed" << std::endl;
        return -1;
    }

    rtapi_set_msg_level(RTAPI_MSG_ALL);
    
    std::cerr << "Starting OPC UA HAL Client" << std::endl;
    
    char abs_path[PATH_MAX];
    char *res = realpath(argv[1], abs_path);
    if (res == NULL) {
        std::cerr << "Failed to resolve path: " << argv[1] << std::endl;
       
        return -1;
    }
    pathYaml = strdup(abs_path);
    
    std::cerr << "Using YAML config file: " << pathYaml << std::endl;
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    std::cerr << "Creating OPC UA client" << std::endl;
    client = UA_Client_new();
    if (!client) {
        std::cerr << "Failed to create client" << std::endl;
        return -1;
    }

    cc = UA_Client_getConfig(client);
    cc->stateCallback = onConnect;
    
    std::cerr << "Initializing HAL component" << std::endl;
    comp_id = hal_init("opcuaclient");
    if(comp_id < 0) {
        std::cerr << "Failed to initialize HAL component" << std::endl;
        return comp_id;
    }

    try {
        std::cerr << "Loading YAML configuration" << std::endl;
        YAML::Node config = YAML::LoadFile(pathYaml);
        
        if (!config["opcua"]) {
            std::cerr << "No OPC UA configuration found in YAML" << std::endl;
            UA_Client_delete(client);
            hal_exit(comp_id);
            return -1;
        }

        YAML::Node opcua = config["opcua"];
        serverURL = opcua["serverURL"].as<std::string>();
        std::cerr << "Connecting to server: " << serverURL << std::endl;
        
        UA_StatusCode retval = UA_Client_connect(client, serverURL.c_str());
        if(retval != UA_STATUSCODE_GOOD) {
            std::cerr << "Failed to connect to server. Status code: " << UA_StatusCode_name(retval) << std::endl;
            UA_Client_delete(client);
            hal_exit(comp_id);
            return -1;
        }
        
        std::cerr << "Successfully connected to server" << std::endl;
        
        std::cerr << "Creating subscription" << std::endl;
        UA_CreateSubscriptionRequest request = UA_CreateSubscriptionRequest_default();
        UA_CreateSubscriptionResponse response = UA_Client_Subscriptions_create(client, request, NULL, NULL, NULL);
        if (response.responseHeader.serviceResult != UA_STATUSCODE_GOOD) {
            std::cerr << "Failed to create subscription" << std::endl;
            UA_Client_disconnect(client);
            UA_Client_delete(client);
            hal_exit(comp_id);
            return -1;
        }
        
        subId = response.subscriptionId;
        std::cerr << "Created subscription with ID " << subId << std::endl;

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
                    std::cerr << "Failed to create MonitoredItem for " << name << ". ns=" 
                             << namespaceIndex << ",s=" << identifier << std::endl;
                } else {
                    std::cerr << "Successfully created MonitoredItem for " << name << std::endl;
                }
            }
        }

        std::cerr << "Not connected. Retrying to connect in 1 second" << std::endl;
        std::cerr << "HAL component ready, entering main loop" << std::endl;
        hal_ready(comp_id);
        while(running) {
            check_connection_status();
            usleep(10000);
        }

        std::cerr << "Cleaning up..." << std::endl;
        UA_Client_disconnect(client);
        UA_Client_delete(client);
        hal_exit(comp_id);
        rtapi_exit(rtapi_id);
        return 0;

    } catch (const YAML::Exception &e) {
        std::cerr << "Error parsing YAML: " << e.what() << std::endl;
        return -1;
    }
}

