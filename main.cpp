//#include <iostream>
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
extern "C" {
    int rtapi_app_main(void);
    void rtapi_app_exit(void);   
}
UA_Boolean running = true;

static void stopHandler(int sign) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_CLIENT, "Received Ctrl-C");
    running = 0;
}
typedef struct {
    void *valuePtr;
    hal_type_t type;
} DataSourceContext;
static UA_Client *client = NULL;
static void onDataChange(UA_Client *client, UA_UInt32 monId, void *monContext,
                         UA_UInt32 subId, void *subContext, UA_DataValue *value) {
    DataSourceContext *context = (DataSourceContext*)subContext;
    //rtapi_print_msg(RTAPI_MSG_ERR, "onDataChange: Адрес контекста %p\n", context);

    // Проверка, что контекст не NULL
    if(context == NULL) {
        rtapi_print_msg(RTAPI_MSG_ERR, "onDataChange: Контекст NULL.\n");
        return;
    }
//rtapi_print_msg(RTAPI_MSG_ERR, "onDataChange: Обработка типа %d.\n", context->type);
switch(context->type) {
    case HAL_FLOAT:
    {
        //UA_Variant_hasScalarType(&value->value, &UA_TYPES[UA_TYPES_FLOAT]);
        //    *((hal_float_t*)context->valuePtr) = *(UA_Float*)value->value.data;
        UA_Float uaValue = *(UA_Float*)value->value.data;
        //rtapi_print_msg(RTAPI_MSG_ERR, "onDataChange: Полученное значение UA_Float: %f\n", uaValue);
        *((hal_float_t*)context->valuePtr) = uaValue;
        //rtapi_print_msg(RTAPI_MSG_ERR, "onDataChange: Значение HAL обновлено: %f\n", *((hal_float_t*)context->valuePtr));   
        break;
}
    case HAL_BIT:{
        UA_Variant_hasScalarType(&value->value, &UA_TYPES[UA_TYPES_BOOLEAN]);
        *((hal_bit_t*)context->valuePtr) = *(UA_Boolean*)value->value.data;
           
        
        break;
    }
    case HAL_S32:{
        UA_Variant_hasScalarType(&value->value, &UA_TYPES[UA_TYPES_INT32]);
            *((hal_s32_t*)context->valuePtr) = *(UA_Int32*)value->value.data;
            
        
        break;
    }
    case HAL_U32:
    {
        UA_Variant_hasScalarType(&value->value, &UA_TYPES[UA_TYPES_UINT32]);
            *((hal_u32_t*)context->valuePtr) = *(UA_UInt32*)value->value.data;
            
        
        break;
    }
}
}

static void globalStartFunction(void* arg1, long int arg2) {
    if (client) {
        UA_Client_run_iterate(client, 0);
    }
}
int rtapi_app_main(void) {
     signal(SIGINT, stopHandler);
    client = UA_Client_new();

    comp_id = hal_init("opcuaclient");
    if(comp_id < 0) return comp_id;

UA_UInt32 subId;
int threadPeriod = 50000000;
    try {
        YAML::Node config = YAML::LoadFile(pathYaml);
        if (config["opcua"]) {
            YAML::Node opcua = config["opcua"];
            std::string serverURL = opcua["serverURL"].as<std::string>();
// Попытка подключения к серверу OPC UA
UA_StatusCode status = UA_Client_connect(client, serverURL.c_str());
if(status != UA_STATUSCODE_GOOD) {
    rtapi_print_msg(RTAPI_MSG_ERR, "Ошибка подключения к OPC UA серверу: %s\n", serverURL.c_str());
    return -1;
}

// Создание подписки
UA_CreateSubscriptionRequest request = UA_CreateSubscriptionRequest_default();
UA_CreateSubscriptionResponse response = UA_Client_Subscriptions_create(client, request, NULL, NULL, NULL);
if(response.responseHeader.serviceResult != UA_STATUSCODE_GOOD) {
    rtapi_print_msg(RTAPI_MSG_ERR, "Не удалось создать подписку.\n");
    UA_Client_disconnect(client);
    UA_Client_delete(client);
    return -1;
}
//rtapi_print_msg(RTAPI_MSG_ERR, "Подписка успешно создана.\n");
subId = response.subscriptionId;
        }
            if (config["thread"]) {
        YAML::Node threads = config["thread"];
        std::string threadName = threads["name"].as<std::string>();
        int threadPeriod = threads["threadPeriod"].as<int>();
        int fp = threads["fp"].as<int>();
        hal_create_thread(threadName.c_str(), threadPeriod, fp);
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
            //rtapi_print_msg(RTAPI_MSG_ERR, "onDataChange: Обработка типа %d.\n", context->type);
        } else if (type == "float") {
            hal_float_t** hal_var = (hal_float_t**)hal_malloc(sizeof(hal_float_t*));
            hal_pin_float_newf(HAL_OUT, hal_var, comp_id, "%s", name.c_str());
            context->valuePtr = (void*)(*hal_var);
            context->type = HAL_FLOAT;
            //rtapi_print_msg(RTAPI_MSG_ERR, "onDataChange: Обработка типа %d.\n", context->type);
        } else if (type == "s32") {
            hal_s32_t** hal_var = (hal_s32_t**)hal_malloc(sizeof(hal_s32_t*));
            hal_pin_s32_newf(HAL_OUT, hal_var, comp_id, "%s", name.c_str());
            context->valuePtr = (void*)(*hal_var);
            context->type = HAL_S32;
            //rtapi_print_msg(RTAPI_MSG_ERR, "onDataChange: Обработка типа %d.\n", context->type);

        } else if (type == "u32") {
            hal_u32_t** hal_var = (hal_u32_t**)hal_malloc(sizeof(hal_u32_t*));
            hal_pin_u32_newf(HAL_OUT, hal_var, comp_id, "%s", name.c_str());
            context->valuePtr = (void*)(*hal_var);
            context->type = HAL_U32;
            //rtapi_print_msg(RTAPI_MSG_ERR, "onDataChange: Обработка типа %d.\n", context->type);
        }
        // Создание запроса на подписку
        UA_NodeId nodeId = UA_NODEID_STRING(namespaceIndex, (char*)identifier.c_str());
        UA_MonitoredItemCreateRequest monRequest = UA_MonitoredItemCreateRequest_default(nodeId);
        monRequest.requestedParameters.samplingInterval = static_cast<double>(threadPeriod) / 1000000.0;
        UA_Client_MonitoredItems_createDataChange(client, subId, UA_TIMESTAMPSTORETURN_BOTH, monRequest, context, onDataChange, NULL);
    }
}

    
    }

    catch (const YAML::Exception &e) {
        rtapi_print_msg(RTAPI_MSG_ERR, "Error parsing YAML: %s\n", e.what());
        return -1;
    }
 

  hal_export_funct("OpcUaHalClient", globalStartFunction, 0,0,0, comp_id);
    hal_ready(comp_id);
   //while(running) {
 //   UA_Client_run_iterate(client, 100);
  // }
    //UA_Client_delete(client);
    //hal_exit(comp_id);
    return 0;
}
void rtapi_app_exit(void){
    
    hal_exit(comp_id);
    //UA_Client_delete(client);
}