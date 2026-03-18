#ifndef __ARDUINO_CORE_WIFI_UDP_H__
#define __ARDUINO_CORE_WIFI_UDP_H__
#include "rcore/c_target.h"
#ifdef USE_PRAGMA_ONCE
#    pragma once
#endif

#include "rwifi/c_definitions.h"

namespace ncore
{
    struct state_t;
    namespace nudp
    {
        void init_state(state_t* state);

        bool open(state_t* state, u16 port);
        void close(state_t* state, u16 port);

        bool open_broadcast(state_t* state, u16 port);
        void close_broadcast(state_t* state, u16 port);

        s32 recv_from(state_t* state, u16 udp_port, byte* data, s32 max_data_size, IPAddress_t& remote_ip, u16& remote_port);
        s32 send_to(state_t* state, u16 udp_port, byte const* data, s32 data_size, const IPAddress_t& to_address, u16 to_port);
    }  // namespace nudp
}  // namespace ncore

#endif  // __ARDUINO_CORE_WIFI_UDP_H__
