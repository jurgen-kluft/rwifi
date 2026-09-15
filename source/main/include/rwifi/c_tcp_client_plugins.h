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
        tcp_recv_plugin_t* new_handshake_plugin(tcp_recv_begin_fn on_handshake, tcp_recv_complete_fn on_complete, void* user_context);
        void               destroy_handshake_plugin(tcp_recv_plugin_t* plugin);

        // Download plugin for handling file downloads where the data is received in chunks
        tcp_recv_plugin_t* new_download_plugin(tcp_recv_begin_fn on_begin, tcp_recv_complete_fn on_complete, void* user_context);
        void               destroy_download_plugin(tcp_recv_plugin_t* plugin);

        // Messages plugin for handling generic incoming messages
        tcp_recv_plugin_t* new_messages_plugin(tcp_recv_begin_fn on_begin, tcp_recv_complete_fn on_complete, void* user_context);
        void               destroy_messages_plugin(tcp_recv_plugin_t* plugin);

    }  // namespace nnet
}  // namespace ncore

#endif  // __RWIFI_TCP_CLIENT_PLUGINS_H__
