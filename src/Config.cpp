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
        serverCfg_.ioThreadsCount = static_cast<std::size_t>(getIntField(L, "io_threads", serverCfg_.ioThreadsCount));
        serverCfg_.workerThreadsCount =
            static_cast<std::size_t>(getIntField(L, "worker_threads", serverCfg_.workerThreadsCount));
        serverCfg_.IdleTimeoutMs =
            static_cast<std::uint64_t>(getIntField(L, "idle_timeout_ms", serverCfg_.IdleTimeoutMs));
    } else {
        std::cerr << "[Config] 'config.server' not found or not a table, use defaults\n";
    }
    lua_pop(L, 1);  // pop server

    // ==== thread_pool ====
    lua_getfield(L, -1, "threadPool");  // stack: ..., config, thread_pool
    if (lua_istable(L, -1)) {
        serverCfg_.maxQueueSize = static_cast<std::size_t>(getIntField(L, "max_queue_size", serverCfg_.maxQueueSize));
    } else {
        std::cerr << "[Config] 'config.thread_pool' not found or not a table, use defaults\n";
    }
    lua_pop(L, 1);  // pop thread_pool

    // ==== limits ====
    lua_getfield(L, -1, "limits");  // stack: ..., config, limits
    if (lua_istable(L, -1)) {
        serverCfg_.maxInflight = static_cast<int>(getIntField(L, "max_inflight", serverCfg_.maxInflight));
        serverCfg_.maxSendBufferBytes =
            static_cast<std::size_t>(getIntField(L, "max_send_buffer_bytes", serverCfg_.maxSendBufferBytes));
    } else {
        std::cerr << "[Config] 'config.limits' not found or not a table, use defaults\n";
    }
    lua_pop(L, 1);  // pop limits

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
        logCfg_.flushIntervalMs =
            static_cast<std::uint64_t>(getIntField(L, "flushIntervalMs", logCfg_.flushIntervalMs));

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

    // 弹出 config
    lua_pop(L, 1);

    return true;
}
