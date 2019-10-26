/* icmp.c
 *
 * Copyright 2019 AOL Inc. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this Software except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "moloch.h"
#include "patricia.h"
#include <inttypes.h>
#include <arpa/inet.h>
#include <errno.h>


/******************************************************************************/
extern MolochConfig_t        config;

LOCAL int                    icmpMProtocol;
LOCAL int                    icmpv6MProtocol;

LOCAL int                    icmpTypeField;
LOCAL int                    icmpCodeField;
LOCAL int                    icmpPayloadSrcIp;
LOCAL int                    icmpPayloadDstIp;


/******************************************************************************/
SUPPRESS_ALIGNMENT
int icmp_packet_enqueue(MolochPacketBatch_t * UNUSED(batch), MolochPacket_t * const packet, const uint8_t *UNUSED(data), int UNUSED(len))
{
    uint8_t                 sessionId[MOLOCH_SESSIONID_LEN];

    if (packet->v6) {
        struct ip6_hdr *ip6 = (struct ip6_hdr *)(packet->pkt + packet->ipOffset);
        moloch_session_id6(sessionId, ip6->ip6_src.s6_addr, 0, ip6->ip6_dst.s6_addr, 0);
    } else {
        struct ip *ip4 = (struct ip*)(packet->pkt + packet->ipOffset);
        moloch_session_id(sessionId, ip4->ip_src.s_addr, 0, ip4->ip_dst.s_addr, 0);
    }
    packet->mProtocol = icmpMProtocol;
    packet->hash = moloch_session_hash(sessionId);
    return MOLOCH_PACKET_DO_PROCESS;
}
/******************************************************************************/
SUPPRESS_ALIGNMENT
int icmpv6_packet_enqueue(MolochPacketBatch_t * UNUSED(batch), MolochPacket_t * const packet, const uint8_t *UNUSED(data), int UNUSED(len))
{
    uint8_t                 sessionId[MOLOCH_SESSIONID_LEN];

    if (!packet->v6)
        return MOLOCH_PACKET_CORRUPT;

    struct ip6_hdr *ip6 = (struct ip6_hdr *)(packet->pkt + packet->ipOffset);
    moloch_session_id6(sessionId, ip6->ip6_src.s6_addr, 0, ip6->ip6_dst.s6_addr, 0);
    packet->mProtocol = icmpv6MProtocol;
    packet->hash = moloch_session_hash(sessionId);
    return MOLOCH_PACKET_DO_PROCESS;
}
/******************************************************************************/
SUPPRESS_ALIGNMENT
void icmp_create_sessionid(uint8_t *sessionId, MolochPacket_t *packet)
{
    struct ip           *ip4 = (struct ip*)(packet->pkt + packet->ipOffset);
    struct ip6_hdr      *ip6 = (struct ip6_hdr*)(packet->pkt + packet->ipOffset);

    if (packet->v6) {
        moloch_session_id6(sessionId, ip6->ip6_src.s6_addr, 0, ip6->ip6_dst.s6_addr, 0);
    } else {
        moloch_session_id(sessionId, ip4->ip_src.s_addr, 0, ip4->ip_dst.s_addr, 0);
    }
}
/******************************************************************************/
SUPPRESS_ALIGNMENT
void icmp_pre_process(MolochSession_t *session, MolochPacket_t * const packet, int isNewSession)
{
    struct ip           *ip4 = (struct ip*)(packet->pkt + packet->ipOffset);
    struct ip6_hdr      *ip6 = (struct ip6_hdr*)(packet->pkt + packet->ipOffset);

    if (isNewSession)
        moloch_session_add_protocol(session, "icmp");

    int dir;
    if (ip4->ip_v == 4) {
        dir = (MOLOCH_V6_TO_V4(session->addr1) == ip4->ip_src.s_addr &&
               MOLOCH_V6_TO_V4(session->addr2) == ip4->ip_dst.s_addr);

    } else {
        dir = (memcmp(session->addr1.s6_addr, ip6->ip6_src.s6_addr, 16) == 0 &&
               memcmp(session->addr2.s6_addr, ip6->ip6_dst.s6_addr, 16) == 0);
    }

    packet->direction = (dir)?0:1;
    session->databytes[packet->direction] += (packet->pktlen -packet->payloadOffset);
}
/******************************************************************************/
int icmp_process(MolochSession_t *session, MolochPacket_t * const packet)
{
    const uint8_t *data = packet->pkt + packet->payloadOffset;

    if (packet->payloadLen >= 2) {
        moloch_field_int_add(icmpTypeField, session, data[0]);
        moloch_field_int_add(icmpCodeField, session, data[1]);
    }

    switch (data[0]) {
      case 3:
      case 11:
				LOG("packet paylaodlen=%d", packet->payloadLen);
				// icmp header (8) + ip header (20) + proto header (8)
        if (packet->payloadLen >= 36) {
          char srcip[32];
          char dstip[32];

          sprintf (srcip, "%d.%d.%d.%d", packet->pkt[58-4], packet->pkt[55], packet->pkt[56], packet->pkt[57]);
          moloch_field_string_add(icmpPayloadSrcIp, session, srcip, -1, TRUE);

          sprintf (dstip, "%d.%d.%d.%d", packet->pkt[58], packet->pkt[59], packet->pkt[60], packet->pkt[61]);
          moloch_field_string_add(icmpPayloadDstIp, session, dstip, -1, TRUE);
        } else {
          moloch_field_string_add(icmpPayloadSrcIp, session, "error", -1, TRUE);
          moloch_field_string_add(icmpPayloadDstIp, session, "error", -1, TRUE);
        }
				break;
    }

    return 1;
}
/******************************************************************************/
SUPPRESS_ALIGNMENT
void icmpv6_create_sessionid(uint8_t *sessionId, MolochPacket_t *packet)
{
    struct ip6_hdr      *ip6 = (struct ip6_hdr*)(packet->pkt + packet->ipOffset);
    moloch_session_id6(sessionId, ip6->ip6_src.s6_addr, 0, ip6->ip6_dst.s6_addr, 0);
}
/******************************************************************************/
SUPPRESS_ALIGNMENT
void icmpv6_pre_process(MolochSession_t *session, MolochPacket_t * const packet, int isNewSession)
{
    struct ip6_hdr      *ip6 = (struct ip6_hdr*)(packet->pkt + packet->ipOffset);

    if (isNewSession)
        moloch_session_add_protocol(session, "icmp");

    session->midSave = 1;

    int dir = (memcmp(session->addr1.s6_addr, ip6->ip6_src.s6_addr, 16) == 0 &&
               memcmp(session->addr2.s6_addr, ip6->ip6_dst.s6_addr, 16) == 0);

    packet->direction = (dir)?0:1;
    session->databytes[packet->direction] += (packet->pktlen -packet->payloadOffset);
}
/******************************************************************************/
void moloch_parser_init()
{
    moloch_packet_set_ip_cb(IPPROTO_ICMP, icmp_packet_enqueue);
    moloch_packet_set_ip_cb(IPPROTO_ICMPV6, icmpv6_packet_enqueue);

    icmpMProtocol = moloch_mprotocol_register("icmp",
                                              SESSION_ICMP,
                                              icmp_create_sessionid,
                                              icmp_pre_process,
                                              icmp_process,
                                              NULL);

    icmpv6MProtocol = moloch_mprotocol_register("icmpv6",
                                                SESSION_ICMP,
                                                icmpv6_create_sessionid,
                                                icmpv6_pre_process,
                                                icmp_process,
                                                NULL);

    icmpTypeField = moloch_field_define("general", "integer",
        "icmp.type", "ICMP Type", "icmp.type",
        "ICMP type field values",
        MOLOCH_FIELD_TYPE_INT_GHASH, 0,
        (char *)NULL);

    icmpCodeField = moloch_field_define("general", "integer",
        "icmp.code", "ICMP Code", "icmp.code",
        "ICMP code field values",
        MOLOCH_FIELD_TYPE_INT_GHASH, 0,
        (char *)NULL);

    icmpPayloadSrcIp = moloch_field_define("general", "ip",
        "icmp.payload.srcip", "ICMP payload src IP", "icmp.payload.srcip",
        "ICMP payload src IP values",
        MOLOCH_FIELD_TYPE_STR_GHASH, 0,
        (char *)NULL);

    icmpPayloadDstIp = moloch_field_define("general", "ip",
        "icmp.payload.dstip", "ICMP payload dst IP", "icmp.payload.dstip",
        "ICMP payload dst IP values",
        MOLOCH_FIELD_TYPE_STR_GHASH, 0,
        (char *)NULL);
}
