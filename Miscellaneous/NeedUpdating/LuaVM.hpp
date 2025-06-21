#pragma once

#include <Windows.h>
#include <Offsets.hpp>

#define REBASE(x) x + (uintptr_t)GetModuleHandle(nullptr)

template <typename T>
struct vmval1
{
private:
    T Storage;
public:
    operator const T() const {
        return (T)((uintptr_t)this->Storage - (uintptr_t)this);
    }

    void operator=(const T& Value) {
        this->Storage = (T)((uintptr_t)Value + (uintptr_t)this);
    }

    const T operator->() const {
        return operator const T();
    }

    T Get() {
        return operator const T();
    }

    void Set(const T& Value) {
        operator=(Value);
    }
};

template <typename T>
struct vmval2
{
private:
    T Storage;
public:
    operator const T() const {
        return (T)((uintptr_t)this - (uintptr_t)this->Storage);
    }

    void operator=(const T& Value) {
        this->Storage = (T)((uintptr_t)this - (uintptr_t)Value);
    }

    const T operator->() const {
        return operator const T();
    }

    T Get() {
        return operator const T();
    }

    void Set(const T& Value) {
        operator=(Value);
    }
};

template <typename T>
struct vmval3
{
private:
    T Storage;
public:
    operator const T() const {
        return (T)((uintptr_t)this ^ (uintptr_t)this->Storage);
    }

    void operator=(const T& Value) {
        this->Storage = (T)((uintptr_t)Value ^ (uintptr_t)this);
    }

    const T operator->() const {
        return operator const T();
    }

    T Get() {
        return operator const T();
    }

    void Set(const T& Value) {
        operator=(Value);
    }
};

template <typename T>
struct vmval4
{
private:
    T Storage;
public:
    operator const T() const {
        return (T)((uintptr_t)this + (uintptr_t)this->Storage);
    }

    void operator=(const T& Value) {
        this->Storage = (T)((uintptr_t)Value - (uintptr_t)this);
    }

    const T operator->() const {
        return operator const T();
    }

    T Get() {
        return operator const T();
    }

    void Set(const T& Value) {
        operator=(Value);
    }
};

template<typename T>
struct vmval0
{
private:
    T storage;

public:
    operator const T() const
    {
        return storage;
    }

    void operator=(const T& value)
    {
        storage = value;
    }

    const T operator->() const
    {
        return operator const T();
    }

    T Get()
    {
        return operator const T();
    }

    void Set(const T& value)
    {
        operator=(value);
    }
};

#define LUAU_COMMA_SEP ,
#define LUAU_SEMICOLON_SEP ;

#define LUAU_SHUFFLE3(s, a1, a2, a3) a1 s a3 s a2
#define LUAU_SHUFFLE4(s, a1, a2, a3, a4) a3 s a1 s a2 s a4
#define LUAU_SHUFFLE5(s, a1, a2, a3, a4, a5) a5 s a4 s a1 s a3 s a2
#define LUAU_SHUFFLE6(s, a1, a2, a3, a4, a5, a6) a2 s a4 s a3 s a1 s a5 s a6
#define LUAU_SHUFFLE7(s, a1, a2, a3, a4, a5, a6, a7) a4 s a5 s a1 s a7 s a2 s a3 s a6
#define LUAU_SHUFFLE8(s, a1, a2, a3, a4, a5, a6, a7, a8) a6 s a4 s a1 s a2 s a8 s a5 s a7 s a3
#define LUAU_SHUFFLE9(s, a1, a2, a3, a4, a5, a6, a7, a8, a9) a1 s a8 s a5 s a3 s a4 s a6 s a7 s a9 s a2

#define PROTO_MEMBER1_ENC vmval0
#define PROTO_MEMBER2_ENC vmval4
#define PROTO_DEBUGISN_ENC vmval3
#define PROTO_TYPEINFO_ENC vmval2
#define PROTO_DEBUGNAME_ENC vmval1

#define LSTATE_STACKSIZE_ENC vmval2
#define LSTATE_GLOBAL_ENC vmval0

#define CLOSURE_FUNC_ENC vmval0
#define CLOSURE_CONT_ENC vmval3
#define CLOSURE_DEBUGNAME_ENC vmval4

#define TABLE_MEMBER_ENC vmval0
#define TABLE_META_ENC vmval0

#define UDATA_META_ENC vmval3

#define TSTRING_HASH_ENC vmval1
#define TSTRING_LEN_ENC vmval0

#define GSTATE_TTNAME_ENC vmval0
#define GSTATE_TMNAME_ENC vmval0



namespace LuaVM {
    const uintptr_t LuaO_NilObject = Offsets::LuaO_NilObject;
    const uintptr_t LuaH_DummyNode = Offsets::LuaH_DummyNode;
    const uintptr_t Luau_Execute   = Offsets::Luau_Execute;
    const uintptr_t LuaD_Throw     = Offsets::LuaD_throw;
}
