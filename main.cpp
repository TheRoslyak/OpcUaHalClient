//#include <iostream>
//#include "common.h"
#include <unistd.h>
#include <hal.h>
#include <rtapi.h>
#include <signal.h>
#include <rtapi_app.h>
#include <yaml-cpp/yaml.h>
#include <open62541/client.h>
#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>
#include <open62541/client_subscriptions.h>
#include <open62541/plugin/log_stdout.h>

MODULE_AUTHOR("Robert Dremor");
MODULE_DESCRIPTION("Motion Controller for DCS");
MODULE_LICENSE("GPL");

static char *pathYaml ;
RTAPI_MP_STRING(pathYaml, "Path of Yaml File");

static int comp_id;
int threadPeriod = 50000000;
static UA_Client *client = NULL;
std::string serverURL = "opc.tcp://localhost:4840";
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
onDataChange(UA_Client *client, UA_UInt32 monId, void *monContext,
                         UA_UInt32 subId, void *subContext, UA_DataValue *value) {
    DataSourceContext *context = (DataSourceContext*)subContext;
    


switch(context->type) {
    case HAL_FLOAT:
    {
        UA_Float uaValue = *(UA_Float*)value->value.data;
        *((hal_float_t*)context->valuePtr) = uaValue;   
        break;
        }

    case HAL_BIT:{
        UA_Boolean uaValue = *(UA_Boolean*)value->value.data;
        *((hal_bit_t*)context->valuePtr) = uaValue; 
        break;
        }

    case HAL_S32:{
        UA_Int32 uaValue = *(UA_Int32*)value->value.data;
        *((hal_s32_t*)context->valuePtr) = uaValue; 
        break;
    }
    case HAL_U32:
{
        UA_UInt32 uaValue = *(UA_UInt32*)value->value.data;
        *((hal_u32_t*)context->valuePtr) = uaValue; 
        break;
    }
}
}
#define DISCONNECTED 0
#define CONNECTING 1
#define CONNECTED 2
#define SUBSCRIBING 3
#define SUBSCRIBED 4
static long long lastConnectAttempt = 0;
static long long lastSubscriptionAttempt = 0;
static int nState = SUBSCRIBED;

static void globalStartFunction(void *arg, long period) {
    long long now = rtapi_get_clocks(); 

    switch (nState) {
        case DISCONNECTED:
            if (now - lastConnectAttempt >= 50000000000){
                rtapi_print_msg(RTAPI_MSG_ERR, "Attempting to reconnect.\n");
                UA_Client_connectAsync(client, serverURL.c_str());
                UA_CreateSubscriptionRequest request = UA_CreateSubscriptionRequest_default();
                UA_Client_Subscriptions_create(client, request, NULL, NULL, NULL);
                lastConnectAttempt = now;
                if (globalConnectStatus == UA_STATUSCODE_GOOD) {
                    nState =  SUBSCRIBING;
                }
            }
            break;

        case SUBSCRIBING:
            if (now - lastSubscriptionAttempt >= 50000000000) {

                for (auto& subContext : subscriptionContexts) {
                    DataSourceContext *tempContext = subContext.dataSourceContext;
                    UA_NodeId nodeId = UA_NODEID_STRING(subContext.namespaceIndex, (char*)subContext.identifier.c_str());
                    UA_MonitoredItemCreateRequest monRequest = UA_MonitoredItemCreateRequest_default(nodeId);
                    monRequest.requestedParameters.samplingInterval = static_cast<double>(threadPeriod) / 1000000.0;
                    UA_MonitoredItemCreateResult monResponse = UA_Client_MonitoredItems_createDataChange(client, subId, UA_TIMESTAMPSTORETURN_BOTH, monRequest, tempContext, onDataChange, NULL);
                    if(monResponse.statusCode != UA_STATUSCODE_GOOD) {
                        rtapi_print_msg(RTAPI_MSG_ERR, "Failed to recreate MonitoredItem for %s. ns=%d,s=%s\n", subContext.identifier.c_str(), subContext.namespaceIndex, subContext.identifier.c_str());
                    }
                }
                lastSubscriptionAttempt = now;
                nState = SUBSCRIBED;
            }
            break;

        case SUBSCRIBED:
            UA_Client_run_iterate(client, 0); // Итерация без задержки
            break;
    }
}
static void
onConnect(UA_Client *client, UA_SecureChannelState channelState,
          UA_SessionState sessionState, UA_StatusCode connectStatus) {
    globalConnectStatus = connectStatus;
    if (globalConnectStatus != UA_STATUSCODE_GOOD) nState = DISCONNECTED;

}
int rtapi_app_main(void) {
    
    client = UA_Client_new();
    
    cc = UA_Client_getConfig(client);
    
    
    comp_id = hal_init("opcuaclient");
    if(comp_id < 0) return comp_id;

    try {
        YAML::Node config = YAML::LoadFile(pathYaml);
        if (config["opcua"]) {
            YAML::Node opcua = config["opcua"];
            serverURL = opcua["serverURL"].as<std::string>();
            cc->stateCallback = onConnect;
            UA_Client_connectAsync(client, serverURL.c_str());
            UA_CreateSubscriptionRequest request = UA_CreateSubscriptionRequest_default();
            UA_CreateSubscriptionResponse response = UA_Client_Subscriptions_create(client, request, NULL, NULL, NULL);

if (globalConnectStatus != UA_STATUSCODE_GOOD) {
    rtapi_print_msg(RTAPI_MSG_ERR, "Error connecting to OPC UA server: %s\n", serverURL.c_str());
    UA_Client_delete(client);
    hal_exit(comp_id);
    return -1;   
} else rtapi_print_msg(RTAPI_MSG_ERR,"Connect returned with status code %s\n",
           UA_StatusCode_name(globalConnectStatus));


subId = response.subscriptionId;
        }
      
if (config["variables"]) {
    for (const auto& variable : config["variables"]) {
        std::string name = variable["name"].as<std::string>();
        std::string type = variable["type"].as<std::string>();
        int namespaceIndex = variable["namespaceIndex"].as<int>();
        std::string identifier = variable["identifier"].as<std::string>();

        DataSourceContext *context = new DataSourceContext;

        if (type == "bool") {
            hal_bit_t** hal_var = (hal_bit_t**)hal_malloc(sizeof(hal_bit_t*));
            hal_pin_bit_newf(HAL_OUT, hal_var, comp_id, "%s", name.c_str());
            context->valuePtr = (void*)(*hal_var);
            context->type = HAL_BIT;
            
        } else if (type == "float") {
            hal_float_t** hal_var = (hal_float_t**)hal_malloc(sizeof(hal_float_t*));
            hal_pin_float_newf(HAL_OUT, hal_var, comp_id, "%s", name.c_str());
            context->valuePtr = (void*)(*hal_var);
            context->type = HAL_FLOAT;
            
        } else if (type == "s32") {
            hal_s32_t** hal_var = (hal_s32_t**)hal_malloc(sizeof(hal_s32_t*));
            hal_pin_s32_newf(HAL_OUT, hal_var, comp_id, "%s", name.c_str());
            context->valuePtr = (void*)(*hal_var);
            context->type = HAL_S32;
            
        } else if (type == "u32") {
            hal_u32_t** hal_var = (hal_u32_t**)hal_malloc(sizeof(hal_u32_t*));
            hal_pin_u32_newf(HAL_OUT, hal_var, comp_id, "%s", name.c_str());
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
        monRequest.requestedParameters.samplingInterval = static_cast<double>(threadPeriod) / 1000000.0;
        
        UA_MonitoredItemCreateResult monResponse = UA_Client_MonitoredItems_createDataChange(client, subId, UA_TIMESTAMPSTORETURN_BOTH, monRequest, context, onDataChange, NULL);
        
        if(monResponse.statusCode != UA_STATUSCODE_GOOD) {
            rtapi_print_msg(RTAPI_MSG_ERR, "Failed to create MonitoredItem for %s. ns=%d,s=%s\n",
                            name.c_str(), namespaceIndex, identifier.c_str());
        }
        
    }
    if (config["thread"]) {
        YAML::Node threads = config["thread"];
        std::string threadName = threads["name"].as<std::string>();
        int threadPeriod = threads["threadPeriod"].as<int>();
        int fp = threads["fp"].as<int>();
        hal_create_thread(threadName.c_str(), threadPeriod, fp);
      }
}

    }

    catch (const YAML::Exception &e) {
        rtapi_print_msg(RTAPI_MSG_ERR, "Error parsing YAML: %s\n", e.what());
        return -1;
    }
 

    hal_export_funct("OpcUaHalClient", globalStartFunction, 0,0,0, comp_id);
    hal_ready(comp_id);

    return 0;
}
void rtapi_app_exit(void){
    UA_Client_disconnect(client);
    UA_Client_delete(client);
    hal_exit(comp_id); 
}