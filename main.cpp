//#include <iostream>
#include <hal.h>
#include <rtapi.h>
#include <rtapi_app.h>
#include <yaml-cpp/yaml.h>
#include <open62541/client.h>
#include <open62541/client_highlevel.h>

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


int rtapi_app_main(void) {
    UA_Client *client = UA_Client_new();
    
    comp_id = hal_init("opcuaclient");
    if(comp_id < 0) return comp_id;

    try {
        YAML::Node config = YAML::LoadFile(pathYaml);
    if (config["opcua"]) {
        YAML::Node opcua = config["opcua"];
        std::string serverURL = opcua["serverURL"].as<std::string>();
        std::string securityPolicy = opcua["securityPolicy"].as<std::string>();
        std::string securityMode = opcua["securityMode"].as<std::string>();

        UA_StatusCode status = UA_Client_connect(client, serverURL.c_str());
        if(status != UA_STATUSCODE_GOOD) {
            UA_Client_delete(client);
            hal_exit(comp_id);
            rtapi_print_msg(RTAPI_MSG_ERR, "Ошибка подключения к OPC UA серверу: %s\n", serverURL.c_str());
            return -1;
        }
        // Дальнейшие действия с клиентом OPC UA
    }
if (config["variables"]) {
    for (const auto& variable : config["variables"]) {
        std::string name = variable["name"].as<std::string>();
        std::string type = variable["type"].as<std::string>();
        int namespaceIndex = variable["namespaceIndex"].as<int>();
        std::string identifier = variable["identifier"].as<std::string>();

        //UA_NodeId ua_nodeId = UA_NODEID_STRING_ALLOC(1, nodeId.c_str());
        UA_Variant value; 
        UA_Variant_init(&value);


        if (type == "bool") {
            hal_bit_t** hal_var = (hal_bit_t**)hal_malloc(sizeof(hal_bit_t*));
            hal_pin_bit_newf(HAL_OUT, hal_var, comp_id, "%s", name.c_str());
            UA_Client_readValueAttribute(client, UA_NODEID_STRING(namespaceIndex, (char*)identifier.c_str()), &value);
            UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_BOOLEAN]);
            **hal_var = *(UA_Boolean*)value.data;  
        } 
        else if (type == "float") {
            hal_float_t** hal_var = (hal_float_t**)hal_malloc(sizeof(hal_float_t*));
            hal_pin_float_newf(HAL_OUT, hal_var, comp_id, "%s", name.c_str());
            UA_Client_readValueAttribute(client, UA_NODEID_STRING(namespaceIndex, (char*)identifier.c_str()), &value);
            UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_FLOAT]);
            **hal_var = *(UA_Float*)value.data;
                    
        } else if (type == "s32") {
            hal_s32_t** hal_var = (hal_s32_t**)hal_malloc(sizeof(hal_s32_t*));
            hal_pin_s32_newf(HAL_OUT, hal_var, comp_id, "%s", name.c_str());
            UA_Client_readValueAttribute(client, UA_NODEID_STRING(namespaceIndex, (char*)identifier.c_str()), &value);
            UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_INT32]);
            **hal_var = *(UA_Int32*)value.data;

        } else if (type == "u32") {
            hal_u32_t** hal_var = (hal_u32_t**)hal_malloc(sizeof(hal_u32_t*));
            hal_pin_u32_newf(HAL_OUT, hal_var, comp_id, "%s", name.c_str());
            UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_UINT32]);
            **hal_var = *(UA_UInt32*)value.data;
            
        }
        UA_Variant_clear(&value);
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

    catch (const YAML::Exception &e) {
        rtapi_print_msg(RTAPI_MSG_ERR, "Error parsing YAML: %s\n", e.what());
        return -1;
    }
 

 
    hal_ready(comp_id);
    //UA_Client_delete(client);
    return 0;
}
void rtapi_app_exit(void){
    hal_exit(comp_id);
    //UA_Client_delete(client);
}