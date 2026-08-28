#ifndef __RWIFI_TCP_CLIENT_PLUGINS_H__
#define __RWIFI_TCP_CLIENT_PLUGINS_H__
#include "rcore/c_target.h"
#ifdef USE_PRAGMA_ONCE
#    pragma once
#endif

#include "rwifi/c_tcp_client.h"

namespace ncore
{
    namespace nnet
    {
        tcp_recv_plugin_t* new_handshake_plugin(tcp_recv_complete_fn on_complete, void* user_context);
        tcp_recv_plugin_t* new_download_plugin(tcp_recv_complete_fn on_complete, void* user_context);

    }  // namespace nnet
}  // namespace ncore

#endif  // __RWIFI_TCP_CLIENT_PLUGINS_H__
