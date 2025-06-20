#pragma once

#include "Includes.hpp"
#include "Offsets.hpp"

class SchedulerClass {
public:
    lua_State* ExecutorState = nullptr;
    lua_State* GetLuaState();

    uintptr_t get_data_model();
    uintptr_t get_script_context();

    uintptr_t rawscheduler()
    {
        return *(uintptr_t*)Offsets::RawScheduler;
    }
};

inline SchedulerClass* GetScheduler() {
    static SchedulerClass sched;
    return &sched;
}

inline auto Scheduler = std::make_unique<SchedulerClass>();