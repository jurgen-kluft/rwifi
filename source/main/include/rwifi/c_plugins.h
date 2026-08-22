#ifndef __RCORE_WIFI_H__
#define __RCORE_WIFI_H__
#include "rcore/c_target.h"
#ifdef USE_PRAGMA_ONCE
#    pragma once
#endif

namespace ncore
{
    namespace nwifi
    {
        // User defined signature for handling the finished download
        typedef bool (*plugin_acquire_fn_t)(plugin_receiver_t* self, msg_hdr_t const* in_hdr, tcp_buffer_t& out_buffer);
        typedef bool (*plugin_commit_fn_t)(plugin_receiver_t* self, msg_hdr_t const* in_hdr, tcp_buffer_t& in_buffer);
        typedef void (*plugin_abort_fn_t)(plugin_receiver_t* self);
        typedef void (*plugin_complete_fn_t)(u32 msg_type, u32 data_type, u32 data_size, byte const* data_ptr, void* user_context);

        // The plugin handler interface
        struct plugin_receiver_t
        {
            // Context hook: plugin inspects the full msg (Header + Payload).
            // Returns true if this plugin *handled* the message.
            // Returns false if the message belongs to a different plugin.
            plugin_acquire_fn_t  m_on_acquire_fn;
            plugin_commit_fn_t   m_on_commit_fn;
            plugin_abort_fn_t    m_on_abort_fn;
            tcp_client_t*        m_tcp_client;
            plugin_complete_fn_t m_on_complete_fn;
            void*                m_user_context;
        };

        plugin_receiver_t* new_handshake_plugin(tcp_client_t* tcp_client, plugin_complete_fn_t on_complete, void* user_context);
        plugin_receiver_t* new_download_plugin(tcp_client_t* tcp_client, plugin_complete_fn_t on_complete, void* user_context);

    }  // namespace nwifi
}  // namespace ncore

#endif  // __RCORE_WIFI_H__
