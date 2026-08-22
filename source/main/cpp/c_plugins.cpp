#ifdef TARGET_ARDUINO

#    include "Arduino.h"

// #    include "rwifi/c_ethernet.h"
#    include "WiFi.h"

#    ifdef TARGET_ESP8266
#        include "ESP8266WiFi.h"
#    endif

#endif

#include "rcore/c_eeprom.h"
#include "rcore/c_network.h"
#include "rcore/c_log.h"
#include "rcore/c_str.h"
#include "ccore/c_memory.h"

#include "rwifi/c_wifi.h"
#include "rwifi/c_tcp_client.h"

namespace ncore
{
    namespace nwifi
    {
        // --- MSG TYPE 0x01: Handshake (ESP32 -> Mac) ---
        struct payload_handshake_t
        {
            u8 mac_address[6];
            u8 reserved0[2];
            u8 encrypted_msg[16];
            u8 reserved1[8];
            u8 public_key[32];
        };

        struct payload_data_init_t
        {
            u32 data_type;   // Custom
            u32 total_size;  // Up to 4GB
        };

        // --- MSG TYPE 0x03: Data Block Chunk (Mac -> ESP32) ---
        struct data_block_header_t
        {
            u32 block_index;  // 0-indexed block counter
            u32 block_size;   // Size of the following data chunk
            u32 file_offset;  // Absolute byte offset in file
            u32 reserved;     // Padded for 32-bit alignment
            // u8 data[block_size] follows directly in the stream
        };

        // --- ACK PAYLOADS ---
        struct handshake_ack_t : public msg_hdr_t
        {
            u32 status;  // 0x01 = Approved, 0x00 = Denied
        };

        struct data_init_ack_t
        {
            u32 status;  // 0x01 = Ready to receive, 0x00 = Out of memory/Error
        };

        struct data_block_ack_t
        {
            u32 block_index;  // Confirms receipt of specific block
            u32 status;       // 0x01 = Success, 0x00 = Corrupt/Retry
        };

        enum msg_types_t
        {
            MSG_TYPE_HANDSHAKE      = 0x01,
            MSG_TYPE_HANDSHAKE_ACK  = 0x02,
            MSG_TYPE_DATA_INIT      = 0x03,
            MSG_TYPE_DATA_INIT_ACK  = 0x04,
            MSG_TYPE_DATA_BLOCK     = 0x05,
            MSG_TYPE_DATA_BLOCK_ACK = 0x06
        };

        // hand-shake plugin: handles the initial handshake with the Mac, including public key exchange and authentication
        static byte s_handshake_buffer[sizeof(payload_handshake_t)];
        bool handshake_acquire_fn(plugin_receiver_t* self, msg_hdr_t const* in_hdr, tcp_buffer_t& out_buffer)
        {
            if (in_hdr->msg_type != 0x01)
                return false;  // Not a handshake message

            ASSERT(in_hdr->payload_len == sizeof(payload_handshake_t));

            out_buffer.m_buffer = s_handshake_buffer;
            out_buffer.m_length = in_hdr->payload_len;  // Should be sizeof(payload_handshake_t)

            return true;  // Handled handshake message
        }

        bool handshake_commit_fn(plugin_receiver_t* self, msg_hdr_t const* in_hdr, tcp_buffer_t& in_buffer)
        {
            // Call the user-defined callback to indicate handshake completion
            // The user should process the handshake message, e.g., verify public key, authenticate, etc.
            // Then when everything is Ok, it should send back a handshake acknowledgment to the Remote.
            self->m_on_complete_fn(in_hdr->msg_type, 0, in_hdr->payload_len, in_buffer.m_buffer, self->m_user_context);
            return true;
        }

        bool handshake_abort_fn(plugin_receiver_t* self)
        {
            // Handle any cleanup or state reset if the handshake is aborted
            return true;
        }

        plugin_receiver_t* new_handshake_plugin()
        {
            plugin_receiver_t* plugin = new plugin_receiver_t();
            return plugin;
        }

        plugin_receiver_t* new_download_plugin(plugin_complete_fn_t on_complete)
        {
            // todo
            return nullptr;
        }

    }  // namespace nwifi
}  // namespace ncore
