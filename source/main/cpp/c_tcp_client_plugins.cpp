#include "rcore/c_eeprom.h"
#include "rcore/c_network.h"
#include "rcore/c_log.h"
#include "rcore/c_str.h"
#include "rcore/c_system.h"

#include "ccore/c_memory.h"

#include "rwifi/c_wifi.h"
#include "rwifi/c_tcp_client.h"

#ifdef TARGET_ARDUINO

#    include "Arduino.h"

// #    include "rwifi/c_ethernet.h"
#    include "WiFi.h"

#    ifdef TARGET_ESP8266
#        include "ESP8266WiFi.h"
#    endif

#endif

namespace ncore
{
    namespace nnet
    {
        // 8888888b.   .d88888b.  888       888 888b    888 888      .d88888b.        d8888 8888888b.
        // 888  "Y88b d88P" "Y88b 888   o   888 8888b   888 888     d88P" "Y88b      d88888 888  "Y88b
        // 888    888 888     888 888  d8b  888 88888b  888 888     888     888     d88P888 888    888
        // 888    888 888     888 888 d888b 888 888Y88b 888 888     888     888    d88P 888 888    888
        // 888    888 888     888 888d88888b888 888 Y88b888 888     888     888   d88P  888 888    888
        // 888    888 888     888 88888P Y88888 888  Y88888 888     888     888  d88P   888 888    888
        // 888  .d88P Y88b. .d88P 8888P   Y8888 888   Y8888 888     Y88b. .d88P d8888888888 888  .d88P
        // 8888888P"   "Y88888P"  888P     Y888 888    Y888 88888888 "Y88888P" d88P     888 8888888P"

        // NOTE:
        //
        // Downloading data is done in pieces called "blocks".
        // Since this is TCP, the stream will contain the following messages in sequence, until the last block is received.
        // - MSG_TYPE_DOWNLOAD_INFO
        //   - MSG_TYPE_DOWNLOAD_BLOCK_INFO
        //   - MSG_TYPE_DOWNLOAD_BLOCK_DATA
        //   - ...
        //   - ...
        //   - MSG_TYPE_DOWNLOAD_BLOCK_INFO
        //   - MSG_TYPE_DOWNLOAD_BLOCK_DATA
        //   - etc.. until all blocks are received

        // --- MSG TYPE 0x10: Download Info (Remote -> Local) ---
        struct download_info_t
        {
            u32 data_type;     // Custom
            u32 total_blocks;  // Total number of blocks that are to be sent (max 65535 blocks)
            u32 block_size;    // Size of each block
            u32 total_size;    // Up to 4GB
        };

        // --- MSG TYPE 0x11: Download Block Info (Remote -> Local) ---
        struct download_block_info_t
        {
            u16 block_index;  // 0-indexed block counter (max 65535 blocks)
            u16 block_size;   // Size of the following data chunk (max 65535 bytes)
            u32 file_offset;  // Absolute byte offset in file
        };

        struct download_plugin_data_t
        {
            download_info_t           m_download_info;
            download_block_info_t     m_block_info;
            void*                     m_user_ctx;
            tcp_recv_user_acquire_fn  m_on_user_acquire;
            tcp_recv_user_complete_fn m_on_user_complete;
            tcp_recv_user_abort_fn    m_on_user_abort;
            byte*                     m_target_buffer;       // PSRAM destination for the full downloaded data
            u32                       m_target_buffer_size;  // Size of the PSRAM buffer
            u32                       m_received_blocks;     // Number of blocks received so far
            u32                       m_data_type;           // Remember data type
        };

        bool download_acquire_fn(tcp_recv_plugin_t* plugin, msg_hdr_t* in_hdr, buffer_t* out_buffer)
        {
            download_plugin_data_t* plugin_data = (download_plugin_data_t*)plugin->m_plugin_data;

            if (in_hdr->msg_type == MSG_TYPE_DOWNLOAD_INFO)
            {
                ASSERT(in_hdr->payload_len == sizeof(download_info_t));
                out_buffer->m_buffer           = &plugin_data->m_download_info;
                out_buffer->m_length           = sizeof(plugin_data->m_download_info);
                plugin_data->m_received_blocks = 0;
                return true;
            }
            else if (in_hdr->msg_type == MSG_TYPE_DOWNLOAD_BLOCK_INFO)
            {
                ASSERT(in_hdr->payload_len == sizeof(download_block_info_t));
                out_buffer->m_buffer = &plugin_data->m_block_info;
                out_buffer->m_length = sizeof(plugin_data->m_block_info);
                return true;
            }
            else if (in_hdr->msg_type == MSG_TYPE_DOWNLOAD_BLOCK_DATA)
            {
                ASSERT(in_hdr->payload_len == plugin_data->m_block_info.block_size);
                out_buffer->m_buffer = plugin_data->m_target_buffer + plugin_data->m_block_info.file_offset;
                out_buffer->m_length = in_hdr->payload_len;
                return true;
            }

            return false;
        }

        void download_commit_fn(tcp_recv_plugin_t* plugin, msg_hdr_t* hdr, buffer_t buffer)
        {
            download_plugin_data_t* plugin_data = (download_plugin_data_t*)plugin->m_plugin_data;

            if (hdr->msg_type == MSG_TYPE_DOWNLOAD_INFO)
            {
                // Extract download information from the received payload, which is our full download information.
                // This tells us the full size of the download, and the number of blocks that will be needed to
                // complete the download.
                download_info_t* info             = (download_info_t*)buffer.m_buffer;
                plugin_data->m_data_type          = info->data_type;
                plugin_data->m_target_buffer_size = info->total_size;
                plugin_data->m_on_user_acquire(plugin->m_user_ctx, plugin_data->m_data_type, plugin_data->m_target_buffer_size, plugin_data->m_target_buffer);
            }
            else if (hdr->msg_type == MSG_TYPE_DOWNLOAD_BLOCK_INFO)
            {
                // downloaded in plugin_data->m_block_info
            }
            else if (hdr->msg_type == MSG_TYPE_DOWNLOAD_BLOCK_DATA)
            {
                // Note: block data is downloaded directly into the target buffer, and here we are informed that
                //       a block has been received.
                plugin_data->m_received_blocks++;

                // Was this the last block? If so, call the on_complete callback
                if (plugin_data->m_received_blocks == plugin_data->m_download_info.total_blocks)
                {
                    if (plugin->m_on_user_complete)
                    {
                        plugin->m_on_user_complete(plugin->m_user_ctx, plugin_data->m_data_type, plugin_data->m_target_buffer_size, plugin_data->m_target_buffer);

                        // The target buffer is now owned by the user and should be freed by them when done.
                        plugin_data->m_target_buffer      = nullptr;
                        plugin_data->m_target_buffer_size = 0;
                    }
                }
            }
        }

        void download_abort_fn(tcp_recv_plugin_t* plugin)
        {
            // Handle any cleanup or state reset if the handshake is aborted
            download_plugin_data_t* plugin_data = (download_plugin_data_t*)plugin->m_plugin_data;
            if (plugin_data->m_on_user_abort)
            {
                plugin_data->m_on_user_abort(plugin_data->m_user_ctx, plugin_data->m_data_type, plugin_data->m_target_buffer);
            }

            plugin_data->m_target_buffer      = nullptr;
            plugin_data->m_target_buffer_size = 0;
        }

        // There can only be one download plugin active at a time

        tcp_recv_plugin_t* new_download_plugin(tcp_recv_user_acquire_fn on_acquire, tcp_recv_user_complete_fn on_complete, tcp_recv_user_abort_fn on_abort, void* user_ctx)
        {
            download_plugin_data_t* data = (download_plugin_data_t*)nsystem::calloc(sizeof(download_plugin_data_t));
            data->m_user_ctx             = user_ctx;
            data->m_on_user_acquire      = on_acquire;
            data->m_on_user_complete     = on_complete;
            data->m_on_user_abort        = on_abort;
            data->m_target_buffer        = nullptr;  // Destination will be set when receiving the first block
            data->m_target_buffer_size   = 0;
            data->m_received_blocks      = 0;

            tcp_recv_plugin_t* plugin = (tcp_recv_plugin_t*)nsystem::calloc(sizeof(tcp_recv_plugin_t));
            plugin->m_plugin_data     = data;
            plugin->m_acquire         = download_acquire_fn;
            plugin->m_commit          = download_commit_fn;
            plugin->m_abort           = download_abort_fn;
            return plugin;
        }

        void destroy_download_plugin(tcp_recv_plugin_t* plugin)
        {
            nsystem::free(plugin->m_plugin_data);
            nsystem::free(plugin);
        }

        // 888b     d888 8888888888 .d8888b.   .d8888b.        d8888  .d8888b.  8888888888 .d8888b.
        // 8888b   d8888 888       d88P  Y88b d88P  Y88b      d88888 d88P  Y88b 888       d88P  Y88b
        // 88888b.d88888 888       Y88b.      Y88b.          d88P888 888    888 888       Y88b.
        // 888Y88888P888 8888888    "Y888b.    "Y888b.      d88P 888 888        8888888    "Y888b.
        // 888 Y888P 888 888           "Y88b.     "Y88b.   d88P  888 888  88888 888           "Y88b.
        // 888  Y8P  888 888             "888       "888  d88P   888 888    888 888             "888
        // 888   "   888 888       Y88b  d88P Y88b  d88P d8888888888 Y88b  d88P 888       Y88b  d88P
        // 888       888 8888888888 "Y8888P"   "Y8888P" d88P     888  "Y8888P88 8888888888 "Y8888P"

        struct message_plugin_data_t
        {
            void*                     m_user_ctx;
            tcp_recv_user_acquire_fn  m_on_user_acquire;
            tcp_recv_user_complete_fn m_on_user_complete;
            tcp_recv_user_abort_fn    m_on_user_abort;
            byte*                     m_target_buffer;       // Final destination for a message
            u32                       m_target_buffer_size;  // Size of the PSRAM buffer
            u32                       m_data_type;           // Remember data type
        };

        bool message_acquire_fn(tcp_recv_plugin_t* plugin, msg_hdr_t* in_hdr, buffer_t* out_buffer)
        {
            message_plugin_data_t* plugin_data = (message_plugin_data_t*)plugin->m_plugin_data;
            plugin_data->m_data_type           = in_hdr->m_data_type;
            plugin_data->m_target_buffer       = nullptr;
            plugin_data->m_target_buffer_size  = in_hdr->PayloadSize;

            plugin_data->m_on_user_acquire(plugin->m_user_ctx, plugin_data->m_data_type, plugin_data->m_target_buffer_size, plugin_data->m_target_buffer);

            out_buffer->m_data      = plugin_data->m_target_buffer;
            out_buffer->m_data_size = plugin_data->m_target_buffer_size;

            return true;  // Handled download message
        }

        void message_commit_fn(tcp_recv_plugin_t* plugin, msg_hdr_t* hdr, buffer_t buffer)
        {
            message_plugin_data_t* plugin_data = (message_plugin_data_t*)plugin->m_plugin_data;
            plugin_data->m_on_user_complete(plugin->m_user_ctx, plugin_data->m_data_type, buffer.m_data_size, buffer.m_data);
            plugin_data->m_target_buffer      = nullptr;
            plugin_data->m_target_buffer_size = 0;
        }

        void message_abort_fn(tcp_recv_plugin_t* plugin)
        {
            message_plugin_data_t* plugin_data = (message_plugin_data_t*)plugin->m_plugin_data;
            if (plugin_data->m_on_user_abort)
            {
                plugin_data->m_on_user_abort(plugin->m_user_ctx, plugin_data->m_data_type, plugin_data->m_target_buffer_size, plugin_data->m_target_buffer);
            }
            plugin_data->m_target_buffer      = nullptr;
            plugin_data->m_target_buffer_size = 0;
        }

        tcp_recv_plugin_t* new_messages_plugin(tcp_recv_user_acquire_fn on_acquire, tcp_recv_user_complete_fn on_complete, tcp_recv_user_abort_fn on_abort, void* user_ctx)
        {
            message_plugin_data_t* data = (message_plugin_data_t*)nsystem::malloc(sizeof(message_plugin_data_t));
            data->m_user_ctx            = user_ctx;
            data->m_on_user_acquire     = on_acquire;
            data->m_on_user_complete    = on_complete;
            data->m_on_user_abort       = on_abort;
            tcp_recv_plugin_t* plugin   = (tcp_recv_plugin_t*)nsystem::malloc(sizeof(tcp_recv_plugin_t));
            plugin->m_plugin_data       = data;
            plugin->m_acquire           = message_acquire_fn;
            plugin->m_commit            = message_commit_fn;
            plugin->m_abort             = message_abort_fn;
            return plugin;
        }

        void destroy_messages_plugin(tcp_recv_plugin_t* plugin)
        {
            if (plugin)
            {
                if (plugin->m_plugin_data)
                {
                    nsystem::free(plugin->m_plugin_data);
                }
                nsystem::free(plugin);
            }
        }

    }  // namespace nnet
}  // namespace ncore
