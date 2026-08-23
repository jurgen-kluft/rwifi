#ifndef __RWIFI_PROTOCOL_H__
#define __RWIFI_PROTOCOL_H__
#include "rcore/c_target.h"
#ifdef USE_PRAGMA_ONCE
#    pragma once
#endif

namespace ncore
{
    namespace nwifi
    {
        // Header for all packets, fixed size 8 bytes, little-endian
        struct msg_hdr_t
        {
            u16 magic;        // Always 0xF00D
            u16 msg_type;     // 0x01 = Handshake, 0x02 = File Init, 0x03 = Data Chunk
            u16 payload_len;  // Payload is always <= 65535 bytes
            u16 checksum;     // Checksum of the payload
        };

    }  // namespace nwifi
}  // namespace ncore

#endif  // __RWIFI_PROTOCOL_H__
