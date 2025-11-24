#pragma once

#include "Codec.h"
#include "RouteRegistry.h"

enum : std::uint16_t {
    MSG_HEARTBEAT = 1,
    MSG_ECHO = 2,
    MSG_JSON_ECHO = 3,
    MSG_PROTO_PING = 4,
};

namespace CoreRoutes {
    inline void Register(RouteRegistry& registry) {
        // 心跳
        registry.add(MSG_HEARTBEAT, "heartbeat", [](const ConnectionPtr& conn, const std::string& body) {

        });

        // echo
        registry.add(MSG_ECHO, "echo", [](const ConnectionPtr& conn, const std::string& body) {
            std::string resp = "echo" + body;
            LengthHeaderCodec::send(conn, MSG_ECHO, resp);
        });
    }
}  // namespace CoreRoutes
