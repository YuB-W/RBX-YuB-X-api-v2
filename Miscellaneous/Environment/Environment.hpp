#pragma once

#include "Includes.hpp"
#include "Scheduler.hpp"
#include "cpr/cpr.h"
#include "cpr/cprtypes.h"
#include "HttpStatus/HttpStatus.hpp" 
#include "nlohmann/json.hpp"
#include "lz4/lz4.h"

#include <cryptopp/aes.h>
#include <cryptopp/rsa.h>
#include <cryptopp/gcm.h>
#include <cryptopp/eax.h>
#include <cryptopp/md2.h>
#include <cryptopp/md5.h>
#include <cryptopp/sha.h>
#include <cryptopp/sha3.h>
#include <cryptopp/osrng.h>
#include <cryptopp/hex.h>
#include <cryptopp/pssr.h>
#include <cryptopp/base64.h>
#include <cryptopp/modes.h>
#include <cryptopp/filters.h>
#include <cryptopp/serpent.h>
#include <cryptopp/pwdbased.h>
#include <cryptopp/blowfish.h>
#include <cryptopp/rdrand.h>

using json = nlohmann::json;

static std::string Name = "YubxInternal";
static std::string Version = "3.5";

namespace HelpFuncs {
	using YieldReturn = std::function<int(lua_State* L)>;

	static void ThreadFunc(const std::function<YieldReturn()>& YieldedFunction, lua_State* L)
	{
		YieldReturn ret_func;

		try
		{
			ret_func = YieldedFunction();
		}
		catch (std::exception ex)
		{
			lua_pushstring(L, ex.what());
			lua_error(L);
		}

		lua_State* l_new = lua_newthread(L);

		const auto returns = ret_func(L);

		lua_getglobal(l_new, ("task"));
		lua_getfield(l_new, -1, ("defer"));

		lua_pushthread(L);
		lua_xmove(L, l_new, 1);

		for (int i = returns; i >= 1; i--)
		{
			lua_pushvalue(L, -i);
			lua_xmove(L, l_new, 1);
		}

		lua_pcall(l_new, returns + 1, 0, 0);
		lua_settop(l_new, 0);
	}

	static int YieldExecution(lua_State* L, const std::function<YieldReturn()>& YieldedFunction)
	{
		lua_pushthread(L);
		lua_ref(L, -1);
		lua_pop(L, 1);

		std::thread(ThreadFunc, YieldedFunction, L).detach();

		L->base = L->top;
		L->status = LUA_YIELD;

		L->ci->flags |= 1;
		return -1;
	}

	static void IsInstance(lua_State* L, int idx) {
		std::string typeoff = luaL_typename(L, idx);
		if (typeoff != ("Instance"))
			luaL_typeerrorL(L, 1, ("Instance"));
	};

	static bool IsClassName(lua_State* L, int idx, std::string className) {
		int originalArgCount = lua_gettop(L);

		if (lua_isnil(L, idx)) {
			return false;
		}

		lua_getglobal(L, ("typeof"));
		lua_pushvalue(L, idx);
		lua_pcall(L, 1, 1, 0);

		std::string resultType = luaL_checklstring(L, -1, nullptr);
		lua_pop(L, lua_gettop(L) - originalArgCount);

		if (resultType != ("Instance")) {
			return false;
		}

		lua_getfield(L, idx, "ClassName");
		std::string object_ClassName = luaL_checklstring(L, -1, nullptr);
		lua_pop(L, lua_gettop(L) - originalArgCount);

		lua_getfield(L, idx, ("IsA"));
		lua_pushvalue(L, idx);
		lua_pushlstring(L, className.data(), className.size());
		lua_pcall(L, 2, 1, 0);

		bool isAResult = lua_isboolean(L, -1) ? lua_toboolean(L, -1) : false;
		lua_pop(L, lua_gettop(L) - originalArgCount);

		if (!isAResult & object_ClassName != className)
			return false;

		return true;
	};

	static int GetEveryInstance(lua_State* L)
	{
		lua_pushvalue(L, LUA_REGISTRYINDEX);
		lua_pushlightuserdata(L, (void*)Roblox::PushInstance);
		lua_gettable(L, -2);
		return 1;
	};

	static uintptr_t GetPlaceId() {
		return 0;
	}

	static uintptr_t GetGameId() {
		return 0;
	}

	static std::string DecompressBytecode(const std::string_view compressed) {
		const uint8_t bytecodeSignature[4] = { 'R', 'S', 'B', '1' };
		const int bytecodeHashMultiplier = 41;
		const int bytecodeHashSeed = 42;

		if (compressed.size() < 8)
			return "invalid bytecode: size is less than 8";

		std::vector<uint8_t> compressedData(compressed.begin(), compressed.end());
		std::vector<uint8_t> headerBuffer(4);

		for (size_t i = 0; i < 4; ++i) {
			headerBuffer[i] = compressedData[i] ^ bytecodeSignature[i];
			headerBuffer[i] = (headerBuffer[i] - i * bytecodeHashMultiplier) % 256;
		}

		for (size_t i = 0; i < compressedData.size(); ++i) {
			compressedData[i] ^= (headerBuffer[i % 4] + i * bytecodeHashMultiplier) % 256;
		}

		uint32_t hashValue = 0;
		for (size_t i = 0; i < 4; ++i) {
			hashValue |= headerBuffer[i] << (i * 8);
		}

		uint32_t rehash = XXH32(compressedData.data(), compressedData.size(), bytecodeHashSeed);
		if (rehash != hashValue)
			return "";

		uint32_t decompressedSize = 0;
		for (size_t i = 4; i < 8; ++i) {
			decompressedSize |= compressedData[i] << ((i - 4) * 8);
		}

		compressedData = std::vector<uint8_t>(compressedData.begin() + 8, compressedData.end());
		std::vector<uint8_t> decompressed(decompressedSize);

		size_t const actualDecompressedSize = ZSTD_decompress(decompressed.data(), decompressedSize, compressedData.data(), compressedData.size());
		if (ZSTD_isError(actualDecompressedSize))
			return "zstd decompress error: " + std::string(ZSTD_getErrorName(actualDecompressedSize));

		decompressed.resize(actualDecompressedSize);
		return std::string(decompressed.begin(), decompressed.end());
	}

	static std::string GetBytecode(std::uint64_t Address) {
		uintptr_t str = Address + 0x10;
		uintptr_t data;

		if (*reinterpret_cast<std::size_t*>(str + 0x18) > 0xf) {
			data = *reinterpret_cast<uintptr_t*>(str);
		}
		else {
			data = str;
		}

		std::string ee;
		std::size_t len = *reinterpret_cast<std::size_t*>(str + 0x10);
		ee.reserve(len);

		for (unsigned i = 0; i < len; i++) {
			ee += *reinterpret_cast<char*>(data + i);
		}

		return ee;
	}

	static std::string RequestBytecode(uintptr_t scriptPtr, bool Decompress) {
		uintptr_t temp[0x4];
		std::memset(temp, 0, sizeof(temp));

		Roblox::RequestCode((uintptr_t)temp, scriptPtr);

		uintptr_t bytecodePtr = temp[1];

		if (!bytecodePtr) {
			return "Nil";
		}

		std::string Compressed = GetBytecode(bytecodePtr);
		if (Compressed.size() <= 8) {
			return "Nil";
		}

		if (!Decompress)
		{
			return Compressed;
		}
		else
		{
			std::string Decompressed = DecompressBytecode(Compressed);
			if (Decompressed.size() <= 8) {
				return "Nil";
			}

			return Decompressed;
		}
	}

	static void SetNewIdentity(lua_State* LS, int Identity) {
		LS->userdata->Identity = Identity;

		std::int64_t Ignore[128];
		Roblox::Impersonator(Ignore, &Identity, *(__int64*)((uintptr_t)LS->userdata + Offsets::ExtraSpace::Capabilities));
	}

}

namespace Handler
{
	inline std::unordered_map<Closure*, Closure*> Newcclosures = {};

	inline std::unordered_map<Closure*, Closure*> HookedFunctions = {};

	inline std::map<Closure*, lua_CFunction> ExecutorClosures = {};

	inline std::unordered_set<Closure*> ExecutorFunctions = {};

	static int ClosuresHandler(lua_State* L)
	{
		auto found = ExecutorClosures.find(curr_func(L));

		if (found != ExecutorClosures.end())
		{
			return found->second(L);
		}

		return 0;
	}

	static lua_CFunction GetClosure(Closure* Closure)
	{
		return ExecutorClosures[Closure];
	}

	static void SetClosure(Closure* Closure, lua_CFunction Function)
	{
		ExecutorClosures[Closure] = Function;
	}


	static void PushClosure(lua_State* L, lua_CFunction Function, const char* debugname, int nup)
	{
		lua_pushcclosurek(L, ClosuresHandler, debugname, nup, 0);
		Closure* closure = *reinterpret_cast<Closure**>(index2addr(L, -1));
		ExecutorClosures[closure] = Function;
	}

	static void PushWrappedClosure(lua_State* L, lua_CFunction Function, const char* debugname, int nup, lua_Continuation count)
	{
		lua_pushcclosurek(L, ClosuresHandler, debugname, nup, count);
		Closure* closure = *reinterpret_cast<Closure**>(index2addr(L, -1));
		ExecutorClosures[closure] = Function;
		Handler::ExecutorFunctions.insert(closure);
		lua_ref(L, -1);
	}


	namespace Wraps
	{
		static Closure* GetClosure(Closure* c)
		{
			return Newcclosures.find(c)->second;
		}

		static void SetClosure(Closure* c, Closure* l)
		{
			Newcclosures[c] = l;
		}
	}
}

static void NewFunction(lua_State* L, const char* globalname, lua_CFunction function)
{
	Handler::PushClosure(L, function, nullptr, 0);
	Handler::ExecutorFunctions.insert(*reinterpret_cast<Closure**>(index2addr(L, -1)));
	lua_setfield(L, LUA_GLOBALSINDEX, globalname);
}

static void NewTableFunction(lua_State* L, const char* globalname, lua_CFunction function)
{
	Handler::PushClosure(L, function, nullptr, 0);
	Handler::ExecutorFunctions.insert(*reinterpret_cast<Closure**>(index2addr(L, -1)));
	lua_setfield(L, -2, globalname);
}

static bool IsMetamethod(const char* Metamethod)
{
	if (std::string(Metamethod).empty())
		return false;

	const std::unordered_set<std::string> Allowed = {
		"__namecall",
		"__newindex",
		"__index"
	};
	return Allowed.find(Metamethod) != Allowed.end();
};

class CEnvironment {
public:
	void Initialize(lua_State* LS);
};

inline auto Environment = std::make_unique<CEnvironment>();