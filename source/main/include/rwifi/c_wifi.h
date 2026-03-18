#ifndef __ARDUINO_CORE_WIFI_H__
#define __ARDUINO_CORE_WIFI_H__
#include "rcore/c_target.h"
#ifdef USE_PRAGMA_ONCE
#    pragma once
#endif

#include "rcore/c_state.h"
#include "rcore/c_network.h"
#include "rwifi/c_definitions.h"

namespace ncore
{
    namespace nwifi
    {
        void init_state(state_t* state, bool load_cache);

        void connect(state_t* state, bool force_normal_mode = false);
        bool connected(state_t* state);
        void disconnect(state_t* state);

        IPAddress_t  get_IP(state_t* state);
        u8 const*    get_MAC(state_t* state);
        s32          get_RSSI(state_t* state);

        void print_connection_info(state_t* state);

    }  // namespace nwifi
}  // namespace ncore

#endif  // __ARDUINO_CORE_WIFI_H__
