#include <Scheduler.hpp>
#include "Execution.hpp"



uintptr_t SchedulerClass::get_data_model()
{
    uintptr_t fakedm = *(uintptr_t*)Offsets::DataModel::FakeDataModelPointer;

    return *(uintptr_t*)(fakedm + Offsets::DataModel::FakeDataModelToDataModel);
}

uintptr_t SchedulerClass::get_script_context()
{
    uintptr_t children = *(uintptr_t*)(*(uintptr_t*)(get_data_model() + 0x80));

    return *(uintptr_t*)(children + 0x3B0);
}

lua_State* SchedulerClass::GetLuaState()
{
    if (!ExecutorState)
    {
        int32_t ignore1 = 2;
        uintptr_t ignore2 = { 0 };
        constexpr uintptr_t DisableRequireLock = 0x6E0;

        *reinterpret_cast<BYTE*>(*(uintptr_t*)(**(uintptr_t**)(*(uintptr_t*)(*(uintptr_t*)(Offsets::DataModel::FakeDataModelPointer)+Offsets::DataModel::FakeDataModelToDataModel) + Offsets::DataModel::Children) + Offsets::LuaUserData::ScriptContext) + DisableRequireLock) = TRUE;
        uintptr_t Ls = Roblox::GetState(*(uintptr_t*)(**(uintptr_t**)(*(uintptr_t*)(*(uintptr_t*)(Offsets::DataModel::FakeDataModelPointer)+Offsets::DataModel::FakeDataModelToDataModel) + Offsets::DataModel::Children) + Offsets::LuaUserData::ScriptContext) + Offsets::LuaUserData::GlobalState, &ignore1, &ignore2);
        if (!Ls)
        {
            Roblox::Print(3, "Error getting Obfuscated LuaState!");
            return 0;
        }
        ExecutorState = lua_newthread((lua_State*)Roblox::DecryptState(Ls + Offsets::LuaUserData::DecryptState));
    }

    return ExecutorState;
}