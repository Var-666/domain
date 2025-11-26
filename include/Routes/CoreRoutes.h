#pragma once

#include "Codec.h"
#include "RouteRegistry.h"
#include <boost/asio/awaitable.hpp>

enum : std::uint16_t {
    MSG_HEARTBEAT = 1,
    MSG_ECHO = 2,
    MSG_JSON_ECHO = 3,
    MSG_PROTO_PING = 4,
};

namespace CoreRoutes {
    inline void Register(RouteRegistry& registry) {
        // 心跳
        registry.add(MSG_HEARTBEAT, "heartbeat", [](const ConnectionPtr& /*conn*/, std::string_view /*body*/) -> boost::asio::awaitable<void> {
            co_return;
        });

        // echo
        registry.add(MSG_ECHO, "echo", [](const ConnectionPtr& conn, std::string_view body) -> boost::asio::awaitable<void> {
            std::string resp = "echo";
            resp.append(body);
            LengthHeaderCodec::send(conn, MSG_ECHO, resp);
            co_return;
        });
    }
}  // namespace CoreRoutes
