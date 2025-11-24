#include "Config.h"

#include <iostream>
extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

Config& Config::Instance() {
    static Config instance;
    return instance;
}

// 辅助函数：从 table 中读取整数字段（带默认值）
static std::int64_t getIntField(lua_State* L, const char* key, std::int64_t defaultVal) {
    lua_getfield(L, -1, key);
    std::int64_t v = defaultVal;
    if (lua_isnumber(L, -1)) {
        v = static_cast<std::int64_t>(lua_tointeger(L, -1));
    }
    lua_pop(L, 1);
    return v;
}

static void parseStringSet(lua_State* L, const char* key, std::unordered_set<std::string>& out) {
    lua_getfield(L, -1, key);
    if (lua_istable(L, -1)) {
        lua_pushnil(L);
        while (lua_next(L, -2) != 0) {
            if (lua_isstring(L, -1)) {
                out.insert(lua_tostring(L, -1));
            }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
}

static bool getBoolField(lua_State* L, const char* key, bool def) {
    lua_getfield(L, -1, key);
    bool v = def;
    if (lua_isboolean(L, -1)) {
        v = lua_toboolean(L, -1) != 0;
    }
    lua_pop(L, 1);
    return v;
}

// 解析 uint16 列表到 unordered_set
static void parseUint16Set(lua_State* L, const char* key, std::unordered_set<std::uint16_t>& out) {
    lua_getfield(L, -1, key);
    if (lua_istable(L, -1)) {
        lua_pushnil(L);
        while (lua_next(L, -2) != 0) {
            if (lua_isinteger(L, -1)) {
                auto v = lua_tointeger(L, -1);
                if (v >= 0 && v <= 0xFFFF) {
                    out.insert(static_cast<std::uint16_t>(v));
                }
            }
            lua_pop(L, 1);  // pop value
        }
    }
    lua_pop(L, 1);  // pop list
}

bool Config::loadFromFile(const std::string& path) {
    lua_State* L = luaL_newstate();
    if (!L) {
        std::cerr << "[Config] Failed to create Lua state\n";
        return false;
    }

    luaL_openlibs(L);  // 打开标准库（math/table 等）

    // 加载并执行 Lua 文件
    if (luaL_dofile(L, path.c_str()) != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        std::cerr << "[Config] Failed to load " << path << ": " << (err ? err : "unknown error") << "\n";
        lua_close(L);
        return false;
    }

    bool ok = parseLuaConfig(L);
    lua_close(L);
    return ok;
}

const ServerConfig& Config::server() const { return serverCfg_; }

const LogConfig& Config::log() const { return logCfg_; }

const ThreadPoolConfig& Config::threadPool() const { return threadPoolCfg_; }

const Limits& Config::limits() const { return limitscfg_; }

const BackpressureConfig& Config::backpressure() const { return backpressureCfg_; }

const IpLimitConfig& Config::ipLimit() const { return ipLimitCfg_; }

const ErrorFrames& Config::errorFrames() const { return errorFrames_; }

const std::unordered_map<std::uint16_t, MsgLimitConfig>& Config::msgLimits() const { return msgLimitsCfg_; }

bool Config::parseLuaConfig(void* pL) {
    lua_State* L = static_cast<lua_State*>(pL);

    // 期望全局有一个 table: config
    lua_getglobal(L, "config");
    if (!lua_istable(L, -1)) {
        std::cerr << "[Config] 'config' not found or not a table\n";
        lua_pop(L, 1);
        return false;
    }

    // ==== server ====
    lua_getfield(L, -1, "server");  // stack: ..., config, server
    if (lua_istable(L, -1)) {
        serverCfg_.port = static_cast<unsigned short>(getIntField(L, "port", serverCfg_.port));
        serverCfg_.ioThreadsCount = static_cast<std::size_t>(getIntField(L, "ioThreadsCount", serverCfg_.ioThreadsCount));
        serverCfg_.IdleTimeoutMs = static_cast<std::uint64_t>(getIntField(L, "IdleTimeoutMs", serverCfg_.IdleTimeoutMs));
    } else {
        std::cerr << "[Config] 'config.server' not found or not a table, use defaults\n";
    }
    lua_pop(L, 1);  // pop server

    // ==== thread_pool ====（兼容 threadPool / thread_pool）
    lua_getfield(L, -1, "threadPool");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);                       // pop nil
        lua_getfield(L, -1, "thread_pool");  // 再尝试下划线命名
    }
    if (lua_istable(L, -1)) {
        threadPoolCfg_.workerThreadsCount = static_cast<std::size_t>(getIntField(L, "workerThreadsCount", threadPoolCfg_.workerThreadsCount));
        threadPoolCfg_.minThreads = static_cast<std::size_t>(getIntField(L, "minThreads", threadPoolCfg_.minThreads));
        threadPoolCfg_.maxThreads = static_cast<std::size_t>(getIntField(L, "maxThreads", threadPoolCfg_.maxThreads));
        threadPoolCfg_.maxQueueSize = static_cast<std::size_t>(getIntField(L, "maxQueueSize", threadPoolCfg_.maxQueueSize));

        threadPoolCfg_.autoTune = getBoolField(L, "autoTune", threadPoolCfg_.autoTune);

        threadPoolCfg_.highWatermark = static_cast<std::size_t>(getIntField(L, "highWatermark", threadPoolCfg_.highWatermark));
        threadPoolCfg_.lowWatermark = static_cast<std::size_t>(getIntField(L, "lowWatermark", threadPoolCfg_.lowWatermark));
        threadPoolCfg_.upThreshold = static_cast<int>(getIntField(L, "upThreshold", threadPoolCfg_.upThreshold));
        threadPoolCfg_.downThreshold = static_cast<int>(getIntField(L, "downThreshold", threadPoolCfg_.downThreshold));
    } else {
        std::cerr << "[Config] 'config.threadPool' not found or not a table, use defaults\n";
    }
    lua_pop(L, 1);  // pop thread_pool

    // ==== limits ====
    lua_getfield(L, -1, "limits");  // stack: ..., config, limits
    if (lua_istable(L, -1)) {
        limitscfg_.maxInflight = static_cast<std::size_t>(getIntField(L, "max_inflight", limitscfg_.maxInflight));
        limitscfg_.maxSendBufferBytes = static_cast<std::size_t>(getIntField(L, "max_send_buffer_bytes", limitscfg_.maxSendBufferBytes));
    } else {
        std::cerr << "[Config] 'config.limits' not found or not a table, use defaults\n";
    }
    lua_pop(L, 1);  // pop limits

    // ==== backpressure ====
    lua_getfield(L, -1, "backpressure");
    if (lua_istable(L, -1)) {
        backpressureCfg_.rejectLowPriority = getBoolField(L, "rejectLowPriority", backpressureCfg_.rejectLowPriority);
        backpressureCfg_.sendErrorFrame = getBoolField(L, "sendErrorFrame", backpressureCfg_.sendErrorFrame);

        lua_getfield(L, -1, "errorMsgType");
        if (lua_isinteger(L, -1)) {
            auto v = lua_tointeger(L, -1);
            if (v >= 0 && v <= 0xFFFF) {
                backpressureCfg_.errorMsgType = static_cast<std::uint16_t>(v);
            }
        }
        lua_pop(L, 1);  // pop errorMsgType

        lua_getfield(L, -1, "errorBody");
        if (lua_isstring(L, -1)) {
            backpressureCfg_.errorBody = lua_tostring(L, -1);
        }
        lua_pop(L, 1);  // pop errorBody

        parseUint16Set(L, "lowPriorityMsgTypes", backpressureCfg_.lowPriorityMsgTypes);
        parseUint16Set(L, "allowMsgTypes", backpressureCfg_.alwaysAllowMsgTypes);
    } else {
        // 可选配置，不存在则沿用默认
    }
    lua_pop(L, 1);  // pop backpressure

    // ==== Log ====
    lua_getfield(L, -1, "log");
    if (!lua_istable(L, -1)) {
        std::cerr << "[Config] 'config.log' not found or not a table, use defaults\n";
    } else {
        // level
        lua_getfield(L, -1, "level");
        if (lua_isstring(L, -1)) {
            logCfg_.level = lua_tostring(L, -1);
        }
        lua_pop(L, 1);

        logCfg_.asyncQueueSize = static_cast<std::size_t>(getIntField(L, "asyncQueueSize", logCfg_.asyncQueueSize));
        logCfg_.flushIntervalMs = static_cast<std::uint64_t>(getIntField(L, "flushIntervalMs", logCfg_.flushIntervalMs));

        // console
        lua_getfield(L, -1, "console");
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "enable");
            if (lua_isboolean(L, -1)) {
                logCfg_.consoleEnable = lua_toboolean(L, -1);
            }
            lua_pop(L, 1);
        }
        lua_pop(L, 1);  // pop console

        // file
        lua_getfield(L, -1, "file");
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "enable");
            if (lua_isboolean(L, -1)) {
                logCfg_.fileEnable = lua_toboolean(L, -1);
            }
            lua_pop(L, 1);

            lua_getfield(L, -1, "baseName");
            if (lua_isstring(L, -1)) {
                logCfg_.fileBaseName = lua_tostring(L, -1);
            }
            lua_pop(L, 1);

            logCfg_.fileMaxSizeMb = static_cast<std::size_t>(getIntField(L, "maxSizeMb", logCfg_.fileMaxSizeMb));
            logCfg_.fileMaxFiles = static_cast<std::size_t>(getIntField(L, "maxFiles", logCfg_.fileMaxFiles));
        }
        lua_pop(L, 1);  // pop file
    }
    lua_pop(L, 1);

    // ==== ipLimit ====
    lua_getfield(L, -1, "ipLimit");
    if (lua_istable(L, -1)) {
        ipLimitCfg_.maxConnPerIp = static_cast<std::size_t>(getIntField(L, "maxConnPerIp", ipLimitCfg_.maxConnPerIp));
        ipLimitCfg_.maxQpsPerIp = static_cast<std::size_t>(getIntField(L, "maxQpsPerIp", ipLimitCfg_.maxQpsPerIp));
        parseStringSet(L, "whitelist", ipLimitCfg_.whitelist);
    }
    lua_pop(L, 1);  // pop ipLimit

    // ==== errorFrames ====
    lua_getfield(L, -1, "errorFrames");
    if (lua_istable(L, -1)) {
        auto parseMsg = [&](const char* keyMsgType, const char* keyBody, std::uint16_t& outType, std::string& outBody) {
            lua_getfield(L, -1, keyMsgType);
            if (lua_isinteger(L, -1)) {
                auto v = lua_tointeger(L, -1);
                if (v >= 0 && v <= 0xFFFF) {
                    outType = static_cast<std::uint16_t>(v);
                }
            }
            lua_pop(L, 1);

            lua_getfield(L, -1, keyBody);
            if (lua_isstring(L, -1)) {
                outBody = lua_tostring(L, -1);
            }
            lua_pop(L, 1);
        };

        parseMsg("ipConnLimitMsgType", "ipConnLimitBody", errorFrames_.ipConnLimitMsgType, errorFrames_.ipConnLimitBody);
        parseMsg("ipQpsLimitMsgType", "ipQpsLimitBody", errorFrames_.ipQpsLimitMsgType, errorFrames_.ipQpsLimitBody);
        parseMsg("inflightLimitMsgType", "inflightLimitBody", errorFrames_.inflightLimitMsgType, errorFrames_.inflightLimitBody);
        parseMsg("msgRateLimitMsgType", "msgRateLimitBody", errorFrames_.msgRateLimitMsgType, errorFrames_.msgRateLimitBody);
        parseMsg("backpressureMsgType", "backpressureBody", errorFrames_.backpressureMsgType, errorFrames_.backpressureBody);
    }
    lua_pop(L, 1);  // pop errorFrames

    // ==== message_limits ====
    lua_getfield(L, -1, "messageLimits");  // stack: ..., config, message_limits
    if (lua_istable(L, -1)) {
        // 遍历 message_limits 表：key = msgType, value = table
        lua_pushnil(L);  // 初始 key
        while (lua_next(L, -2) != 0) {
            // stack: ..., message_limits, key, value
            if (!lua_isinteger(L, -2) || !lua_istable(L, -1)) {
                lua_pop(L, 1);  // pop value
                continue;
            }

            std::int64_t msgType = lua_tointeger(L, -2);
            MsgLimitConfig msgLimitsCfg;

            // enabled
            lua_getfield(L, -1, "enabled");
            if (lua_isboolean(L, -1)) {
                msgLimitsCfg.enabled = lua_toboolean(L, -1);
            }
            lua_pop(L, 1);

            msgLimitsCfg.maxQps = static_cast<int>(getIntField(L, "maxQps", msgLimitsCfg.maxQps));
            msgLimitsCfg.maxConcurrent = static_cast<int>(getIntField(L, "maxConcurrent", msgLimitsCfg.maxConcurrent));

            if (msgType >= 0 && msgType <= 0xFFFF) {
                msgLimitsCfg_[static_cast<std::uint16_t>(msgType)] = msgLimitsCfg;
            }

            lua_pop(L, 1);  // pop value, 保留 key 供下次 lua_next 使用
        }
    } else {
        std::cerr << "[Config] 'config.message_limits' not found or not a table, use defaults\n";
    }
    lua_pop(L, 1);  // pop message_limits

    // 弹出 config
    lua_pop(L, 1);

    return true;
}
