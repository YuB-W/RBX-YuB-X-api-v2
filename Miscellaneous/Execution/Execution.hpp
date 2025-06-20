#pragma once

#include "Includes.hpp"

#include <Luau/Compiler.h>
#include <Luau/BytecodeBuilder.h>
#include <Luau/BytecodeUtils.h>
#include <Luau/Bytecode.h>

#include "Offsets.hpp"

static class BytecodeEncoderClass : public Luau::BytecodeEncoder {
    inline void encode(uint32_t* data, size_t count) override
    {
        for (auto i = 0u; i < count;) {

            auto& opcode = *(uint8_t*)(data + i);

            i += Luau::getOpLength(LuauOpcode(opcode));

            opcode *= 227;
        }
    }
};

static BytecodeEncoderClass Encoder;

static uintptr_t MaxCaps = 0xFFFFFFFFFFFFFFFF;

class CExecution {
private:
public:
    std::string CompileScript(const std::string Source);
    void Execute(std::string Source);
    void ElevateProtoCapabilities(Proto* Proto);
};

inline auto Execution = std::make_unique<CExecution>();
