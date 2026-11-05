#ifndef __RWIFI_WIFI_MANAGER_ESP8266_H__
#define __RWIFI_WIFI_MANAGER_ESP8266_H__
#include "rcore/c_target.h"
#ifdef USE_PRAGMA_ONCE
#    pragma once
#endif

#ifdef TARGET_ESP8266

#include "rwifi/c_wifi_platform.h"

namespace ncore
{
    namespace nnet
    {
        struct wifi_esp8266_adapter_t;

        wifi_esp8266_adapter_t* create_wifi_esp8266_adapter();
        void                    destroy_wifi_esp8266_adapter(wifi_esp8266_adapter_t* adapter);
        wifi_platform_ops_t     get_wifi_esp8266_platform_ops(wifi_esp8266_adapter_t* adapter);

    }  // namespace nnet
}  // namespace ncore

#endif  // TARGET_ESP8266

#endif  // __RWIFI_WIFI_MANAGER_ESP8266_H__
