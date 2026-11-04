#ifndef __RWIFI_WIFI_MANAGER_ESP32_H__
#define __RWIFI_WIFI_MANAGER_ESP32_H__
#include "rcore/c_target.h"
#ifdef USE_PRAGMA_ONCE
#    pragma once
#endif

#ifdef TARGET_ESP32

#include "rwifi/c_wifi_platform.h"

namespace ncore
{
    namespace nnet
    {
        struct wifi_esp32_adapter_t;

        wifi_esp32_adapter_t* create_wifi_esp32_adapter();
        void                  destroy_wifi_esp32_adapter(wifi_esp32_adapter_t* adapter);
        wifi_platform_ops_t   get_wifi_esp32_platform_ops(wifi_esp32_adapter_t* adapter);

    }  // namespace nnet
}  // namespace ncore

#endif  // TARGET_ESP32

#endif  // __RWIFI_WIFI_MANAGER_ESP32_H__
