#include "Execution.hpp"
#include "Scheduler.hpp"

std::string CExecution::CompileScript(const std::string Source) { 

    std::string Bytecode = Luau::compile(Source, { 2, 1, 2 }, { true, true }, &Encoder);

    size_t DataSize = Bytecode.size();
    size_t MaxSize = ZSTD_compressBound(DataSize);
    std::vector<char> Buffer(MaxSize + 8);

    memcpy(Buffer.data(), "RSB1", 4);
    memcpy(Buffer.data() + 4, &DataSize, sizeof(DataSize));

    size_t CompressedSize = ZSTD_compress(Buffer.data() + 8, MaxSize, Bytecode.data(), DataSize, ZSTD_maxCLevel());
    size_t TotalSize = CompressedSize + 8;

    uint32_t Key = XXH32(Buffer.data(), TotalSize, 42);
    uint8_t* KeyBytes = (uint8_t*)&Key;

    for (size_t i = 0; i < TotalSize; ++i) Buffer[i] ^= KeyBytes[i % 4] + i * 41;

    return std::string(Buffer.data(), TotalSize);
}

void CExecution::ElevateProtoCapabilities(Proto* Proto) {
    Proto->userdata = &MaxCaps;
    for (int i = 0; i < Proto->sizep; i++)
    {
        ElevateProtoCapabilities(Proto->p[i]);
    }
}

void CExecution::Execute(std::string Source)
{
    if (Source.empty()) return;

    auto LuaState = Scheduler->GetLuaState();

    const auto OriginalTop = lua_gettop(LuaState);

    const auto FunctionThread = lua_newthread(LuaState);
    lua_pop(LuaState, -1);

    auto Bytecode = CompileScript("script = Instance.new('LocalScript');" + Source);

    if (Roblox::LuaVMLoad((int64_t)FunctionThread, &Bytecode, "@YubxInternal", 0) != LUA_OK)
    {
        return;
    }

    Closure* Function = lua_toclosure(FunctionThread, -1);
    if (Function && Function->l.p)
    {
        ElevateProtoCapabilities(Function->l.p);
    }

    lua_getglobal(LuaState, "task");
    lua_getfield(LuaState, -1, "defer");
    lua_remove(LuaState, -2);
    lua_xmove(FunctionThread, LuaState, 1);
    lua_pcall(LuaState, 1, 0, 0);


    lua_settop(FunctionThread, 0);
    lua_settop(LuaState, OriginalTop);
}