
/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Master Packet Router
 * -------------------------------------------------------------------------
 * This file automatically selects the correct wire-format headers 
 * based on the build flags defined in platformio.ini.
 * -------------------------------------------------------------------------*/


#ifndef VOID_PACKETS_H
#define VOID_PACKETS_H

// Default to SNLP (Community) if nothing is specified (Safety First)
#ifndef VOID_PROTOCOL_TYPE
    #warning "No Protocol Type defined! Defaulting to SNLP (Community)."
    #define VOID_PROTOCOL_TYPE 2 
#endif

// --- ROUTING LOGIC ---

#if VOID_PROTOCOL_TYPE == 1
    // Enterprise Tier: 6-Byte Headers
    // Define the expected sizes (Original)


    #define SIZE_CCSDS_HEADER       6       // CCSDS Primary Header
    #define SIZE_PACKET_A           72      // Invoice (VOID-114B: body 62→66, was 68)
    #define SIZE_PACKET_B           184     // Payment (VOID-114B: body 174 + _tail_pad[4] = 178 → 184 frame, ÷8 ✅)
    #define SIZE_PACKET_C           104     // Receipt
    #define SIZE_PACKET_D           128     // Delivery
    #define SIZE_PACKET_H           112     // Handshake
    #define SIZE_PACKET_ACK         120     // Acknowledgement
    #define SIZE_TUNNEL_DATA        88      // Tunnel Data (from PacketAck_t::enc_tunnel)
    #define SIZE_HEARTBEAT_PCK  40   // 6 (Head) + 32 (Body)


    #define SIZE_VOID_HEADER        SIZE_CCSDS_HEADER

    #include "void_packets_ccsds.h"


#elif VOID_PROTOCOL_TYPE == 2
    // Community Tier: 14-Byte Headers

    // Define the expected sizes (Original + 8 Bytes)
    #define SIZE_SNLP_HEADER        14      // SNLP Header
    #define SIZE_PACKET_A           80      // Invoice (VOID-114B: body 62→66, was 76)
    #define SIZE_PACKET_B           192     // Payment (VOID-114B: body 174 + _tail_pad[4] = 178 → 192 frame, ÷64 ✅)
    #define SIZE_PACKET_C           112     // Receipt
    #define SIZE_PACKET_D           136     // Delivery
    #define SIZE_PACKET_H           120     // Handshake
    #define SIZE_PACKET_ACK         136     // Acknowledgement
    #define SIZE_TUNNEL_DATA        96      // Tunnel Data (from PacketAck_t::enc_tunnel)
    #define SIZE_HEARTBEAT_PCK  48   // 14 (Head) + 32 (Body)


    #define SIZE_VOID_HEADER        SIZE_SNLP_HEADER

    #include "void_packets_snlp.h"


#else
    #error "Unknown VOID_PROTOCOL_TYPE! Use 1 (CCSDS) or 2 (SNLP)."
#endif

#endif // VOID_PACKETS_H