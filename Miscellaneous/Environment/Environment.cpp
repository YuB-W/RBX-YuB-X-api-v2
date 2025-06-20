#include "Environment.hpp"
#include "Execution.hpp"
#include "../../Dependencies/easywsclient/easywsclient.hpp"
#include <crypt/base64.h>

std::map<Closure*, Closure*> newcc_map;
std::map<Closure*, Closure*> hooks_map;

using easywsclient::WebSocket;


namespace Yubx {
    int gettenv(lua_State* LS) {
        //("gettenv");

        luaL_checktype(LS, 1, LUA_TTHREAD);
        lua_State* ls = (lua_State*)lua_topointer(LS, 1);
        LuaTable* tab = hvalue(luaA_toobject(ls, LUA_GLOBALSINDEX));

        sethvalue(LS, LS->top, tab);
        LS->top++;

        return 1;
    };

    int getgenv(lua_State* LS) {
        lua_pushvalue(LS, LUA_ENVIRONINDEX);
        return 1;
    }


    int getgc(lua_State* LS) {
        //("getgc");

        const bool includeTables = luaL_optboolean(LS, 1, false);

        lua_newtable(LS);
        lua_newtable(LS);

        lua_pushstring(LS, "kvs");
        lua_setfield(LS, -2, "__mode");
        lua_setmetatable(LS, -2);

        typedef struct {
            lua_State* luaThread;
            bool includeTables;
            int itemsFound;
        } GCOContext;

        auto GCContext = GCOContext{ LS, includeTables, 0 };

        const auto oldGCThreshold = LS->global->GCthreshold;
        LS->global->GCthreshold = SIZE_MAX;

        luaM_visitgco(LS, &GCContext, [](void* ctx, lua_Page* page, GCObject* gcObj) -> bool {
            const auto context = static_cast<GCOContext*>(ctx);
            const auto luaThread = context->luaThread;

            if (isdead(luaThread->global, gcObj))
                return false;

            const auto gcObjectType = gcObj->gch.tt;
            if (gcObjectType == LUA_TFUNCTION || gcObjectType == LUA_TTHREAD || gcObjectType == LUA_TUSERDATA ||
                gcObjectType == LUA_TLIGHTUSERDATA ||
                gcObjectType == LUA_TBUFFER || gcObjectType == LUA_TTABLE && context->includeTables) {
                luaThread->top->value.gc = gcObj;
                luaThread->top->tt = gcObjectType;
                incr_top(luaThread);

                const auto newTableIndex = context->itemsFound++;
                lua_rawseti(luaThread, -2, newTableIndex);
            }

            return false;
            });

        LS->global->GCthreshold = oldGCThreshold;

        return 1;
    };

    int loadstring(lua_State* LS) {
        //("loadstring");

        luaL_checktype(LS, 1, LUA_TSTRING);
        auto Source = lua_tostring(LS, 1);
        auto ChunkName = luaL_optstring(LS, 2, "Vexure");

        auto Bytecode = Execution->CompileScript(Source);

        if (Roblox::LuaVMLoad((int64_t)LS, &Bytecode, ChunkName, 0) != LUA_OK) {
            lua_pushnil(LS);
            lua_pushvalue(LS, -2);
            return 2;
        }

        Closure* Function = lua_toclosure(LS, -1);
        if (Function && Function->l.p)
        {
            Execution->ElevateProtoCapabilities(Function->l.p);
        }

        lua_setsafeenv(LS, LUA_GLOBALSINDEX, false);
        return 1;
    };

    namespace Miscellaneous {
        int identifyexecutor(lua_State* LS) {
            //("identifyexecutor");

            lua_pushstring(LS, "Vexure");
            lua_pushstring(LS, "1.0");
            return 2;
        };



        struct signal_t;

        struct signal_connection_t {
            char padding[16];
            int thread_idx; // 0x10
            int func_idx; //0x14
        };

        struct signal_data_t {
            uint64_t padding1;
            signal_t* root; //0x8
            uint64_t padding2[12];
            signal_connection_t* connection_data; //0x70
        };

        struct signal_t {
            uint64_t padding1[2];
            signal_t* next; //0x10
            uint64_t padding2;
            uint64_t state;
            uint64_t padding3;
            signal_data_t* signal_data; //0x30
        };

        struct connection_object {
            signal_t* signal;
            uint64_t state;
            uint64_t metatable;
            uint64_t root;
        };

        std::unordered_map<signal_t*, connection_object> connection_table;

        int connection_blank(lua_State* rl) {
            return 0;
        }

        int disable_connection(lua_State* rl) {
            auto connection = (connection_object*)lua_touserdata(rl, 1);
            if (connection->signal->state != 0)
                connection->state = connection->signal->state;

            connection->signal->state = 0;
            return 0;
        }

        int enable_connection(lua_State* rl) {
            auto connection = (connection_object*)lua_touserdata(rl, 1);
            connection->signal->state = connection->state;
            return 0;
        }

        int disconnect_connection(lua_State* rl) {
            auto connection = (connection_object*)lua_touserdata(rl, 1);
            auto root = (signal_t*)connection->root;
            if ((uint64_t)root == (uint64_t)connection) {
                luaL_error(rl, "Failed to disconnect.");
            }

            while (root->next && root->next != connection->signal) {
                root = root->next;
            }

            if (!root->next) {
                luaL_error(rl, "Already disconnected.");
            }

            root->next = root->next->next;
            connection->signal->state = 0;
            return 0;
        }

        int connection_index(lua_State* rl) {
            std::string key = std::string(lua_tolstring(rl, 2, nullptr));
            auto connection = (connection_object*)lua_touserdata(rl, 1);
            uintptr_t connection2 = *(uintptr_t*)lua_touserdata(rl, 1);

            if (key == "Enabled" || key == "enabled") {
                lua_pushboolean(rl, !(connection->signal->state == 0));
                return 1;
            }

            if (key == "Function" || key == "function" || key == "Fire" || key == "fire" || key == "Defer" || key == "defer") {
                int signal_data = *(int*)&connection->signal->signal_data;
                if (signal_data && *(int*)&connection->signal->signal_data->connection_data) {
                    int index = connection->signal->signal_data->connection_data->func_idx;
                    lua_rawgeti(rl, LUA_REGISTRYINDEX, index);

                    if (lua_type(rl, -1) != LUA_TFUNCTION)
                        lua_pushcclosure(rl, connection_blank, 0, 0, 0);

                    return 1;
                }

                lua_pushcclosure(rl, connection_blank, 0, 0, 0);
                return 1;
            }

            if (key == "LuaConnection") {
                int signal_data = *(int*)&connection->signal->signal_data;
                if (signal_data && *(int*)&connection->signal->signal_data->connection_data) {
                    int index = connection->signal->signal_data->connection_data->func_idx;

                    lua_rawgeti(rl, LUA_REGISTRYINDEX, index);
                    auto func_tval = (TValue*)luaA_toobject(rl, -1);
                    auto cl = (Closure*)func_tval->value.gc;
                    bool lua = !cl->isC;

                    lua_pop(rl, 1);
                    lua_pushboolean(rl, lua);
                    return 1;
                }

                lua_pushboolean(rl, false);
                return 1;
            }

            if (key == "ForeignState") {
                int signal_data = *(int*)&connection->signal->signal_data;
                if (signal_data && *(int*)&connection->signal->signal_data->connection_data) {
                    int index = connection->signal->signal_data->connection_data->func_idx;

                    lua_rawgeti(rl, LUA_REGISTRYINDEX, index);
                    auto func_tval = (TValue*)luaA_toobject(rl, -1);
                    auto cl = (Closure*)func_tval->value.gc;
                    bool c = cl->isC;

                    lua_pop(rl, 1);
                    lua_pushboolean(rl, c);
                    return 1;
                }

                lua_pushboolean(rl, false);
                return 1;
            }

            if (key == "Disconnect" || key == "disconnect") {
                lua_pushcclosure(rl, disconnect_connection, 0, 0, 0);
                return 1;
            }

            if (key == "Disable" || key == "disable") {
                lua_pushcclosure(rl, disable_connection, 0, 0, 0);
                return 1;
            }

            if (key == "Enable" || key == "enable") {
                lua_pushcclosure(rl, enable_connection, 0, 0, 0);
                return 1;
            }

            if (key == "Thread") {
                int signal_data = *(int*)&connection->signal->signal_data;
                if (signal_data && *(int*)&connection->signal->signal_data->connection_data) {
                    int index = connection->signal->signal_data->connection_data->thread_idx;
                    lua_rawgeti(rl, LUA_REGISTRYINDEX, index);

                    if (lua_type(rl, -1) != LUA_TTHREAD)
                        lua_pushthread(rl);

                    return 1;
                }
            }

            luaL_error(rl, "Invalid idx");
            return 0;
        }

        int getconnections_handler(lua_State* rl) {
            auto signal = *(signal_t**)lua_touserdata(rl, 1);
            signal = signal->next;

            lua_createtable(rl, 0, 0);
            auto signal_root = signal;
            int index = 1;

            while (signal) {
                int func_idx = signal->signal_data->connection_data->func_idx;

                if (!connection_table.count(signal)) {
                    connection_object new_connection;
                    new_connection.signal = signal;
                    new_connection.root = (uint64_t)signal_root;
                    new_connection.state = signal->state;
                    connection_table[signal] = new_connection;
                }

                auto connection = (connection_object*)lua_newuserdata(rl, sizeof(connection_object), 0);
                *connection = connection_table[signal];

                lua_createtable(rl, 0, 0);
                lua_pushcclosure(rl, connection_index, 0, 0, 0);
                lua_setfield(rl, -2, "__index");

                lua_pushstring(rl, "RealConnection");
                lua_setfield(rl, -2, "__type");
                lua_setmetatable(rl, -2);

                lua_rawseti(rl, -2, index++);
                signal = signal->next;
            }

            return 1;
        }

        int getconnections(lua_State* L)
        {

            luaL_checktype(L, 1, LUA_TUSERDATA);

            lua_getfield(L, 1, "Connect");
            if (!lua_isfunction(L, -1)) {
                luaL_error(L, "Signal does not have 'Connect' method");
            }

            lua_pushvalue(L, 1);
            lua_pushcfunction(L, connection_blank, nullptr);

            if (lua_pcall(L, 2, 1, 0) != LUA_OK) {
                const char* err = lua_tostring(L, -1);
                luaL_error(L, "Error calling 'Connect': %s", err ? err : "unknown error");
            }

            if (!lua_istable(L, -1) && !lua_isuserdata(L, -1)) {
                luaL_error(L, "Connect did not return a valid connection object");
            }

            lua_pushvalue(L, -1);

            lua_pushcfunction(L, getconnections_handler, nullptr);
            lua_pushvalue(L, -2);

            if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
                const char* err = lua_tostring(L, -1);
                luaL_error(L, "Error calling 'getconnections': %s", err ? err : "unknown error");
            }

            lua_getfield(L, -3, "Disconnect");
            if (lua_isfunction(L, -1)) {
                lua_pushvalue(L, -4);
                if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
                    const char* err = lua_tostring(L, -1);
                    luaL_error(L, "Error calling 'Disconnect': %s", err ? err : "unknown error");
                }
            }
            else {
                lua_pop(L, 1);
            }

            lua_replace(L, 1);
            lua_settop(L, 1);
            return 1;
        }


        int getexecutorname(lua_State* LS) {
            //("getexecutorname");

            lua_pushstring(LS, "Vexure");
            return 1;
        };

        std::mutex Tpmutext;
        int queue_on_teleport(lua_State* LS) {
            //("queueonteleport");
            const auto script = luaL_checkstring(LS, 1);

            std::unique_lock<std::mutex> locker{ Tpmutext };

            //Environment->QueueTeleportScripts.push_back(std::string(script));

            return 0;
        };

        int setfps(lua_State* LS) {
            //("setfps");

            luaL_checktype(LS, 1, LUA_TNUMBER);

            double fps = lua_tonumber(LS, 1);

            *reinterpret_cast<int32_t*>(Offsets::TaskSchedulerTargetFps) = fps;

            return 0;
        };

        int getfps(lua_State* LS) {
            //("getfps");

            std::optional<double> fps = *reinterpret_cast<int32_t*>(Offsets::TaskSchedulerTargetFps);

            if (fps.has_value()) {
                lua_pushnumber(LS, fps.value());
            }
            else {
                lua_pushnumber(LS, 0);
            }

            return 1;
        };


        int lz4compress(lua_State* LS) {
            //("lz4compress");
            luaL_checktype(LS, 1, LUA_TSTRING);

            const char* data = lua_tostring(LS, 1);
            int nMaxCompressedSize = LZ4_compressBound(strlen(data));
            char* out_buffer = new char[nMaxCompressedSize];

            LZ4_compress(data, out_buffer, strlen(data));
            lua_pushlstring(LS, out_buffer, nMaxCompressedSize);
            return 1;
        };

        int lz4decompress(lua_State* LS) {
            //("lz4decompress");

            luaL_checktype(LS, 1, LUA_TSTRING);
            luaL_checktype(LS, 2, LUA_TNUMBER);

            const char* data = lua_tostring(LS, 1);
            int data_size = lua_tointeger(LS, 2);

            char* pszUnCompressedFile = new char[data_size];

            LZ4_uncompress(data, pszUnCompressedFile, data_size);
            lua_pushlstring(LS, pszUnCompressedFile, data_size);
            return 1;
        };

        int messagebox(lua_State* LS) {
            //("messagebox");
            const auto text = luaL_checkstring(LS, 1);
            const auto caption = luaL_checkstring(LS, 2);
            const auto type = luaL_checkinteger(LS, 3);

            return HelpFuncs::YieldExecution(LS,
                [text, caption, type]() -> auto {

                    const int lMessageboxReturn = MessageBoxA(nullptr, text, caption, type);

                    return [lMessageboxReturn](lua_State* L) -> int {
                        lua_pushinteger(L, lMessageboxReturn);
                        return 1;
                        };
                }
            );
        };

        int setclipboard(lua_State* LS) {
            //("setclipboard");
            luaL_checktype(LS, 1, LUA_TSTRING);

            std::string content = lua_tostring(LS, 1);

            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, content.size() + 1);
            memcpy(GlobalLock(hMem), content.data(), content.size());
            GlobalUnlock(hMem);
            OpenClipboard(0);
            EmptyClipboard();
            SetClipboardData(CF_TEXT, hMem);
            CloseClipboard();
            return 0;
        };

    };
    namespace Instances {

        std::vector<std::tuple<uintptr_t, std::string, bool>> script_able_cache;
        std::vector<std::pair<std::string, bool>> default_property_states;

        int getCachedScriptableProperty(uintptr_t instance, std::string property) {
            for (auto& cacheData : script_able_cache) {
                uintptr_t instanceAddress = std::get<0>(cacheData);
                std::string instanceProperty = std::get<1>(cacheData);

                if (instanceAddress == instance && instanceProperty == property) {
                    return std::get<2>(cacheData);
                }
            }

            return -1;
        };

        int getCachedDefultScriptableProperty(std::string property) {
            for (auto& cacheData : default_property_states) {
                if (cacheData.first == property) {
                    return cacheData.second;
                }
            }

            return -1;
        };

        bool findAndUpdateScriptAbleCache(uintptr_t instance, std::string property, bool state) {
            for (auto& cacheData : script_able_cache) {
                uintptr_t instanceAddress = std::get<0>(cacheData);
                std::string instanceProperty = std::get<1>(cacheData);

                if (instanceAddress == instance && instanceProperty == property) {
                    std::get<2>(cacheData) = state;
                    return true;
                }
            }

            return false;
        }

        void addDefaultPropertyState(std::string property, bool state) {
            bool hasDefault = false;

            for (auto& cacheData : default_property_states) {
                if (cacheData.first == property) {
                    hasDefault = true;
                    break;
                }
            }

            if (!hasDefault) {
                default_property_states.push_back({ property, state });
            }
        };

        namespace InstancesHelper {


            std::unordered_map<std::string, uintptr_t> GetInstanceProperties(const uintptr_t rawInstance) {
                auto foundProperties = std::unordered_map<std::string, uintptr_t>();

                const auto classDescriptor = *reinterpret_cast<uintptr_t*>(
                    rawInstance + Offsets::DataModel::ClassDescriptor);
                const auto allPropertiesStart = *reinterpret_cast<uintptr_t*>(
                    classDescriptor + Offsets::DataModel::PropertiesStart);
                const auto allPropertiesEnd = *reinterpret_cast<uintptr_t*>(
                    classDescriptor + Offsets::DataModel::PropertiesEnd);

                for (uintptr_t currentPropertyAddress = allPropertiesStart; currentPropertyAddress != allPropertiesEnd;
                    currentPropertyAddress += 0x8) {
                    const auto currentProperty = *reinterpret_cast<uintptr_t*>(currentPropertyAddress);
                    const auto propertyNameAddress = *reinterpret_cast<uintptr_t*>(
                        currentProperty + 0x8);
                    if (propertyNameAddress == 0)
                        continue;
                    const auto propertyName = *reinterpret_cast<std::string*>(propertyNameAddress);
                    foundProperties[propertyName] = currentProperty;
                }

                return foundProperties;
            }
        };


        int getcallbackvalue(lua_State* LS) {
            //("getcallbackvalue");

            HelpFuncs::IsInstance(LS, 1);
            luaL_checktype(LS, 2, LUA_TSTRING);

            const auto rawInstance = reinterpret_cast<uintptr_t>(lua_torawuserdata(LS, 1));
            int Atom;
            lua_tostringatom(LS, 2, &Atom);

            auto propertyName = reinterpret_cast<uintptr_t*>(Offsets::KTable)[Atom];
            if (propertyName == 0 || IsBadReadPtr(reinterpret_cast<void*>(propertyName), 0x10))
                luaL_argerrorL(LS, 2, "Invalid property!");

            const auto instanceClassDescriptor = *reinterpret_cast<uintptr_t*>(
                rawInstance + Offsets::DataModel::ClassDescriptor);
            const auto Property = Roblox::GetProperty(
                instanceClassDescriptor + Offsets::DataModel::PropDescriptor,
                &propertyName);
            if (Property == 0 || IsBadReadPtr(reinterpret_cast<void*>(Property), 0x10))
                luaL_argerrorL(LS, 2, "Invalid property!");

            const auto callbackStructureStart = rawInstance + *reinterpret_cast<uintptr_t*>(
                *reinterpret_cast<uintptr_t*>(Property) + 0x80);
            const auto hasCallback = *reinterpret_cast<uintptr_t*>(callbackStructureStart + 0x38);
            if (hasCallback == 0) {
                lua_pushnil(LS);
                return 1;
            }

            const auto callbackStructure = *reinterpret_cast<uintptr_t*>(callbackStructureStart + 0x18);
            if (callbackStructure == 0) {
                lua_pushnil(LS);
                return 1;
            }

            const auto ObjectRefs = *reinterpret_cast<uintptr_t*>(callbackStructure + 0x38);
            if (ObjectRefs == 0) {
                lua_pushnil(LS);
                return 1;
            }

            const auto ObjectRef = *reinterpret_cast<uintptr_t*>(ObjectRefs + 0x28);
            const auto RefId = *reinterpret_cast<int*>(ObjectRef + 0x14);

            lua_getref(LS, RefId);
            return 1;
            return 0;
        }

        int getthreadidentity(lua_State* LS) {
            //("getthreadidentity");

            lua_pushnumber(LS, LS->userdata->Identity);

            return 1;
        };

        int setthreadidentity(lua_State* LS)
        {
            //("setthreadidentity");

            luaL_checktype(LS, 1, LUA_TNUMBER);

            HelpFuncs::SetNewIdentity(LS, lua_tonumber(LS, 1));

            return 0;
        };


        int getinstances(lua_State* LS) {
            //("getinstances");

            lua_pop(LS, lua_gettop(LS));

            HelpFuncs::GetEveryInstance(LS);

            if (!lua_istable(LS, -1)) { lua_pop(LS, 1); lua_pushnil(LS); return 1; };

            lua_newtable(LS);

            int index = 0;

            lua_pushnil(LS);
            while (lua_next(LS, -3) != 0) {

                if (!lua_isnil(LS, -1)) {
                    lua_getglobal(LS, "typeof");
                    lua_pushvalue(LS, -2);
                    lua_pcall(LS, 1, 1, 0);

                    std::string type = lua_tostring(LS, -1);
                    lua_pop(LS, 1);

                    if (type == "Instance") {
                        lua_pushinteger(LS, ++index);

                        lua_pushvalue(LS, -2);
                        lua_settable(LS, -5);
                    }
                }

                lua_pop(LS, 1);
            }

            lua_remove(LS, -2);

            return 1;
        };

        int getnilinstances(lua_State* LS)
        {
            //("getnilinstances");

            lua_pop(LS, lua_gettop(LS));

            HelpFuncs::GetEveryInstance(LS);

            if (!lua_istable(LS, -1)) { lua_pop(LS, 1); lua_pushnil(LS); return 1; };

            lua_newtable(LS);

            int index = 0;

            lua_pushnil(LS);
            while (lua_next(LS, -3) != 0) {

                if (!lua_isnil(LS, -1)) {
                    lua_getglobal(LS, "typeof");
                    lua_pushvalue(LS, -2);
                    lua_pcall(LS, 1, 1, 0);

                    std::string type = lua_tostring(LS, -1);
                    lua_pop(LS, 1);

                    if (type == "Instance") {
                        lua_getfield(LS, -1, "Parent");
                        int parentType = lua_type(LS, -1);
                        lua_pop(LS, 1);

                        if (parentType == LUA_TNIL) {
                            lua_pushinteger(LS, ++index);

                            lua_pushvalue(LS, -2);
                            lua_settable(LS, -5);
                        }
                    }
                }

                lua_pop(LS, 1);
            }

            lua_remove(LS, -2);

            return 1;
        };

        int gethui(lua_State* LS)
        {
            //("gethui");

            lua_getglobal(LS, "__hiddeninterface");

            return 1;
        };

        int fireclickdetector(lua_State* LS) { // TO ADD: Get local player with offsets
            //("fireclickdetector");

            luaL_checktype(LS, 1, LUA_TUSERDATA);

            std::string ToFire = lua_isstring(LS, 3) ? lua_tostring(LS, 3) : "";

            HelpFuncs::IsInstance(LS, 1);

            uintptr_t ClickDetector = *reinterpret_cast<uintptr_t*>(lua_touserdata(LS, 1));
            if (!ClickDetector)
                luaL_argerror(LS, 1, "Invalid clickdetector");

            float distance = 0.0;

            if (lua_isnumber(LS, 2))
                distance = (float)lua_tonumber(LS, 2);

            lua_getglobal(LS, "game");
            lua_getfield(LS, -1, "GetService");
            lua_insert(LS, -2);
            lua_pushstring(LS, "Players");
            lua_pcall(LS, 2, 1, 0);

            lua_getfield(LS, -1, "LocalPlayer");

            uintptr_t LocalPlayer = *reinterpret_cast<uintptr_t*>(lua_touserdata(LS, -1));
            if (!LocalPlayer)
                luaL_error(LS, "Error getting localplayer!");


            Roblox::FireMouseClick(ClickDetector, distance, LocalPlayer);
            return 0;
        }

        int firetouchinterest(lua_State* LS) {
            //("firetouchinterest");

            luaL_checktype(LS, 1, LUA_TUSERDATA);
            luaL_checktype(LS, 2, LUA_TUSERDATA);
            //luaL_checktype(LS, 3, LUA_TNUMBER);

            int Toggle = lua_tonumber(LS, 3);

            uintptr_t BasePart = *reinterpret_cast<uintptr_t*>(lua_touserdata(LS, 1));
            if (!BasePart)
                luaL_argerror(LS, 1, "Invalid basepart");

            uintptr_t BasePartTouch = *reinterpret_cast<uintptr_t*>(lua_touserdata(LS, 2));
            if (!BasePartTouch)
                luaL_argerror(LS, 2, "Invalid basepart");

            uintptr_t Touch1 = *reinterpret_cast<uintptr_t*>(BasePart + Offsets::DataModel::PrimitiveTouch);
            if (!Touch1)
                luaL_argerror(LS, 1, "Error getting primitive touch");

            uintptr_t Touch2 = *reinterpret_cast<uintptr_t*>(BasePartTouch + Offsets::DataModel::PrimitiveTouch);
            if (!Touch2)
                luaL_argerror(LS, 2, "Error getting primitive touch");

            uintptr_t Overlap = *reinterpret_cast<uintptr_t*>(Touch1 + Offsets::DataModel::Overlap);
            if (!Overlap)
                luaL_argerror(LS, 1, "Error getting overlap");

            Roblox::FireTouchInterest(Overlap, Touch1, Touch2, Toggle, true);
        };

        int fireproximityprompt(lua_State* LS) {
            //("fireproximityprompt");

            luaL_checktype(LS, 1, LUA_TUSERDATA);

            uintptr_t ProximityPrompt = *(uintptr_t*)(lua_topointer(LS, 1));
            if (!ProximityPrompt)
                luaL_argerror(LS, 1, "Invalid proximity prompt!");

            Roblox::FireProximityPrompt(ProximityPrompt);
            return 0;
        };
    };
    namespace Crypt {

        namespace HelpFunctions {

            template<typename T>
            static std::string hash_with_algo(const std::string& Input)
            {
                T Hash;
                std::string Digest;

                CryptoPP::StringSource SS(Input, true,
                    new CryptoPP::HashFilter(Hash,
                        new CryptoPP::HexEncoder(
                            new CryptoPP::StringSink(Digest), false
                        )));

                return Digest;
            }
            std::string b64encode(const std::string& stringToEncode) {
                std::string base64EncodedString;
                CryptoPP::Base64Encoder encoder{ new CryptoPP::StringSink(base64EncodedString), false };
                encoder.Put((byte*)stringToEncode.c_str(), stringToEncode.length());
                encoder.MessageEnd();

                return base64EncodedString;
            }

            std::string b64decode(const std::string& stringToDecode) {
                std::string base64DecodedString;
                CryptoPP::Base64Decoder decoder{ new CryptoPP::StringSink(base64DecodedString) };
                decoder.Put((byte*)stringToDecode.c_str(), stringToDecode.length());
                decoder.MessageEnd();

                return base64DecodedString;
            }

            std::string RamdonString(int len) {
                static const char* chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890";
                std::string str;
                str.reserve(len);

                for (int i = 0; i < len; ++i) {
                    str += chars[rand() % (strlen(chars) - 1)];
                }

                return str;
            }
        }

        enum HashModes
        {
            //MD5
            MD5,

            //SHA1
            SHA1,

            //SHA2
            SHA224,
            SHA256,
            SHA384,
            SHA512,

            //SHA3
            SHA3_224,
            SHA3_256,
            SHA3_384,
            SHA3_512,
        };

        std::map<std::string, HashModes> HashTranslationMap = {
            //MD5
            { "md5", MD5 },

            //SHA1
            { "sha1", SHA1 },

            //SHA2
            { "sha224", SHA224 },
            { "sha256", SHA256 },
            { "sha384", SHA384 },
            { "sha512", SHA512 },

            //SHA3
            { "sha3-224", SHA3_224 },
            { "sha3_224", SHA3_224 },
            { "sha3-256", SHA3_256 },
            { "sha3_256", SHA3_256 },
            { "sha3-384", SHA3_384 },
            { "sha3_384", SHA3_384 },
            { "sha3-512", SHA3_512 },
            { "sha3_512", SHA3_512 },
        };

        int base64encode(lua_State* L) {
            luaL_checktype(L, 1, LUA_TSTRING);

            const char* str = luaL_checklstring(L, 1, nullptr);
            bool urlEncoding = lua_isboolean(L, 2) ? lua_toboolean(L, 2) : false;

            std::string encoded = base64_encode(str, urlEncoding);
            lua_pushlstring(L, encoded.data(), encoded.size());
            return 1;
        }

        int base64decode(lua_State* L) {
            luaL_checktype(L, 1, LUA_TSTRING);

            const char* str = luaL_checklstring(L, 1, nullptr);
            bool urlEncoding = lua_isboolean(L, 2) ? lua_toboolean(L, 2) : false;

            std::string decoded = base64_decode(str, urlEncoding);
            lua_pushlstring(L, decoded.data(), decoded.size());
            return 1;

        }

        int generatebytes(lua_State* L) { // <- Sun's Dick Penetrated this function which caused it to not pass on sunc
            luaL_checktype(L, 1, LUA_TNUMBER);

            size_t length = luaL_checkinteger(L, 1);

            std::string generatedString = HelpFunctions::RamdonString(length);
            std::string encodedBytes = base64_encode(generatedString, false);

            lua_pushlstring(L, encodedBytes.data(), encodedBytes.size());
            return 1;
        }

        int generatekey(lua_State* L) {
            std::string generatedString = HelpFunctions::RamdonString(32);
            std::string encodedKey = base64_encode(generatedString, false);

            lua_pushlstring(L, encodedKey.data(), encodedKey.size());
            return 1;

        }

        int hash(lua_State* L) {
            //("crypt.hash");

            std::string algo = luaL_checklstring(L, 2, NULL);
            std::string data = luaL_checklstring(L, 1, NULL);

            std::transform(algo.begin(), algo.end(), algo.begin(), tolower);

            if (!HashTranslationMap.count(algo))
            {
                luaL_argerror(L, 1, "non-existant hash algorithm");
                return 0;
            }

            const auto ralgo = HashTranslationMap[algo];

            std::string hash;

            if (ralgo == MD5) {
                hash = HelpFunctions::hash_with_algo<CryptoPP::MD5>(data);
            }
            else if (ralgo == SHA1) {
                hash = HelpFunctions::hash_with_algo<CryptoPP::SHA1>(data);
            }
            else if (ralgo == SHA224) {
                hash = HelpFunctions::hash_with_algo<CryptoPP::SHA224>(data);
            }
            else if (ralgo == SHA256) {
                hash = HelpFunctions::hash_with_algo<CryptoPP::SHA256>(data);
            }
            else if (ralgo == SHA384) {
                hash = HelpFunctions::hash_with_algo<CryptoPP::SHA384>(data);
            }
            else if (ralgo == SHA512) {
                hash = HelpFunctions::hash_with_algo<CryptoPP::SHA512>(data);
            }
            else if (ralgo == SHA3_224) {
                hash = HelpFunctions::hash_with_algo<CryptoPP::SHA3_224>(data);
            }
            else if (ralgo == SHA3_256) {
                hash = HelpFunctions::hash_with_algo<CryptoPP::SHA3_256>(data);
            }
            else if (ralgo == SHA3_384) {
                hash = HelpFunctions::hash_with_algo<CryptoPP::SHA3_384>(data);
            }
            else if (ralgo == SHA3_512) {
                hash = HelpFunctions::hash_with_algo<CryptoPP::SHA3_512>(data);
            }
            else {
                luaL_argerror(L, 1, "non-existant hash algorithm");
                return 0;
            }

            lua_pushlstring(L, hash.c_str(), hash.size());

            return 1;
        }
        using ModePair = std::pair<std::unique_ptr<CryptoPP::CipherModeBase>, std::unique_ptr<CryptoPP::CipherModeBase> >;

        std::optional<ModePair> getEncryptionDecryptionMode(const std::string& modeName) {
            if (modeName == "cbc") {
                //("Mode:cbc");
                return ModePair{
                    std::make_unique<CryptoPP::CBC_Mode<CryptoPP::AES>::Encryption>(),
                    std::make_unique<CryptoPP::CBC_Mode<CryptoPP::AES>::Decryption>()
                };
            }
            else if (modeName == "cfb") {
                return ModePair{
                    std::make_unique<CryptoPP::CFB_Mode<CryptoPP::AES>::Encryption>(),
                    std::make_unique<CryptoPP::CFB_Mode<CryptoPP::AES>::Decryption>()
                };
            }
            else if (modeName == "ofb") {
                return ModePair{
                    std::make_unique<CryptoPP::OFB_Mode<CryptoPP::AES>::Encryption>(),
                    std::make_unique<CryptoPP::OFB_Mode<CryptoPP::AES>::Decryption>()
                };
            }
            else if (modeName == "ctr") {
                return ModePair{
                    std::make_unique<CryptoPP::CTR_Mode<CryptoPP::AES>::Encryption>(),
                    std::make_unique<CryptoPP::CTR_Mode<CryptoPP::AES>::Decryption>()
                };
            }
            else if (modeName == "ecb") {
                return ModePair{
                    std::make_unique<CryptoPP::ECB_Mode<CryptoPP::AES>::Encryption>(),
                    std::make_unique<CryptoPP::ECB_Mode<CryptoPP::AES>::Decryption>()
                };
            }
            else {
                return std::nullopt;
            }
        }

        int encrypt(lua_State* L) {
            //("crypt.encrypt");

            luaL_checktype(L, 1, LUA_TSTRING);
            luaL_checktype(L, 2, LUA_TSTRING);

            const auto rawDataString = lua_tostring(L, 1);
            lua_pushstring(L, HelpFunctions::b64encode(rawDataString).c_str());
            lua_pushstring(L, "");

            return 2;
        }

        int decrypt(lua_State* L) {
            //("crypt.decrypt");

            luaL_checktype(L, 1, LUA_TSTRING);
            luaL_checktype(L, 2, LUA_TSTRING);
            luaL_checktype(L, 3, LUA_TSTRING);
            luaL_checktype(L, 4, LUA_TSTRING);

            const auto rawDataString = lua_tostring(L, 1);
            lua_pushstring(L, HelpFunctions::b64decode(rawDataString).c_str());
            return 1;
        }
    };
    namespace Script {



        int getscriptbytecode(lua_State* LS) {
            //("getscriptbytecode");

            luaL_checktype(LS, 1, LUA_TUSERDATA);

            std::string Bytecode = HelpFuncs::RequestBytecode(*(uintptr_t*)lua_topointer(LS, 1), true);

            if (Bytecode != "Nil")
            {
                lua_pushlstring(LS, Bytecode.data(), Bytecode.size());
            }
            else
                lua_pushnil(LS);

            return 1;

        };


        int getscriptclosure(lua_State* LS) {
            //("getscriptclosure");

            luaL_checktype(LS, 1, LUA_TUSERDATA);

            if (!HelpFuncs::IsClassName(LS, 1, "ModuleScript") && !HelpFuncs::IsClassName(LS, 1, "LocalScript") && !HelpFuncs::IsClassName(LS, 1, "Script"))
            {
                luaL_argerror(LS, 1, "Expected a ModuleScript or a LocalScript");
                return 0;
            }

            std::string scriptCode = HelpFuncs::RequestBytecode(*(uintptr_t*)lua_topointer(LS, 1), false);
            if (scriptCode == "Nil")
            {
                lua_pushnil(LS);
                return 1;
            }

            lua_State* L2 = lua_newthread(LS);
            luaL_sandboxthread(L2);

            HelpFuncs::SetNewIdentity(L2, 8);

            lua_pushvalue(LS, 1);
            lua_xmove(LS, L2, 1);
            lua_setglobal(L2, "script");

            int result = Roblox::LuaVMLoad((int64_t)L2, &scriptCode, "", 0);
            if (result == LUA_OK) {
                Closure* cl = clvalue(luaA_toobject(L2, -1));

                if (cl) {
                    Proto* p = cl->l.p;
                    if (p) {
                        Execution->ElevateProtoCapabilities(p);
                    }
                }

                lua_pop(L2, lua_gettop(L2));
                lua_pop(LS, lua_gettop(LS));

                setclvalue(LS, LS->top, cl);
                incr_top(LS);

                return 1;
            }
            else
            {
                luaL_error(LS, "Error loading the script bytecode!");
            }

            lua_pushnil(LS);
            return 1;
        }
    };

    namespace Cache {
        int invalidate(lua_State* LS) {
            //("invalidate");

            luaL_checktype(LS, 1, LUA_TUSERDATA);

            HelpFuncs::IsInstance(LS, 1);

            const auto Instance = *static_cast<void**>(lua_touserdata(LS, 1));

            lua_pushlightuserdata(LS, (void*)Roblox::PushInstance);
            lua_gettable(LS, LUA_REGISTRYINDEX);

            lua_pushlightuserdata(LS, reinterpret_cast<void*>(Instance));
            lua_pushnil(LS);
            lua_settable(LS, -3);

            return 0;
        };

        int replace(lua_State* LS) {
            //("replace");

            luaL_checktype(LS, 1, LUA_TUSERDATA);
            luaL_checktype(LS, 2, LUA_TUSERDATA);

            HelpFuncs::IsInstance(LS, 1);
            HelpFuncs::IsInstance(LS, 2);

            const auto Instance = *reinterpret_cast<uintptr_t*>(lua_touserdata(LS, 1));

            lua_pushlightuserdata(LS, (void*)Roblox::PushInstance);
            lua_gettable(LS, LUA_REGISTRYINDEX);

            lua_pushlightuserdata(LS, (void*)Instance);
            lua_pushvalue(LS, 2);
            lua_settable(LS, -3);
            return 0;
        };

        int iscached(lua_State* LS) {
            //("iscached");

            luaL_checktype(LS, 1, LUA_TUSERDATA);

            HelpFuncs::IsInstance(LS, 1);
            const auto Instance = *static_cast<void**>(lua_touserdata(LS, 1));

            lua_pushlightuserdata(LS, (void*)Roblox::PushInstance);
            lua_gettable(LS, LUA_REGISTRYINDEX);

            lua_pushlightuserdata(LS, Instance);
            lua_gettable(LS, -2);

            lua_pushboolean(LS, !lua_isnil(LS, -1));
            return 1;
        };

        int cloneref(lua_State* LS) {
            //("cloneref");

            luaL_checktype(LS, 1, LUA_TUSERDATA);

            HelpFuncs::IsInstance(LS, 1);

            const auto OldUserdata = lua_touserdata(LS, 1);

            const auto NewUserdata = *reinterpret_cast<uintptr_t*>(OldUserdata);

            lua_pushlightuserdata(LS, (void*)Roblox::PushInstance);

            lua_rawget(LS, -10000);
            lua_pushlightuserdata(LS, reinterpret_cast<void*>(NewUserdata));
            lua_rawget(LS, -2);

            lua_pushlightuserdata(LS, reinterpret_cast<void*>(NewUserdata));
            lua_pushnil(LS);
            lua_rawset(LS, -4);

            Roblox::PushInstance(LS, (uintptr_t)OldUserdata);

            lua_pushlightuserdata(LS, reinterpret_cast<void*>(NewUserdata));
            lua_pushvalue(LS, -3);
            lua_rawset(LS, -5);

            return 1;
        };

        int compareinstances(lua_State* LS) {
            //("compareinstances");

            luaL_checktype(LS, 1, LUA_TUSERDATA);
            luaL_checktype(LS, 2, LUA_TUSERDATA);

            HelpFuncs::IsInstance(LS, 1);
            HelpFuncs::IsInstance(LS, 2);

            uintptr_t First = *reinterpret_cast<uintptr_t*>(lua_touserdata(LS, 1));
            if (!First)
                luaL_argerrorL(LS, 1, "Invalid instance");

            uintptr_t Second = *reinterpret_cast<uintptr_t*>(lua_touserdata(LS, 2));
            if (!Second)
                luaL_argerrorL(LS, 2, "Invalid instance");

            if (First == Second)
                lua_pushboolean(LS, true);
            else
                lua_pushboolean(LS, false);

            return 1;
        };
    };

    namespace Debug {
        int getreg(lua_State* LS) {
            //("getreg");

            lua_pushvalue(LS, LUA_REGISTRYINDEX);
            return 1;
        };

        int getinfo(lua_State* L) {
            //("debug.getinfo");

            luaL_trimstack(L, 1);
            luaL_argexpected(L, lua_isnumber(L, 1) || lua_isfunction(L, 1), 1, "function or level");

            auto infoLevel = 0;
            if (lua_isnumber(L, 1)) {
                infoLevel = lua_tointeger(L, 1);
                luaL_argcheck(L, infoLevel >= 0, 1, "level cannot be negative");
            }
            else if (lua_isfunction(L, 1)) {
                infoLevel = -lua_gettop(L);
            }

            lua_Debug debugInfo{};
            if (!lua_getinfo(L, infoLevel, "fulasnf", &debugInfo))
                luaL_argerrorL(L, 1, "invalid level");

            lua_newtable(L);

            lua_pushvalue(L, -2);
            lua_setfield(L, -2, "func");

            lua_pushstring(L, debugInfo.source);
            lua_setfield(L, -2, "source");

            lua_pushstring(L, debugInfo.short_src);
            lua_setfield(L, -2, "short_src");

            lua_pushstring(L, debugInfo.what);
            lua_setfield(L, -2, "what");

            lua_pushinteger(L, debugInfo.currentline);
            lua_setfield(L, -2, "currentline");

            lua_pushstring(L, debugInfo.name);
            lua_setfield(L, -2, "name");

            lua_pushinteger(L, debugInfo.nupvals);
            lua_setfield(L, -2, "nups");

            lua_pushinteger(L, debugInfo.nparams);
            lua_setfield(L, -2, "numparams");

            lua_pushinteger(L, debugInfo.isvararg);
            lua_setfield(L, -2, "is_vararg");

            return 1;
        }

        __forceinline void convert_level_or_function_to_closure(lua_State* L, const char* cFunctionErrorMessage,
            const bool shouldErrorOnCFunction = true) {
            luaL_checkany(L, 1);

            if (lua_isnumber(L, 1)) {
                lua_Debug debugInfo{};
                const auto level = lua_tointeger(L, 1);

                if (level < 0 || level > 255)
                    luaL_argerrorL(L, 1, "level out of bounds");

                if (!lua_getinfo(L, level, "f", &debugInfo))
                    luaL_argerrorL(L, 1, "invalid level");
            }
            else if (lua_isfunction(L, 1)) {
                lua_pushvalue(L, 1);
            }
            else {
                luaL_argerrorL(L, 1, "level or function expected");
            }

            if (!lua_isfunction(L, -1))
                luaG_runerrorL(L, "There isn't function on stack");

            if (shouldErrorOnCFunction && lua_iscfunction(L, -1))
                luaL_argerrorL(L, 1, cFunctionErrorMessage);
        }

        int getconstants(lua_State* L) {
            //("debug.getconstants");

            luaL_trimstack(L, 1);
            convert_level_or_function_to_closure(L, "Cannot get constants from C closure");

            // Do not touch this unless if you want to die
            const auto closure = lua_toclosure(L, -1);
            lua_createtable(L, closure->l.p->sizek, 0);

            for (int i = 0; i < closure->l.p->sizek; i++) {
                auto&& constant = &closure->l.p->k[i];

                if (constant->tt != LUA_TFUNCTION && constant->tt != LUA_TTABLE) {
                    setobj(L, L->top, constant);
                    incr_top(L);
                }
                else {
                    lua_pushnil(L);
                }

                lua_rawseti(L, -2, (i + 1));
            }

            return 1;
        }

        int getconstant(lua_State* L) {
            //("debug.getconstant");

            luaL_trimstack(L, 2);
            luaL_checktype(L, 2, LUA_TNUMBER);
            convert_level_or_function_to_closure(L, "Cannot get constants from C closure");

            const auto constantIndex = lua_tointeger(L, 2);
            const auto closure = lua_toclosure(L, -1);
            Proto* p = (Proto*)closure->l.p;

            if ((int)p->sizek == 0)
            {
                luaL_argerror(L, 1, ("Function doesn't has constants."));
            }

            if (!(constantIndex >= 1 && constantIndex <= p->sizek))
            {
                luaL_argerror(L, 2, ("Index out of range."));
            }

            TValue k = (TValue)p->k[constantIndex - 1];

            if (k.tt == LUA_TFUNCTION || k.tt == LUA_TTABLE)
            {
                lua_pushnil(L);
            }
            else
            {
                luaC_threadbarrier(L) luaA_pushobject(L, &k);
            }
            return 1;
        }

        int setconstant(lua_State* L) {
            //("debug.setconstant");

            luaL_trimstack(L, 3);
            luaL_checktype(L, 2, LUA_TNUMBER);
            luaL_argexpected(L, lua_isnumber(L, 3) || lua_isboolean(L, 3) || lua_isstring(L, 3), 3,
                "number or boolean or string");
            convert_level_or_function_to_closure(L, "Cannot set constants on a C closure");

            const auto constantIndex = lua_tointeger(L, 2);
            const auto closure = lua_toclosure(L, -1);

            luaL_argcheck(L, constantIndex > 0, 2, "index cannot be negative");
            luaL_argcheck(L, constantIndex <= closure->l.p->sizek, 3, "index out of range");

            setobj(L, &closure->l.p->k[constantIndex - 1], index2addr(L, 3))

                return 0;
        }

        int getupvalues(lua_State* L) {
            //("debug.getupvalues");

            luaL_trimstack(L, 1);
            convert_level_or_function_to_closure(L, "Cannot get upvalues on C Closures", true);

            const auto closure = lua_toclosure(L, -1);
            lua_newtable(L);

            lua_pushrawclosure(L, closure);
            for (int i = 0; i < closure->nupvalues;) {
                lua_getupvalue(L, -1, ++i);
                lua_rawseti(L, -3, i);
            }

            lua_pop(L, 1);
            return 1;
        }

        int getupvalue(lua_State* L) {
            //("debug.getupvalue");

            luaL_trimstack(L, 2);
            luaL_checktype(L, 2, LUA_TNUMBER);
            convert_level_or_function_to_closure(L, "Cannot get upvalue on C Closures", true);

            const auto upvalueIndex = lua_tointeger(L, 2);
            const auto closure = lua_toclosure(L, -1);

            luaL_argcheck(L, upvalueIndex > 0, 2, "index cannot be negative");
            luaL_argcheck(L, upvalueIndex <= closure->nupvalues, 2, "index out of range");

            lua_pushrawclosure(L, closure);
            lua_getupvalue(L, -1, upvalueIndex);

            lua_remove(L, -2);
            return 1;
        }

        int debug_setupvalue(lua_State* L) {
            //("debug.setupvalue");

            luaL_trimstack(L, 3);
            luaL_checktype(L, 2, LUA_TNUMBER);
            luaL_checkany(L, 3);
            convert_level_or_function_to_closure(L, "Cannot set upvalue on C Closure", true);

            const auto closure = lua_toclosure(L, -1);
            const auto upvalueIndex = lua_tointeger(L, 2);
            const auto objToSet = index2addr(L, 3);

            luaL_argcheck(L, upvalueIndex > 0, 2, "index cannot be negative");
            luaL_argcheck(L, upvalueIndex <= closure->nupvalues, 2, "index out of range");

            setobj(L, &closure->l.uprefs[upvalueIndex - 1], objToSet);
            return 0;
        }

        int getprotos(lua_State* L) {
            //("debug.getprotos");

            luaL_trimstack(L, 1);
            convert_level_or_function_to_closure(L, "Cannot get protos on C Closure");

            const auto closure = lua_toclosure(L, -1);
            Proto* originalProto = closure->l.p;

            lua_newtable(L);
            for (int i = 0; i < originalProto->sizep;) {
                const auto currentProto = originalProto->p[i];
                lua_pushrawclosure(L, luaF_newLclosure(L, currentProto->nups, closure->env, currentProto));
                lua_rawseti(L, -2, ++i);
            }

            return 1;
        }

        int getproto(lua_State* L) {
            //("debug.getproto");

            luaL_trimstack(L, 3);
            const auto isActiveProto = luaL_optboolean(L, 3, false);
            luaL_checktype(L, 2, LUA_TNUMBER);
            convert_level_or_function_to_closure(L, "Cannot get proto on C Closure");

            const auto protoIndex = lua_tointeger(L, 2);
            const auto closure = lua_toclosure(L, -1);

            luaL_argcheck(L, protoIndex > 0, 2, "index cannot be negative");
            luaL_argcheck(L, protoIndex <= closure->l.p->sizep, 2, "index out of range");

            auto proto = closure->l.p->p[protoIndex - 1];
            if (isActiveProto) {
                lua_newtable(L);
                struct LookupContext {
                    lua_State* L;
                    int count;
                    Closure* closure;
                } context{ L, 0, closure };

                luaM_visitgco(L, &context, [](void* contextPointer, lua_Page* page, GCObject* gcObject) -> bool {
                    const auto context = static_cast<LookupContext*>(contextPointer);
                    if (isdead(context->L->global, gcObject))
                        return false;

                    if (gcObject->gch.tt == LUA_TFUNCTION) {
                        const auto closure = (Closure*)gcObject;
                        if (!closure->isC && closure->l.p == context->closure->l.p
                            ->p[context->count]) {
                            setclvalue(context->L, context->L->top, closure);
                            incr_top(context->L);
                            lua_rawseti(context->L, -2, ++context->count);
                        }
                    }

                    return false;
                    });

                return 1;
            }

            lua_pushrawclosure(L, luaF_newLclosure(L, proto->nups, closure->env, proto));
            return 1;
        }

        int getstack(lua_State* L) {
            //("debug.getstack");

            luaL_checktype(L, 1, LUA_TNUMBER);
            luaL_trimstack(L, 2);

            const auto level = lua_tointeger(L, 1);
            const auto index = (lua_isnoneornil(L, 2)) ? -1 : luaL_checkinteger(L, 2);

            if (level >= (L->ci - L->base_ci) || level < 0)
                luaL_argerrorL(L, 1, "level out of range");

            if (index < -2 || index > 255)
                luaL_argerrorL(L, 2, "index out of range");

            const auto frame = L->ci - level;
            if (!frame->func || !ttisfunction(frame->func))
                luaL_argerrorL(L, 1, "invalid function in frame");

            if (clvalue(frame->func)->isC)
                luaL_argerrorL(L, 1, "level cannot point to a c closure");

            if (!frame->top || !frame->base || frame->top < frame->base)
                luaL_error(L, "invalid frame pointers");

            const size_t stackFrameSize = frame->top - frame->base;

            if (index == -1) {
                lua_newtable(L);
                for (int i = 0; i < stackFrameSize; i++) {
                    setobj2s(L, L->top, &frame->base[i]);
                    incr_top(L);

                    lua_rawseti(L, -2, i + 1);
                }
            }
            else {
                if (index < 1 || index > stackFrameSize)
                    luaL_argerrorL(L, 2, "index out of range");

                setobj2s(L, L->top, &frame->base[index - 1]);
                incr_top(L);
            }

            return 1;
        }

        int setstack(lua_State* L) {
            //("debug.setstack");

            luaL_trimstack(L, 3);
            luaL_checktype(L, 1, LUA_TNUMBER);
            luaL_checktype(L, 2, LUA_TNUMBER);
            luaL_checkany(L, 3);

            const auto level = lua_tointeger(L, 1);
            const auto index = lua_tointeger(L, 2);

            if (level >= L->ci - L->base_ci || level < 0)
                luaL_argerrorL(L, 1, "level out of range");

            const auto frame = L->ci - level;
            const size_t top = frame->top - frame->base;

            if (clvalue(frame->func)->isC)
                luaL_argerrorL(L, 1, "level cannot point to a c closure");

            if (index < 1 || index > top)
                luaL_argerrorL(L, 2, "index out of range");

            if (frame->base[index - 1].tt != lua_type(L, 3))
                luaL_argerrorL(L, 3, "type of the value on the stack is different than the object you are setting it to");

            setobj2s(L, &frame->base[index - 1], luaA_toobject(L, 3));
            return 0;
        }
    };

    namespace Metatable {
        int getrawmetatable(lua_State* LS) {
            //("getrawmetatable");

            luaL_checkany(LS, 1);

            if (!lua_getmetatable(LS, 1))
                lua_pushnil(LS);

            return 1;
        };

        int setrawmetatable(lua_State* LS) {
            //("setrawmetatable");

            luaL_argexpected(LS, lua_istable(LS, 1) || lua_islightuserdata(LS, 1) || lua_isuserdata(LS, 1) || lua_isbuffer(LS, 1) || lua_isvector(LS, 1), 1, "Expected a table or an userdata or a buffer or a vector");

            luaL_argexpected(LS, lua_istable(LS, 2) || lua_isnil(LS, 2), 2, "Expected table or nil");

            const bool OldState = lua_getreadonly(LS, 1);

            lua_setreadonly(LS, 1, false);

            lua_setmetatable(LS, 1);

            lua_setreadonly(LS, 1, OldState);

            lua_ref(LS, 1);

            return 1;
        };


        int isreadonly(lua_State* LS) {
            //("isreadonly");

            lua_pushboolean(LS, lua_getreadonly(LS, 1));
            return 1;
        };

        int setreadonly(lua_State* LS) {
            //("setreadonly");

            luaL_checktype(LS, 1, LUA_TTABLE);
            luaL_checktype(LS, 2, LUA_TBOOLEAN);

            lua_setreadonly(LS, 1, lua_toboolean(LS, 2));

            return 0;
        };

        int getnamecallmethod(lua_State* LS) {
            //("getnamecallmethod");

            auto Namecall = lua_namecallatom(LS, nullptr);
            if (Namecall == nullptr)
                lua_pushnil(LS);
            else
                lua_pushstring(LS, Namecall);

            return 1;
        };

        int hookmetamethod(lua_State* LS) { // <- lags
            //("hookmetamethod");

            luaL_checkany(LS, 1);
            luaL_checkstring(LS, 2);
            luaL_checkany(LS, 3);

            if (!lua_getmetatable(LS, 1)) {
                lua_pushnil(LS);
                return 1;
            }

            int Table = lua_gettop(LS);
            const char* Method = lua_tostring(LS, 2);
            if (!IsMetamethod(Method))
                return 0;

            auto OldReadOnly = lua_getreadonly(LS, 1);

            lua_getfield(LS, Table, Method);
            lua_pushvalue(LS, -1);

            lua_setreadonly(LS, Table, false);

            lua_pushvalue(LS, 3);
            lua_setfield(LS, Table, Method);

            lua_setreadonly(LS, Table, OldReadOnly);

            lua_remove(LS, Table);

            return 1;
        };
    };


    namespace Filesystem {
        static std::filesystem::path a = getenv("LOCALAPPDATA");
        static std::filesystem::path b = a / "Vexure";
        static std::filesystem::path c = b / "workspace";

        std::string getWorkspaceFolder() {
            if (!std::filesystem::exists(c)) {
                std::filesystem::create_directories(c);
            }

            return c.string() + "\\";
        };

        void _SplitString(std::string Str, std::string By, std::vector<std::string>& Tokens)
        {
            Tokens.push_back(Str);
            const auto splitLen = By.size();
            while (true)
            {
                auto frag = Tokens.back();
                const auto splitAt = frag.find(By);
                if (splitAt == std::string::npos)
                    break;
                Tokens.back() = frag.substr(0, splitAt);
                Tokens.push_back(frag.substr(splitAt + splitLen, frag.size() - (splitAt + splitLen)));
            }
        }

        int makefolder(lua_State* L) {
            //("makefolder");

            luaL_checktype(L, 1, LUA_TSTRING);

            std::string Path = luaL_checklstring(L, 1, 0);

            std::replace(Path.begin(), Path.end(), '\\', '/');
            std::vector<std::string> Tokens;
            _SplitString(Path, "/", Tokens);

            std::string CurrentPath = getWorkspaceFolder();
            std::replace(CurrentPath.begin(), CurrentPath.end(), '\\', '/');

            for (const auto& Token : Tokens) {
                CurrentPath += Token + "/";

                if (!std::filesystem::is_directory(CurrentPath))
                    std::filesystem::create_directory(CurrentPath);
            }

            return 0;
        };

        int isfile(lua_State* L) {
            //("isfile");

            luaL_checktype(L, 1, LUA_TSTRING);

            std::string Path = luaL_checklstring(L, 1, 0);

            std::string FullPath = getWorkspaceFolder() + Path;
            std::replace(FullPath.begin(), FullPath.end(), '\\', '/');

            lua_pushboolean(L, std::filesystem::is_regular_file(FullPath));
            return 1;
        };

        int readfile(lua_State* L) {
            //("readfile");

            luaL_checktype(L, 1, LUA_TSTRING);

            std::string Path = luaL_checklstring(L, 1, 0);

            std::string FullPath = getWorkspaceFolder() + Path;
            std::replace(FullPath.begin(), FullPath.end(), '\\', '/');

            if (std::to_string(std::filesystem::is_regular_file(FullPath)) == "1")
            {
                std::ifstream File(FullPath, std::ios::binary);
                if (!File)
                    luaL_error(L, "Failed to open file: %s", FullPath.c_str());

                std::string Content((std::istreambuf_iterator<char>(File)), std::istreambuf_iterator<char>());
                File.close();

                lua_pushlstring(L, Content.data(), Content.size());
                return 1;
            }
            else
                luaL_error(L, "Failed to open file: %s", FullPath.c_str());

            return 0;
        }



        int writefile(lua_State* L) {
            //("writefile");

            luaL_checktype(L, 1, LUA_TSTRING);
            luaL_checktype(L, 2, LUA_TSTRING);

            size_t contentSize = 0;
            std::string Path = luaL_checklstring(L, 1, 0);
            const auto Content = luaL_checklstring(L, 2, &contentSize);

            std::replace(Path.begin(), Path.end(), '\\', '/');

            std::vector<std::string> DisallowedExtensions =
            {
                ".exe", ".scr", ".bat", ".com", ".csh", ".msi", ".vb", ".vbs", ".vbe", ".ws", ".wsf", ".wsh", ".ps1"
            };

            for (std::string Extension : DisallowedExtensions) {
                if (Path.find(Extension) != std::string::npos) {
                    luaL_error(L, ("forbidden file extension"));
                }
            }

            std::string FullPath = getWorkspaceFolder() + Path;
            std::replace(FullPath.begin(), FullPath.end(), '\\', '/');
            //(FullPath);
            std::ofstream fileToWrite(FullPath, std::ios::beg | std::ios::binary);
            fileToWrite.write(Content, contentSize);
            fileToWrite.close();

            return 0;
        };

        int listfiles(lua_State* L) {
            //("listfiles");

            luaL_checktype(L, 1, LUA_TSTRING);

            std::string Path = luaL_checklstring(L, 1, 0);

            std::string FullPath = getWorkspaceFolder() + Path;
            std::replace(FullPath.begin(), FullPath.end(), '\\', '/');
            std::string halfPath = getWorkspaceFolder();
            std::string workspace = ("\\Workspace\\");

            if (!std::filesystem::is_directory(FullPath))
                luaL_error(L, ("folder does not exist"));

            lua_createtable(L, 0, 0);
            int i = 1;
            for (const auto& entry : std::filesystem::directory_iterator(FullPath)) {
                std::string path = entry.path().string().substr(halfPath.length());

                lua_pushinteger(L, i);
                lua_pushstring(L, path.c_str());
                lua_settable(L, -3);
                i++;
            }

            return 1;
        };

        int isfolder(lua_State* L) {
            //("isfolder");

            luaL_checktype(L, 1, LUA_TSTRING);

            std::string Path = luaL_checklstring(L, 1, 0);

            std::string FullPath = getWorkspaceFolder() + Path;
            std::replace(FullPath.begin(), FullPath.end(), '\\', '/');

            lua_pushboolean(L, std::filesystem::is_directory(FullPath));

            return 1;
        };

        int delfolder(lua_State* L) {
            //("delfolder");

            luaL_checktype(L, 1, LUA_TSTRING);

            std::string Path = luaL_checklstring(L, 1, 0);

            std::string FullPath = getWorkspaceFolder() + Path;
            std::replace(FullPath.begin(), FullPath.end(), '\\', '/');

            if (!std::filesystem::remove_all(FullPath))
                luaL_error(L, ("folder does not exist"));

            return 0;
        };

        int delfile(lua_State* L) {
            //("delfile");

            luaL_checktype(L, 1, LUA_TSTRING);

            std::string Path = luaL_checklstring(L, 1, 0);

            std::string FullPath = getWorkspaceFolder() + Path;
            std::replace(FullPath.begin(), FullPath.end(), '\\', '/');

            if (!std::filesystem::remove(FullPath))
                luaL_error(L, ("file does not exist"));

            return 0;
        };

        int loadfile(lua_State* L) {
            //("loadfile");

            luaL_checktype(L, 1, LUA_TSTRING);

            std::string Path = luaL_checklstring(L, 1, 0);

            std::string FullPath = getWorkspaceFolder() + Path;
            std::replace(FullPath.begin(), FullPath.end(), '\\', '/');

            if (!std::filesystem::is_regular_file(FullPath))
                luaL_error(L, ("file does not exist"));

            std::ifstream File(FullPath);
            std::string Content((std::istreambuf_iterator<char>(File)), std::istreambuf_iterator<char>());
            File.close();

            lua_pop(L, lua_gettop(L));

            lua_pushlstring(L, Content.data(), Content.size());

            return loadstring(L);
        };

        int appendfile(lua_State* L) {
            //("appendfile");

            luaL_checktype(L, 1, LUA_TSTRING);
            luaL_checktype(L, 2, LUA_TSTRING);

            size_t contentSize = 0;
            std::string Path = luaL_checklstring(L, 1, 0);
            const auto Content = luaL_checklstring(L, 2, &contentSize);

            std::replace(Path.begin(), Path.end(), '\\', '/');

            std::string FullPath = getWorkspaceFolder() + Path;
            std::replace(FullPath.begin(), FullPath.end(), '\\', '/');

            std::ofstream fileToWrite(FullPath, std::ios::binary | std::ios::app);
            fileToWrite << Content;
            fileToWrite.close();

            return 0;
        };

        int getcustomasset(lua_State* L) {
            //("getcustomasset");

            luaL_checktype(L, 1, LUA_TSTRING);
            std::string assetPath = lua_tostring(L, 1);

            std::string fullPathStr = (getWorkspaceFolder() + assetPath);
            std::replace(fullPathStr.begin(), fullPathStr.end(), '\\', '/');
            std::filesystem::path FullPath = fullPathStr;

            if (!std::filesystem::is_regular_file(FullPath))
                luaL_error(L, ("Failed to find local asset!"));

            std::filesystem::path customAssetsDir = std::filesystem::current_path() / "ExtraContent" / "Vexure";
            std::filesystem::path customAssetsFile = std::filesystem::current_path() / "ExtraContent" / "Vexure" / FullPath.filename();

            if (!std::filesystem::exists(customAssetsDir))
                std::filesystem::create_directory(customAssetsDir);

            std::filesystem::copy_file(FullPath, customAssetsFile, std::filesystem::copy_options::update_existing);

            std::string Final = "rbxasset://Vexure/" + customAssetsFile.filename().string();
            lua_pushlstring(L, Final.c_str(), Final.size());
            return 1;
        }
    };


    

    namespace Closures {
        auto FindSavedCClosure(Closure* closure) {
            const auto it = Handler::Newcclosures.find(closure);
            return it != Handler::Newcclosures.end() ? it->second : nullptr;
        };

        void SplitString(std::string Str, std::string By, std::vector<std::string>& Tokens)
        {
            Tokens.push_back(Str);
            const auto splitLen = By.size();
            while (true)
            {
                auto frag = Tokens.back();
                const auto splitAt = frag.find(By);
                if (splitAt == std::string::npos)
                    break;
                Tokens.back() = frag.substr(0, splitAt);
                Tokens.push_back(frag.substr(splitAt + splitLen, frag.size() - (splitAt + splitLen)));
            }
        };

        std::string ErrorMessage(const std::string& message) {
            static auto callstack_regex = std::regex((R"(.*"\]:(\d)*: )"), std::regex::optimize | std::regex::icase);
            if (std::regex_search(message.begin(), message.end(), callstack_regex)) {
                const auto fixed = std::regex_replace(message, callstack_regex, "");
                return fixed;
            }

            return message;
        };

        static void handler_run(lua_State* L, void* ud) {
            luaD_call(L, (StkId)(ud), LUA_MULTRET);
        };

        int NewCClosureContinuation(lua_State* L, std::int32_t status) {
            if (status != LUA_OK) {
                std::size_t error_len;
                const char* errmsg = luaL_checklstring(L, -1, &error_len);
                lua_pop(L, 1);
                std::string error(errmsg);

                if (error == std::string(("attempt to yield across metamethod/C-call boundary")))
                    return lua_yield(L, LUA_MULTRET);

                std::string fixedError = ErrorMessage(error);
                std::regex pattern(R"([^:]+:\d+:\s?)");

                std::smatch match;
                if (std::regex_search(fixedError, match, pattern)) {
                    fixedError.erase(match.position(), match.length());
                }

                lua_pushlstring(L, fixedError.data(), fixedError.size());
                lua_error(L);
                return 0;
            }

            return lua_gettop(L);
        };

        int NewCClosureStub(lua_State* L) {
            const auto nArgs = lua_gettop(L);

            Closure* cl = clvalue(L->ci->func);
            if (!cl)
                luaL_error(L, ("Invalid closure"));

            const auto originalClosure = FindSavedCClosure(cl);
            if (!originalClosure)
                luaL_error(L, ("Invalid closure"));

            setclvalue(L, L->top, originalClosure);
            L->top++;

            lua_insert(L, 1);

            StkId func = L->base;
            L->ci->flags |= LUA_CALLINFO_HANDLE;

            L->baseCcalls++;
            int status = luaD_pcall(L, handler_run, func, savestack(L, func), 0);
            L->baseCcalls--;

            if (status == LUA_ERRRUN) {
                std::size_t error_len;
                const char* errmsg = luaL_checklstring(L, -1, &error_len);
                lua_pop(L, 1);
                std::string error(errmsg);

                if (error == std::string(("attempt to yield across metamethod/C-call boundary")))
                    return lua_yield(L, LUA_MULTRET);

                std::string fixedError = ErrorMessage(error);
                std::regex pattern(R"([^:]+:\d+:\s?)");

                std::smatch match;
                if (std::regex_search(fixedError, match, pattern)) {
                    fixedError.erase(match.position(), match.length());
                }

                lua_pushlstring(L, fixedError.data(), fixedError.size());
                lua_error(L);
                return 0;
            }

            expandstacklimit(L, L->top);

            if (status == 0 && (L->status == LUA_YIELD || L->status == LUA_BREAK))
                return -1;

            return lua_gettop(L);
        };

        int NonYieldNewCClosureStub(lua_State* L) {
            const auto nArgs = lua_gettop(L);

            Closure* cl = clvalue(L->ci->func);
            if (!cl)
                luaL_error(L, ("Invalid closure"));

            const auto originalClosure = FindSavedCClosure(cl);
            if (!originalClosure)
                luaL_error(L, ("Invalid closure"));

            setclvalue(L, L->top, originalClosure);
            L->top++;

            lua_insert(L, 1);

            StkId func = L->base;
            L->ci->flags |= LUA_CALLINFO_HANDLE;

            L->baseCcalls++;
            int status = luaD_pcall(L, handler_run, func, savestack(L, func), 0);
            L->baseCcalls--;

            if (status == LUA_ERRRUN) {
                std::size_t error_len;
                const char* errmsg = luaL_checklstring(L, -1, &error_len);
                lua_pop(L, 1);
                std::string error(errmsg);

                if (error == std::string(("attempt to yield across metamethod/C-call boundary")))
                    return lua_yield(L, 0);

                std::string fixedError = ErrorMessage(error);
                std::regex pattern(R"([^:]+:\d+:\s?)");

                std::smatch match;
                if (std::regex_search(fixedError, match, pattern)) {
                    fixedError.erase(match.position(), match.length());
                }

                lua_pushlstring(L, fixedError.data(), fixedError.size());
                lua_error(L);
                return 0;
            }

            expandstacklimit(L, L->top);

            if (status == 0 && (L->status == LUA_YIELD || L->status == LUA_BREAK))
                return -1;

            return lua_gettop(L);
        };

        void WrapClosure(lua_State* L, int idx) {
            auto nIdx = idx < 0 ? idx - 1 : idx;

            Closure* OldClosure = clvalue(luaA_toobject(L, idx));
            Proto* p = OldClosure->l.p.Get();

            lua_ref(L, idx);
            Handler::PushWrappedClosure(L, NewCClosureStub, 0, 0, NewCClosureContinuation);

            Closure* HookClosure = clvalue(luaA_toobject(L, -1));

            HookClosure->isC = 1;
            HookClosure->env = OldClosure->env;
            //HookClosure->l.p.Set(OldClosure->l.p.Get());

            lua_ref(L, -1);

            Handler::Newcclosures[HookClosure] = OldClosure;
        };

        enum type
        {
            None,
            RobloxClosure,
            LuauClosure,
            ExecutorFunction,
            NewCClosure,
        };

        static type GetClosureType(Closure* closure)
        {
            auto cl_type = None;

            if (!closure->isC)
            {
                cl_type = LuauClosure;
            }
            else
            {
                if (closure->c.f.Get() == Handler::ClosuresHandler)
                {
                    if (Handler::GetClosure(closure) == NewCClosureStub || Handler::GetClosure(closure) == NonYieldNewCClosureStub)
                        cl_type = NewCClosure;
                    else
                        cl_type = ExecutorFunction;
                }
                else
                    cl_type = RobloxClosure;
            }

            return cl_type;
        };





        std::string strip_error_message(const std::string& message) {
            static auto callstack_regex = std::regex(R"(.*"\]:(\d)*: )", std::regex::optimize | std::regex::icase);
            if (std::regex_search(message.begin(), message.end(), callstack_regex)) {
                const auto fixed = std::regex_replace(message, callstack_regex, "");
                return fixed;
            }

            return message;
        };

        int newcc_continuation(lua_State* L, int Status) {
            if (Status != LUA_OK) {
                const auto regexed_error = strip_error_message(lua_tostring(L, -1));
                lua_pop(L, 1);

                lua_pushlstring(L, regexed_error.c_str(), regexed_error.size());
                lua_error(L);
            }

            return lua_gettop(L);
        };

        int newcc_proxy(lua_State* L) {
            const auto closure = newcc_map.contains(clvalue(L->ci->func)) ? newcc_map.at(clvalue(L->ci->func)) : nullptr;

            if (!closure)
                luaL_error(L, "unable to find closure");

            setclvalue(L, L->top, closure);
            L->top++;

            lua_insert(L, 1);

            StkId function = L->base;
            L->ci->flags |= LUA_CALLINFO_HANDLE;

            L->baseCcalls++;
            int status = luaD_pcall(L, handler_run, function, savestack(L, function), 0);
            L->baseCcalls--;

            if (status == LUA_ERRRUN) {
                const auto regexed_error = strip_error_message(lua_tostring(L, -1));
                lua_pop(L, 1);

                lua_pushlstring(L, regexed_error.c_str(), regexed_error.size());
                lua_error(L);
                return 0;
            }

            expandstacklimit(L, L->top);

            if (status == 0 && (L->status == LUA_YIELD || L->status == LUA_BREAK))
                return -1;

            return lua_gettop(L);
        };

        enum closure_type_t {
            lclosure, cclosure,
            newcc, unidentified
        };

        template<typename T>
        __forceinline static bool is_ptr_valid(T* tValue) {
            const auto ptr = reinterpret_cast<const void*>(const_cast<const T*>(tValue));
            auto buffer = MEMORY_BASIC_INFORMATION{};

            if (const auto read = VirtualQuery(ptr, &buffer, sizeof(buffer)); read != 0 && sizeof(buffer) != read) {
            }
            else if (read == 0) {
                return false;
            }

            if (buffer.RegionSize < sizeof(T)) {
                return false;
            }

            if (buffer.State & MEM_FREE == MEM_FREE) {
                return false;
            }

            auto valid_prot = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;

            if (buffer.Protect & valid_prot) {
                return true;
            }
            if (buffer.Protect & (PAGE_GUARD | PAGE_NOACCESS)) {
                return false;
            }

            return true;
        }

        closure_type_t identify_closure(Closure* closure) {
            if (!closure->isC)
                return closure_type_t::lclosure;
            else if (closure->c.f == newcc_proxy)
                return closure_type_t::newcc;
            else if (closure->isC)
                return closure_type_t::cclosure;
            else
                return closure_type_t::unidentified;
        }

    };

    namespace Http {
        __forceinline static std::optional<const std::string> GetHwid() {
            HW_PROFILE_INFO hwProfileInfo;
            if (!GetCurrentHwProfile(&hwProfileInfo)) {
                return {};
            }

            CryptoPP::SHA256 sha256;
            unsigned char digest[CryptoPP::SHA256::DIGESTSIZE];
            sha256.CalculateDigest(digest, reinterpret_cast<unsigned char*>(hwProfileInfo.szHwProfileGuid),
                sizeof(hwProfileInfo.szHwProfileGuid));

            CryptoPP::HexEncoder encoder;
            std::string output;
            encoder.Attach(new CryptoPP::StringSink(output));
            encoder.Put(digest, sizeof(digest));
            encoder.MessageEnd();

            return output;
        }


        using _Prin2t = uintptr_t(__fastcall*)(uintptr_t, const char*, ...);
        auto Prin2t = (_Prin2t)(Offsets::Print);

        typedef struct _PEB {
            BYTE                          Reserved1[2];
            BYTE                          BeingDebugged;
            BYTE                          Reserved2[1];
            PVOID                         Reserved3[2];
            PVOID                 Ldr;
            PVOID  ProcessParameters;
            PVOID                         Reserved4[3];
            PVOID                         AtlThunkSListPtr;
            PVOID                         Reserved5;
            ULONG                         Reserved6;
            PVOID                         Reserved7;
            ULONG                         Reserved8;
            ULONG                         AtlThunkSListPtr32;
            PVOID                         Reserved9[45];
            BYTE                          Reserved10[96];
            BYTE                          Reserved11[128];
            PVOID                         Reserved12[1];
            ULONG                         SessionId;
        } PEB;

        typedef struct _RTL_USER_PROCESS_PARAMETERS {
            BYTE           Reserved1[16];
            HANDLE ConsoleHandle;
        } RTL_USER_PROCESS_PARAMETERS;

        PEB* GetPeb() {
            PEB* peb = (PEB*)__readgsqword(0x60);
            return peb;
        }

        void SetColor(int color) {
            HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
            SetConsoleTextAttribute(hConsole, color);
        }

        int ConsoleCreate(lua_State* L) {/*
            AllocConsole();
            SetConsoleTitleA(ObfStr("Ancryptic | Roblox Console"));
            freopen("CONOUT$", "w", stdout);
            freopen("CONIN$", "r", stdin);
            PEB* peb = GetPeb();
            if (peb != nullptr && peb->ProcessParameters != nullptr) {
                RTL_USER_PROCESS_PARAMETERS* pm = (RTL_USER_PROCESS_PARAMETERS*)peb->ProcessParameters;

                pm->ConsoleHandle = 0;
            }*/
            return 1;
        }

        int ConsoleClear(lua_State* L) {/*
            system("cls");*/
            return 1;
        }

        int ConsoleDestroy(lua_State* L) {/*
            FreeConsole();*/
            return 1;
        }

        int ConsolePrint(lua_State* L) {/*
            luaL_checktype(L, 1, LUA_TSTRING);
            size_t text_size;
            std::string text = luaL_checklstring(L, 1, &text_size);
            if (text == "@@BLACK@@")
                SetColor(0);
            else if (text == "@@BLUE@@")
                SetColor(1);
            else if (text == "@@GREEN@@")
                SetColor(2);
            else if (text == "@@CYAN@@")
                SetColor(3);
            else if (text == "@@RED@@")
                SetColor(4);
            else if (text == "@@MAGENTA@@")
                SetColor(5);
            else if (text == "@@BROWN@@")
                SetColor(6);
            else if (text == "@@LIGHT_GRAY@@")
                SetColor(7);
            else if (text == "@@DARK_GRAY@@")
                SetColor(8);
            else if (text == "@@LIGHT_BLUE@@")
                SetColor(9);
            else if (text == "@@LIGHT_GREEN@@")
                SetColor(10);
            else if (text == "@@LIGHT_CYAN@@")
                SetColor(11);
            else if (text == "@@LIGHT_RED@@")
                SetColor(12);
            else if (text == "@@LIGHT_MAGENTA@@")
                SetColor(13);
            else if (text == "@@YELLOW@@")
                SetColor(14);
            else if (text == "@@WHITE@@")
                SetColor(15);
            else
                WriteConsoleR(text.c_str());
            Prin2t(RBX_NORMAL, text.c_str()); */
            return 1;
        }

        int ConsoleInfo(lua_State* L) {/*
            luaL_checktype(L, 1, LUA_TSTRING);
            size_t text_size;
            std::string text = luaL_checklstring(L, 1, &text_size);
            std::string er = "[*]: ";
            er += text;
            Prin2t(RBX_INFO, text.c_str());*/
            return 1;
        }

        int ConsoleWarn(lua_State* L) {/*
            luaL_checktype(L, 1, LUA_TSTRING);
            size_t text_size;
            std::string text = luaL_checklstring(L, 1, &text_size);
            std::string er = "[!]: ";
            er += text;
            Prin2t(RBX_WARN, text.c_str());*/
            return 1;
        }

        int ConsoleErr(lua_State* L) {/*
            luaL_checktype(L, 1, LUA_TSTRING);
            size_t text_size;
            std::string text = luaL_checklstring(L, 1, &text_size);
            std::string er = "[-]: ";
            er += text;
            Prin2t(RBX_ERROR, text.c_str());*/
            return 1;
        }

        int ConsoleInput(lua_State* L) {
            /*
            std::string text;
            std::cin >> text;
            lua_pushstring(L, text.c_str());*/
            return 1;
        }

        int ConsoleName(lua_State* L) {/*
            luaL_checktype(L, 1, LUA_TSTRING);
            size_t text_size;
            std::string text = luaL_checklstring(L, 1, &text_size);
            SetConsoleTitleA(text.c_str());*/
            return 1;
        }


        enum RequestMethods
        {
            H_GET,
            H_HEAD,
            H_POST,
            H_PUT,
            H_DELETE,
            H_OPTIONS
        };
        std::map<std::string, RequestMethods> RequestMethodMap = {
            { "get", H_GET },
            { "head", H_HEAD },
            { "post", H_POST },
            { "put", H_PUT },
            { "delete", H_DELETE },
            { "options", H_OPTIONS }
        };

        int HttpGet(lua_State* L) {
            //("httpget");

            std::string url;

            if (!lua_isstring(L, 1)) {
                luaL_checkstring(L, 2);
                url = std::string(lua_tostring(L, 2));
            }
            else {
                url = std::string(lua_tostring(L, 1));
            }

            if (url.find("http://") == std::string::npos && url.find("https://") == std::string::npos) {
                luaL_argerror(L, 1, "Invalid protocol: expected 'http://' or 'https://'");
            }

            return HelpFuncs::YieldExecution(L, [url]() -> std::function<int(lua_State*)> {
                cpr::Header Headers;

                std::string GameId = std::to_string(HelpFuncs::GetGameId());
                std::string PlaceId = std::to_string(HelpFuncs::GetPlaceId());
                using json = nlohmann::json;
                json sessionIdJson;

                if (!GameId.empty() && !PlaceId.empty()) {
                    sessionIdJson["GameId"] = GameId;
                    sessionIdJson["PlaceId"] = PlaceId;
                    Headers.insert({ "Roblox-Game-Id", GameId });
                }
                else {
                    sessionIdJson["GameId"] = "none";
                    sessionIdJson["PlaceId"] = "none";
                    Headers.insert({ "Roblox-Game-Id", "none" });
                }

                Headers.insert({ "User-Agent", "Roblox/WinInet" });
                Headers.insert({ "Roblox-Session-Id", sessionIdJson.dump() });
                Headers.insert({ "Accept-Encoding", "identity" });

                cpr::Response Result;
                try {
                    Result = cpr::Get(cpr::Url{ url }, cpr::Header(Headers));
                }
                catch (const std::exception& e) {
                    return [e](lua_State* L) -> int {
                        lua_pushstring(L, ("HttpGet failed: " + std::string(e.what())).c_str());
                        //("HttpGet failed - 1");
                        return 1;
                        };
                }
                return [Result](lua_State* L) -> int {
                    try {
                        if (Result.status_code == 0 || HttpStatus::IsError(Result.status_code)) {
                            auto Error = std::format("HttpGet failed with status {} - {}", Result.status_code, Result.error.message);
                            lua_pushstring(L, Error.c_str());
                            return 1;
                        }
                        if (Result.status_code == 401) {
                            lua_pushstring(L, "HttpGet failed with unauthorized access");
                            return 1;
                        }
                        lua_pushlstring(L, Result.text.data(), Result.text.size());

                        return 1;

                    }
                    catch (...) {
                        //("HttpGet-failed");
                        lua_pushstring(L, "HttpGet failed");
                        return 1;
                    }
                    };
                });
        }


        int request(lua_State* L) {
            //("request");

            luaL_checktype(L, 1, LUA_TTABLE);

            lua_getfield(L, 1, "Url");
            if (lua_type(L, -1) != LUA_TSTRING) {
                luaL_error(L, "Invalid or no 'Url' field specified in request table");
                return 0;
            }

            std::string Url = lua_tostring(L, -1);
            if (Url.find("http") != 0) {
                luaL_error(L, "Invalid protocol specified (expected 'http://' or 'https://')");
                return 0;
            }

            lua_pop(L, 1);

            auto Method = H_GET;
            lua_getfield(L, 1, "Method");

            if (lua_type(L, -1) == LUA_TSTRING) {
                std::string Methods = luaL_checkstring(L, -1);
                std::transform(Methods.begin(), Methods.end(), Methods.begin(), tolower);

                if (!RequestMethodMap.count(Methods)) {
                    luaL_error(L, "request type '%s' is not a valid http request type.", Methods.c_str());
                    return 0;
                }

                Method = RequestMethodMap[Methods];
            }

            lua_pop(L, 1);

            cpr::Header Headers;

            lua_getfield(L, 1, "Headers");
            if (lua_type(L, -1) == LUA_TTABLE) {
                lua_pushnil(L);

                while (lua_next(L, -2)) {
                    if (lua_type(L, -2) != LUA_TSTRING || lua_type(L, -1) != LUA_TSTRING) {
                        luaL_error(L, "'Headers' table must contain string keys/values.");
                        return 0;
                    }

                    std::string HeaderKey = luaL_checkstring(L, -2);
                    auto HeaderCopy = std::string(HeaderKey);
                    std::ranges::transform(HeaderKey, HeaderKey.begin(), tolower);

                    if (HeaderCopy == "content-length") {
                        luaL_error(L, "Headers: 'Content-Length' header cannot be overwritten.");
                        return 0;
                    }

                    std::string HeaderValue = luaL_checkstring(L, -1);
                    Headers.insert({ HeaderKey, HeaderValue });
                    lua_pop(L, 1);
                }
            }
            lua_pop(L, 1);

            cpr::Cookies Cookies;
            lua_getfield(L, 1, "Cookies");

            if (lua_type(L, -1) == LUA_TTABLE) {
                std::map<std::string, std::string> RobloxCookies;
                lua_pushnil(L);

                while (lua_next(L, -2)) {
                    if (lua_type(L, -2) != LUA_TSTRING || lua_type(L, -1) != LUA_TSTRING) {
                        luaL_error(L, "'Cookies' table must contain string keys/values.");
                        return 0;
                    }

                    std::string CookieKey = luaL_checkstring(L, -2);
                    std::string CookieValue = luaL_checkstring(L, -1);

                    RobloxCookies[CookieKey] = CookieValue;
                    lua_pop(L, 1);
                }

                Cookies = RobloxCookies;
            }

            lua_pop(L, 1);

            auto HasUserAgent = false;
            for (auto& Header : Headers) {
                auto HeaderName = Header.first;
                std::transform(HeaderName.begin(), HeaderName.end(), HeaderName.begin(), tolower);

                if (HeaderName == "user-agent")
                    HasUserAgent = true;
            }

            if (!HasUserAgent) {
                Headers.insert({ "User-Agent", (Name.c_str()) });
            }

            auto hwidd = GetHwid();

            if (hwidd.has_value()) {
                Headers.insert({ "Selp-Fingerprint", GetHwid().value() });
            }
            else {
                Headers.insert({ "Selp-Fingerprint", "0" });
            }

            std::string GameId = std::to_string(HelpFuncs::GetGameId());
            std::string PlaceId = std::to_string(HelpFuncs::GetPlaceId());

            using json = nlohmann::json;
            json sessionIdJson;


            if (!GameId.empty() && !PlaceId.empty()) {
                sessionIdJson["GameId"] = GameId;
                sessionIdJson["PlaceId"] = PlaceId;
                Headers.insert({ "Roblox-Game-Id", GameId });
            }
            else {
                sessionIdJson["GameId"] = "none";
                sessionIdJson["PlaceId"] = "none";
                Headers.insert({ "Roblox-Game-Id", "none" });
            }

            Headers.insert({ "Roblox-Session-Id", sessionIdJson.dump() });

            std::string Body;
            lua_getfield(L, 1, "Body");
            if (lua_type(L, -1) == LUA_TTABLE) {
                if (Method == H_GET || Method == H_HEAD) {
                    luaL_error(L, "'Body' cant be represent in Get or head requests");
                    return 0;
                }

                Body = luaL_checkstring(L, -1);
            }

            lua_pop(L, 1);

            return HelpFuncs::YieldExecution(L,
                [Method, Url, Headers, Cookies, Body]() -> auto {
                    cpr::Response Response;

                    switch (Method) {
                    case H_GET: {
                        Response = cpr::Get(cpr::Url{ Url }, Cookies, Headers);
                        break;
                    }
                    case H_HEAD: {
                        Response = cpr::Head(cpr::Url{ Url }, Cookies, Headers);
                        break;
                    }
                    case H_POST: {
                        Response = cpr::Post(cpr::Url{ Url }, cpr::Body{ Body }, Cookies, Headers);
                        break;
                    }
                    case H_PUT: {
                        Response = cpr::Put(cpr::Url{ Url }, cpr::Body{ Body }, Cookies, Headers);
                        break;
                    }
                    case H_DELETE: {
                        Response = cpr::Delete(cpr::Url{ Url }, cpr::Body{ Body }, Cookies, Headers);
                        break;
                    }
                    case H_OPTIONS: {
                        Response = cpr::Options(cpr::Url{ Url }, cpr::Body{ Body }, Cookies, Headers);
                        break;
                    }
                    default: {
                        throw std::exception("invalid request type");
                    }
                    }

                    return [Response](lua_State* L) -> int {

                        lua_newtable(L);

                        lua_pushboolean(L, HttpStatus::IsSuccessful(Response.status_code));
                        lua_setfield(L, -2, "Success");

                        lua_pushinteger(L, Response.status_code);
                        lua_setfield(L, -2, "StatusCode");

                        auto Phrase = HttpStatus::ReasonPhrase(Response.status_code);
                        lua_pushlstring(L, Phrase.c_str(), Phrase.size());
                        lua_setfield(L, -2, "StatusMessage");

                        lua_newtable(L);

                        for (auto& Header : Response.header) {
                            lua_pushlstring(L, Header.first.c_str(), Header.first.size());
                            lua_pushlstring(L, Header.second.c_str(), Header.second.size());

                            lua_settable(L, -3);
                        }

                        lua_setfield(L, -2, "Headers");

                        lua_newtable(L);

                        for (auto& cookie : Response.cookies.map_) {
                            lua_pushlstring(L, cookie.first.c_str(), cookie.first.size());
                            lua_pushlstring(L, cookie.second.c_str(), cookie.second.size());

                            lua_settable(L, -3);
                        }

                        lua_setfield(L, -2, "Cookies");

                        lua_pushlstring(L, Response.text.c_str(), Response.text.size());
                        lua_setfield(L, -2, "Body");

                        return 1;
                        };
                }
            );

            //return 0;
        }
    };

    namespace Signals {
        struct weak_thread_ref_t2 {
            std::atomic< std::int32_t > _refs;
            lua_State* thread;
            std::int32_t thread_ref;
            std::int32_t object_id;
            std::int32_t unk1;
            std::int32_t unk2;

            weak_thread_ref_t2(lua_State* L)
                : thread(L), thread_ref(NULL), object_id(NULL), unk1(NULL), unk2(NULL) {
            };
        };
        struct live_thread_ref
        {
            int __atomic_refs; // 0
            lua_State* th; // 8
            int thread_id; // 16
            int ref_id; // 20
        };
        struct functionscriptslot_t {
            void* vftable;
            std::uint8_t pad_0[104];
            live_thread_ref* func_ref; // 112

        };
        struct slot_t {
            int64_t unk_0; // 0
            void* func_0; // 8

            slot_t* next; // 16
            void* __atomic; // 24

            int64_t sig; // 32

            void* func_1; // 40

            functionscriptslot_t* storage; // 48
        };

        struct SignalConnectionBridge {
            slot_t* islot; // 0
            int64_t __shared_reference_islot; // 8

            int64_t unk0; // 16
            int64_t unk1; // 24
        };


        namespace Lua_Objects {
            inline std::unordered_map<slot_t*, int64_t> NodeMap;

            class RobloxConnection_t {
            private:
                slot_t* node;
                int64_t sig;
                live_thread_ref* func_ref;
                bool bIsLua;

            public:
                RobloxConnection_t(slot_t* node, bool isLua) : node(node), bIsLua(isLua)
                {
                    if (isLua)
                    {
                        if (node->storage && (node->storage->func_ref != nullptr))
                        {
                            func_ref = node->storage->func_ref;
                        }
                        else
                            func_ref = nullptr;
                    }
                    else
                    {
                        func_ref = nullptr;
                    }
                }
                bool isLuaConn() { return bIsLua; }
                auto get_function_ref() -> int { return func_ref->ref_id; }
                auto get_thread_ref() -> int { return func_ref->thread_id; }
                auto get_luathread() -> lua_State* { return func_ref->th; }
                auto get_node() -> slot_t* { return this->node; }

                auto is_enabled() -> bool { return node->sig != NULL; }

                auto disable() -> void
                {
                    if (!NodeMap[node])
                    {
                        NodeMap[node] = node->sig;
                    }
                    node->sig = NULL;
                }

                auto enable() -> void
                {
                    if (!node->sig)
                    {
                        node->sig = NodeMap[node];
                    }
                }
            };
        }

        int TRobloxScriptConnection = 0;

        static int disable_connection(lua_State* ls)
        {
            auto pInfo = reinterpret_cast<Lua_Objects::RobloxConnection_t*>(lua_touserdata(ls, lua_upvalueindex(1)));
            if (!pInfo)
                luaL_error(ls, "Invalid connection userdata in disable_connection");
            pInfo->disable();
            return 0;
        }

        static int enable_connection(lua_State* ls)
        {
            auto pInfo = reinterpret_cast<Lua_Objects::RobloxConnection_t*>(lua_touserdata(ls, lua_upvalueindex(1)));
            if (!pInfo)
                luaL_error(ls, "Invalid connection userdata in enable_connection");
            pInfo->enable();
            return 0;
        }

        int connection_blank(lua_State* rl) {
            return 0;
        }

        static int fire_connection(lua_State* ls)
        {
            const auto nargs = lua_gettop(ls);

            lua_pushvalue(ls, lua_upvalueindex(1));
            if (lua_isfunction(ls, -1))
            {
                lua_insert(ls, 1);
                lua_call(ls, nargs, 0);
            }

            return 0;
        }

        static int defer_connection(lua_State* ls)
        {
            const auto nargs = lua_gettop(ls);

            lua_getglobal(ls, "task");
            lua_getfield(ls, -1, "defer");
            lua_remove(ls, -2);

            lua_pushvalue(ls, lua_upvalueindex(1));
            if (!lua_isfunction(ls, -1))
                return 0;

            lua_insert(ls, 1);
            lua_insert(ls, 1);

            if (lua_pcall(ls, nargs + 1, 1, 0) != LUA_OK)
                luaL_error(ls, "Error in defer_connection: %s", lua_tostring(ls, -1));

            return 1;
        }


        static int disconnect_connection(lua_State* ls)
        {
            auto pInfo = reinterpret_cast<Lua_Objects::RobloxConnection_t*>(lua_touserdatatagged(ls, lua_upvalueindex(1), 72));
            if (!pInfo)
                luaL_error(ls, "Invalid connection userdata in disconnect_connection");

            if (!pInfo->is_enabled())
                luaL_error(ls, ("Cannot Disconnect a Disabled connection! ( Enable it first before Disconnecting. )"));

            auto pUd = reinterpret_cast<SignalConnectionBridge*>(lua_newuserdatatagged(ls, sizeof(SignalConnectionBridge), TRobloxScriptConnection));
            if (!pUd)
                luaL_error(ls, "Failed to create SignalConnectionBridge userdata");

            pUd->islot = pInfo->get_node();
            pUd->unk0 = 0;
            pUd->unk1 = 0;

            lua_getfield(ls, LUA_REGISTRYINDEX, ("RobloxScriptConnection"));
            if (!lua_istable(ls, -1))
                luaL_error(ls, "RobloxScriptConnection metatable not found in registry");
            lua_setmetatable(ls, -2);

            lua_getfield(ls, -1, ("Disconnect"));
            if (!lua_isfunction(ls, -1))
                luaL_error(ls, "Disconnect function not found in RobloxScriptConnection metatable");

            lua_pushvalue(ls, -2);
            if (lua_pcall(ls, 1, 0, 0) != LUA_OK) {
                const char* err = lua_tostring(ls, -1);
                luaL_error(ls, ("Error while disconnecting connection (%s)"), err);
            }

            return 0;
        }

        static int mt_newindex(lua_State* ls)
        {
            auto pInfo = reinterpret_cast<Lua_Objects::RobloxConnection_t*>(lua_touserdatatagged(ls, 1, 72));
            if (!pInfo)
                luaL_error(ls, "Invalid connection userdata in __newindex");

            const char* key = luaL_checkstring(ls, 2);

            if (strcmp(key, "Enabled") == 0)
            {
                bool enabled = luaL_checkboolean(ls, 3);
                if (enabled)
                    pInfo->enable();
                else
                    pInfo->disable();
            }

            return 0;
        }

        static int mt_index(lua_State* ls)
        {
            const auto pInfo = reinterpret_cast<Lua_Objects::RobloxConnection_t*>(lua_touserdatatagged(ls, 1, 72));
            const char* key = luaL_checkstring(ls, 2);

            bool is_l_connection = true;

            if (strcmp(key, ("Enabled")) == 0 || strcmp(key, ("Connected")) == 0)
            {
                lua_pushboolean(ls, pInfo->is_enabled());
                return 1;
            }
            else if (strcmp(key, ("Disable")) == 0)
            {
                lua_pushvalue(ls, 1);
                lua_pushcclosure(ls, disable_connection, nullptr, 1);
                return 1;
            }
            else if (strcmp(key, ("Enable")) == 0)
            {
                lua_pushvalue(ls, 1);
                lua_pushcclosure(ls, enable_connection, nullptr, 1);
                return 1;
            }
            else if (strcmp(key, ("LuaConnection")) == 0)
            {
                lua_pushboolean(ls, pInfo->isLuaConn());
                return 1;
            }
            else if (strcmp(key, ("ForeignState")) == 0)
            {
                if (pInfo->isLuaConn() == false) {
                    lua_pushboolean(ls, true);
                    return 1;
                }

                auto th = pInfo->get_luathread();
                if (!th)
                {
                    const auto ref = pInfo->get_thread_ref();
                    if (ref) {
                        lua_getref(ls, ref);
                        if (!lua_isthread(ls, -1))
                        {
                            lua_pushboolean(ls, true);
                            return 1;
                        }
                        else
                        {
                            th = lua_tothread(ls, -1);
                            lua_pop(ls, 1);
                        }
                    }
                    else {
                        lua_pushboolean(ls, true);
                        return 1;
                    }
                }

                lua_pushboolean(ls, (th->global != ls->global));
                return 1;
            }
            else if (strcmp(key, ("Function")) == 0)
            {
                if (pInfo->isLuaConn() == false) {
                    lua_pushnil(ls);
                    return 1;
                }

                auto th = pInfo->get_luathread();
                if (!th)
                {
                    const auto ref = pInfo->get_thread_ref();
                    if (ref) {
                        lua_getref(ls, ref);
                        if (!lua_isthread(ls, -1))
                        {
                            lua_pushnil(ls);
                            return 1;
                        }
                        else
                        {
                            th = lua_tothread(ls, -1);
                            lua_pop(ls, 1);
                        }
                    }
                    else
                    {
                        lua_pushnil(ls);
                        return 1;
                    }
                }

                if (th->global != ls->global)
                {
                    lua_pushnil(ls);
                    return 1;
                }

                const auto ref = pInfo->get_function_ref();
                if (ref) {
                    lua_getref(ls, ref);
                    if (!lua_isfunction(ls, -1))
                    {
                        lua_pushnil(ls);
                    }
                }
                else
                    lua_pushnil(ls);
                return 1;
            }
            else if (strcmp(key, ("Thread")) == 0)
            {
                if (pInfo->isLuaConn() == false) {
                    lua_pushnil(ls);
                    return 1;
                }

                auto th = pInfo->get_luathread();
                if (!th)
                {
                    const auto ref = pInfo->get_thread_ref();
                    if (ref) {
                        lua_getref(ls, ref);
                        if (!lua_isthread(ls, -1))
                        {
                            lua_pushnil(ls);
                            return 1;
                        }
                        else
                        {
                            return 1;
                        }
                    }
                    else {
                        lua_pushnil(ls);
                        return 1;
                    }
                }

                luaC_threadbarrier(ls) setthvalue(ls, ls->top, th) ls->top++;
                return 1;
            }
            else if (strcmp(key, ("Fire")) == 0)
            {
                lua_getfield(ls, 1, ("Function"));
                lua_pushcclosure(ls, fire_connection, nullptr, 1);
                return 1;
            }
            else if (strcmp(key, ("Defer")) == 0)
            {
                lua_getfield(ls, 1, ("Function"));
                lua_pushcclosure(ls, defer_connection, nullptr, 1);
                return 1;
            }
            else if (strcmp(key, ("Disconnect")) == 0)
            {
                lua_pushvalue(ls, 1);
                lua_pushcclosure(ls, disconnect_connection, nullptr, 1);
                return 1;
            }

            return 0;
        }

        int allcons(lua_State* ls)
        {
            luaL_checktype(ls, 1, LUA_TUSERDATA);

            if (strcmp(luaL_typename(ls, 1), ("RBXScriptSignal")) != 0)
                luaL_typeerror(ls, 1, ("RBXScriptSignal"));

            const auto stub = ([](lua_State*) -> int { return 0; });

            static void* funcScrSlotvft = nullptr;
            static void* waitvft = nullptr;
            static void* oncevft = nullptr;
            static void* connectparalellvft = nullptr;

            lua_getfield(ls, 1, "Connect");
            {
                lua_pushvalue(ls, 1);
                lua_pushcfunction(ls, stub, nullptr);
            }
            if (lua_pcall(ls, 2, 1, 0) != 0)
                luaL_error(ls, "Error calling Connect stub: %s", lua_tostring(ls, -1));

            auto sigconbr = reinterpret_cast<SignalConnectionBridge*>(lua_touserdata(ls, -1));
            if (!sigconbr)
                luaL_error(ls, "Failed to retrieve connection stub");

            if (!funcScrSlotvft)
                funcScrSlotvft = sigconbr->islot->storage->vftable;

            auto Node = sigconbr->islot->next;

            if (!TRobloxScriptConnection)
                TRobloxScriptConnection = lua_userdatatag(ls, -1);

            lua_getfield(ls, -1, "Disconnect");
            {
                lua_insert(ls, -2);
            }
            if (lua_pcall(ls, 1, 0, 0) != 0)
                luaL_error(ls, "Error calling Disconnect on stub: %s", lua_tostring(ls, -1));

            if (!waitvft && !oncevft && !connectparalellvft) {
                lua_getfield(ls, 1, "Once");
                {
                    lua_pushvalue(ls, 1);
                    lua_pushcfunction(ls, stub, nullptr);
                }
                lua_call(ls, 2, 1);
                auto sigconbr_once = reinterpret_cast<SignalConnectionBridge*>(lua_touserdata(ls, -1));
                if (!sigconbr_once)
                    luaL_error(ls, "Failed to retrieve Once stub");
                oncevft = sigconbr_once->islot->storage->vftable;
                lua_getfield(ls, -1, "Disconnect");
                {
                    lua_insert(ls, -2);
                }
                lua_call(ls, 1, 0);
            }

            int idx = 1;
            lua_newtable(ls);

            while (Node)
            {
                auto conn = reinterpret_cast<Lua_Objects::RobloxConnection_t*>(
                    lua_newuserdatatagged(ls, sizeof(Lua_Objects::RobloxConnection_t), 72)
                    );
                if (Node->storage && Node->storage->vftable == funcScrSlotvft ||
                    Node->storage->vftable == waitvft ||
                    Node->storage->vftable == oncevft ||
                    Node->storage->vftable == connectparalellvft)
                {
                    *conn = Lua_Objects::RobloxConnection_t(Node, true);
                }
                else
                {
                    *conn = Lua_Objects::RobloxConnection_t(Node, false);
                }

                lua_newtable(ls);
                lua_pushcfunction(ls, mt_index, nullptr);
                lua_setfield(ls, -2, ("__index"));
                lua_pushcfunction(ls, mt_newindex, nullptr);
                lua_setfield(ls, -2, ("__newindex"));
                lua_pushstring(ls, "Event");
                lua_setfield(ls, -2, ("__type"));
                lua_setmetatable(ls, -2);

                lua_rawseti(ls, -2, idx++);
                Node = Node->next;
            }

            return 1;
        }



    }

    namespace Input {
        bool RobloxActive() {
            return (GetForegroundWindow() == FindWindowA(NULL, "Roblox"));
        };

        int isrbxactive(lua_State* LS)
        {
            lua_pushboolean(LS, RobloxActive());

            return 1;
        };

        int keypress(lua_State* L) {
            L, 1, 1, luaL_checktype(L, 1, LUA_TNUMBER);
            UINT key = lua_tointeger(L, 1);

            if (RobloxActive())
                keybd_event(0, (BYTE)MapVirtualKeyA(key, MAPVK_VK_TO_VSC), KEYEVENTF_SCANCODE, 0);

            return 0;
        };

        std::int32_t keytap(lua_State* L) {
            L, 1, 1, luaL_checktype(L, 1, LUA_TNUMBER);
            UINT key = lua_tointeger(L, 1);

            if (!RobloxActive())
                return 0;

            keybd_event(0, MapVirtualKeyA(key, MAPVK_VK_TO_VSC), KEYEVENTF_SCANCODE, 0);
            keybd_event(0, MapVirtualKeyA(key, MAPVK_VK_TO_VSC), KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP, 0);

            return 0;
        };

        int keyrelease(lua_State* L) {
            luaL_checktype(L, 1, LUA_TNUMBER);
            UINT key = lua_tointeger(L, 1);

            if (RobloxActive())
                keybd_event(0, (BYTE)MapVirtualKeyA(key, MAPVK_VK_TO_VSC), KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP, 0);

            return 0;
        };

        int mouse1click(lua_State* L) {
            if (RobloxActive())
                mouse_event(MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);

            return 0;
        };

        int mouse1press(lua_State* L) {

            if (RobloxActive())
                mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);

            return 0;
        };

        int mouse1release(lua_State* L) {

            if (RobloxActive())
                mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);

            return 0;
        };

        int mouse2click(lua_State* L) {

            if (RobloxActive())
                mouse_event(MOUSEEVENTF_RIGHTDOWN | MOUSEEVENTF_RIGHTUP, 0, 0, 0, 0);

            return 0;
        };

        int mouse2press(lua_State* L)
        {
            if (RobloxActive())
                mouse_event(MOUSEEVENTF_RIGHTDOWN, 0, 0, 0, 0);

            return 0;
        };

        int mouse2release(lua_State* L) {

            if (RobloxActive())
                mouse_event(MOUSEEVENTF_RIGHTUP, 0, 0, 0, 0);

            return 0;
        };

        int mousemoveabs(lua_State* L) {
            luaL_checktype(L, 1, LUA_TNUMBER);
            luaL_checktype(L, 2, LUA_TNUMBER);

            int x = lua_tointeger(L, 1);
            int y = lua_tointeger(L, 2);

            if (!RobloxActive())
                return 0;

            int width = GetSystemMetrics(SM_CXSCREEN) - 1;
            int height = GetSystemMetrics(SM_CYSCREEN) - 1;

            RECT CRect;
            GetClientRect(GetForegroundWindow(), &CRect);

            POINT Point{ CRect.left, CRect.top };
            ClientToScreen(GetForegroundWindow(), &Point);

            x = (x + Point.x) * (65535 / width);
            y = (y + Point.y) * (65535 / height);

            mouse_event(MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE, x, y, 0, 0);
            return 0;
        };

        int mousemoverel(lua_State* L) {
            luaL_checktype(L, 1, LUA_TNUMBER);
            luaL_checktype(L, 2, LUA_TNUMBER);

            int x = lua_tointeger(L, 1);
            int y = lua_tointeger(L, 2);

            if (RobloxActive())
                mouse_event(MOUSEEVENTF_MOVE, x, y, 0, 0);

            return 0;
        };

        int mousescroll(lua_State* L) {
            luaL_checktype(L, 1, LUA_TNUMBER);

            int amt = lua_tointeger(L, 1);

            if (RobloxActive())
                mouse_event(MOUSEEVENTF_WHEEL, 0, 0, amt, 0);

            return 0;
        };
    };
};
namespace GameHooks {

    lua_CFunction OriginalNamecall;
    lua_CFunction OriginalIndex;

    std::vector<const char*> dangerousFunctions =
    {
        "OpenVideosFolder", "OpenScreenshotsFolder",

    };

    int NewNamecall(lua_State* L) {
        if (!L)
            return 0;

        if (L->userdata->Identity >= 8 && L->userdata->Script.expired()) {
            const char* data = L->namecall->data;

            if (strcmp(data, "HttpGet") == 0 || strcmp(data, "HttpGetAsync") == 0) {
                return Yubx::Http::HttpGet(L);
            }

            for (const std::string& func : dangerousFunctions) {
                if (std::string(data) == func) {
                    luaL_error(L, "Function has been disabled for security reasons.");
                    return 0;
                }
            }
        }

        if (!OriginalNamecall)
            return 0;

        return OriginalNamecall(L);
    }

    int NewIndex(lua_State* L) {
        if (!L)
            return 0;

        if (L->userdata->Identity >= 8 && L->userdata->Script.expired()) {
            const char* data = lua_tostring(L, 2);

            if (strcmp(data, "HttpGet") == 0 || strcmp(data, "HttpGetAsync") == 0) {
                lua_getglobal(L, "HttpGet");
                return 1;
            }

            for (const std::string& func : dangerousFunctions) {
                if (std::string(data) == func) {
                    luaL_error(L, "Function has been disabled for security reasons.");
                    return 0;
                }
            }
        }

        if (!OriginalIndex)
            return 0;

        return OriginalIndex(L);
    }

    void InitializeHooks(lua_State* L) {
        int originalCount = lua_gettop(L);

        lua_getglobal(L, "game");
        luaL_getmetafield(L, -1, "__index");

        Closure* Index = clvalue(luaA_toobject(L, -1));
        OriginalIndex = Index->c.f;
        Index->c.f = NewIndex;

        lua_pop(L, 1);

        luaL_getmetafield(L, -1, "__namecall");

        Closure* Namecall = clvalue(luaA_toobject(L, -1));
        OriginalNamecall = Namecall->c.f;
        Namecall->c.f = NewNamecall;

        lua_settop(L, originalCount);
    }

};

void CEnvironment::Initialize(lua_State* LS) {
    NewFunction(LS, "getgenv", Yubx::getgenv);
    NewFunction(LS, "gettenv", Yubx::gettenv);
    NewFunction(LS, "getgc", Yubx::getgc);


    // Miscellaneous
    NewFunction(LS, "loadstring", Yubx::loadstring);
    // NewFunction(LS, "filtergc", Yubx::filtergc);
    NewFunction(LS, "getconnections", Yubx::Miscellaneous::getconnections);

    NewFunction(LS, "identifyexecutor", Yubx::Miscellaneous::identifyexecutor);
    NewFunction(LS, "getexecutorname", Yubx::Miscellaneous::getexecutorname);
    NewFunction(LS, "setfpscap", Yubx::Miscellaneous::setfps);
    NewFunction(LS, "getfpscap", Yubx::Miscellaneous::getfps);
    NewFunction(LS, "lz4compress", Yubx::Miscellaneous::lz4compress);
    NewFunction(LS, "lz4decompress", Yubx::Miscellaneous::lz4decompress);
    NewFunction(LS, "messagebox", Yubx::Miscellaneous::messagebox);
    NewFunction(LS, "setclipboard", Yubx::Miscellaneous::setclipboard);
    NewFunction(LS, "toclipboard", Yubx::Miscellaneous::setclipboard);
    NewFunction(LS, "queue_on_teleport", Yubx::Miscellaneous::queue_on_teleport);
    NewFunction(LS, "queueonteleport", Yubx::Miscellaneous::queue_on_teleport);

    NewFunction(LS, "getinstances", Yubx::Instances::getinstances);
    NewFunction(LS, "getnilinstances", Yubx::Instances::getnilinstances);
    NewFunction(LS, "getthreadcontext", Yubx::Instances::getthreadidentity);
    NewFunction(LS, "getidentity", Yubx::Instances::getthreadidentity);
    NewFunction(LS, "setidentity", Yubx::Instances::setthreadidentity);
    NewFunction(LS, "setthreadcontext", Yubx::Instances::setthreadidentity);

    NewFunction(LS, "getthreadidentity", Yubx::Instances::getthreadidentity);
    NewFunction(LS, "setthreadidentity", Yubx::Instances::setthreadidentity);

    NewFunction(LS, "fireclickdetector", Yubx::Instances::fireclickdetector);
    NewFunction(LS, "firetouchinterest", Yubx::Instances::firetouchinterest);
    NewFunction(LS, "fireproximityprompt", Yubx::Instances::fireproximityprompt);
    NewFunction(LS, "gethui", Yubx::Instances::gethui);

    NewFunction(LS, "getcallbackvalue", Yubx::Instances::getcallbackvalue);

    NewFunction(LS, "getscriptclosure", Yubx::Script::getscriptclosure);
    NewFunction(LS, "getscriptfunction", Yubx::Script::getscriptclosure);
    NewFunction(LS, "getscriptbytecode", Yubx::Script::getscriptbytecode);
    NewFunction(LS, "dumpstring", Yubx::Script::getscriptbytecode);

    NewFunction(LS, "isrbxactive", Yubx::Input::isrbxactive);
    NewFunction(LS, "isgameactive", Yubx::Input::isrbxactive);
    NewFunction(LS, "keypress", Yubx::Input::keypress);
    NewFunction(LS, "keytap", Yubx::Input::keytap);
    NewFunction(LS, "keyrelease", Yubx::Input::keyrelease);
    NewFunction(LS, "mouse1click", Yubx::Input::mouse1click);
    NewFunction(LS, "mouse1press", Yubx::Input::mouse1press);
    NewFunction(LS, "mouse1release", Yubx::Input::mouse1release);
    NewFunction(LS, "mouse2click", Yubx::Input::mouse2click);
    NewFunction(LS, "mouse2press", Yubx::Input::mouse2press);
    NewFunction(LS, "mouse2release", Yubx::Input::mouse2release);
    NewFunction(LS, "mousemoveabs", Yubx::Input::mousemoveabs);
    NewFunction(LS, "mousemoverel", Yubx::Input::mousemoverel);
    NewFunction(LS, "mousescroll", Yubx::Input::mousescroll);

    lua_newtable(LS);
    NewTableFunction(LS, "invalidate", Yubx::Cache::invalidate);
    NewTableFunction(LS, "iscached", Yubx::Cache::iscached);
    NewTableFunction(LS, "replace", Yubx::Cache::replace);
    lua_setfield(LS, LUA_GLOBALSINDEX, ("cache"));

    NewFunction(LS, "cloneref", Yubx::Cache::cloneref);
    NewFunction(LS, "compareinstances", Yubx::Cache::compareinstances);

    NewFunction(LS, "getrawmetatable", Yubx::Metatable::getrawmetatable);
    NewFunction(LS, "setrawmetatable", Yubx::Metatable::setrawmetatable);
    NewFunction(LS, "isreadonly", Yubx::Metatable::isreadonly);
    NewFunction(LS, "setreadonly", Yubx::Metatable::setreadonly);
    NewFunction(LS, "getnamecallmethod", Yubx::Metatable::getnamecallmethod);
    NewFunction(LS, "hookmetamethod", Yubx::Metatable::hookmetamethod);

    NewFunction(LS, "getreg", Yubx::Debug::getreg);
    NewFunction(LS, "getregistry", Yubx::Debug::getreg);
    NewFunction(LS, "getproto", Yubx::Debug::getproto);
    NewFunction(LS, "getprotos", Yubx::Debug::getprotos);
    NewFunction(LS, "getstack", Yubx::Debug::getstack);
    NewFunction(LS, "getinfo", Yubx::Debug::getinfo);
    NewFunction(LS, "getupvalue", Yubx::Debug::getupvalue);
    NewFunction(LS, "getupvalues", Yubx::Debug::getupvalues);
    NewFunction(LS, "getconstant", Yubx::Debug::getconstant);
    NewFunction(LS, "getconstants", Yubx::Debug::getconstants);
    NewFunction(LS, "setconstant", Yubx::Debug::setconstant);
    NewFunction(LS, "setstack", Yubx::Debug::setstack);
    NewFunction(LS, "setupvalue", Yubx::Debug::debug_setupvalue);

    lua_getglobal(LS, "debug");
    lua_getglobal(LS, "setreadonly");
    lua_pushvalue(LS, -2);
    lua_pushboolean(LS, false);
    lua_pcall(LS, 2, 0, 0);

    NewTableFunction(LS, "getreg", Yubx::Debug::getreg);
    NewTableFunction(LS, "getregistry", Yubx::Debug::getreg);
    NewTableFunction(LS, "getproto", Yubx::Debug::getproto);
    NewTableFunction(LS, "getprotos", Yubx::Debug::getprotos);
    NewTableFunction(LS, "getstack", Yubx::Debug::getstack);
    NewTableFunction(LS, "getinfo", Yubx::Debug::getinfo);
    NewTableFunction(LS, "getupvalue", Yubx::Debug::getupvalue);
    NewTableFunction(LS, "getupvalues", Yubx::Debug::getupvalues);
    NewTableFunction(LS, "getconstant", Yubx::Debug::getconstant);
    NewTableFunction(LS, "getconstants", Yubx::Debug::getconstants);
    NewTableFunction(LS, "setconstant", Yubx::Debug::setconstant);
    NewTableFunction(LS, "setstack", Yubx::Debug::setstack);
    NewTableFunction(LS, "setupvalue", Yubx::Debug::debug_setupvalue);

    lua_pop(LS, 1);

    NewFunction(LS, "getcustomasset", Yubx::Filesystem::getcustomasset);
    NewFunction(LS, "writefile", Yubx::Filesystem::writefile);
    NewFunction(LS, "readfile", Yubx::Filesystem::readfile);
    NewFunction(LS, "makefolder", Yubx::Filesystem::makefolder);
    NewFunction(LS, "isfolder", Yubx::Filesystem::isfolder);
    NewFunction(LS, "delfile", Yubx::Filesystem::delfile);
    NewFunction(LS, "appendfile", Yubx::Filesystem::appendfile);
    NewFunction(LS, "delfolder", Yubx::Filesystem::delfolder);
    NewFunction(LS, "isfile", Yubx::Filesystem::isfile);
    NewFunction(LS, "listfiles", Yubx::Filesystem::listfiles);
    NewFunction(LS, "loadfile", Yubx::Filesystem::loadfile);


    NewFunction(LS, "base64_encode", Yubx::Crypt::base64encode);
    NewFunction(LS, "base64_decode", Yubx::Crypt::base64decode);

    lua_newtable(LS);
    NewTableFunction(LS, "encode", Yubx::Crypt::base64encode);
    NewTableFunction(LS, "decode", Yubx::Crypt::base64decode);
    lua_setfield(LS, LUA_GLOBALSINDEX, ("base64"));

    lua_newtable(LS);
    NewTableFunction(LS, "base64encode", Yubx::Crypt::base64encode);
    NewTableFunction(LS, "base64decode", Yubx::Crypt::base64decode);
    NewTableFunction(LS, "base64_encode", Yubx::Crypt::base64encode);
    NewTableFunction(LS, "base64_decode", Yubx::Crypt::base64decode);

    lua_newtable(LS);
    NewTableFunction(LS, "encode", Yubx::Crypt::base64encode);
    NewTableFunction(LS, "decode", Yubx::Crypt::base64decode);
    lua_setfield(LS, -2, ("base64"));

    NewTableFunction(LS, "encrypt", Yubx::Crypt::encrypt);
    NewTableFunction(LS, "decrypt", Yubx::Crypt::decrypt);
    NewTableFunction(LS, "generatebytes", Yubx::Crypt::generatebytes);
    NewTableFunction(LS, "generatekey", Yubx::Crypt::generatekey);
    NewTableFunction(LS, "hash", Yubx::Crypt::hash);
    lua_setfield(LS, LUA_GLOBALSINDEX, ("crypt"));


    NewFunction(LS, "HttpGet", Yubx::Http::HttpGet);
    NewFunction(LS, "request", Yubx::Http::request);
    NewFunction(LS, "http_request", Yubx::Http::request);
    NewFunction(LS, "consolecreate", Yubx::Http::ConsoleCreate);
    NewFunction(LS, "consoleclear", Yubx::Http::ConsoleClear);
    NewFunction(LS, "consoledestroy", Yubx::Http::ConsoleDestroy);
    NewFunction(LS, "consoleprint", Yubx::Http::ConsolePrint);
    NewFunction(LS, "consoleinfo", Yubx::Http::ConsoleInfo);
    NewFunction(LS, "consolewarn", Yubx::Http::ConsoleWarn);
    NewFunction(LS, "consoleerr", Yubx::Http::ConsoleErr);
    NewFunction(LS, "consoleinput", Yubx::Http::ConsoleInput);
    NewFunction(LS, "consoleinputasync", Yubx::Http::ConsoleInput);
    NewFunction(LS, "consolename", Yubx::Http::ConsoleName);
    NewFunction(LS, "consolesettitle", Yubx::Http::ConsoleName);

    NewFunction(LS, "rconsolecreate", Yubx::Http::ConsoleCreate);
    NewFunction(LS, "rconsoleclear", Yubx::Http::ConsoleClear);
    NewFunction(LS, "rconsoledestroy", Yubx::Http::ConsoleDestroy);
    NewFunction(LS, "rconsoleprint", Yubx::Http::ConsolePrint);
    NewFunction(LS, "rconsoleinfo", Yubx::Http::ConsoleInfo);
    NewFunction(LS, "rconsolewarn", Yubx::Http::ConsoleWarn);
    NewFunction(LS, "rconsoleerr", Yubx::Http::ConsoleErr);
    NewFunction(LS, "rconsoleinput", Yubx::Http::ConsoleInput);
    NewFunction(LS, "rconsoleinputasync", Yubx::Http::ConsoleInput);
    NewFunction(LS, "rconsolename", Yubx::Http::ConsoleName);
    NewFunction(LS, "rconsolesettitle", Yubx::Http::ConsoleName);


    GameHooks::InitializeHooks(LS);


    lua_newtable(LS);
    lua_setglobal(LS, "_G");

    lua_newtable(LS);
    lua_setglobal(LS, "shared");

    lua_getglobal(LS, "game");
    lua_getfield(LS, -1, "GetService");
    lua_pushvalue(LS, -2);
    lua_pushstring(LS, "CoreGui");
    lua_call(LS, 2, 1);

    lua_getglobal(LS, "cloneref");
    lua_insert(LS, -2);
    lua_call(LS, 1, 1);

    lua_setglobal(LS, "__hiddeninterface");
};
