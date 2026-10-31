#include "config.hpp"
#include <cstdlib>
#include <cstring>
#include <optional>
#include <print>
#include <steam/isteamnetworkingutils.h>
#include <typeinfo>

template<typename T>
bool ParseConfigValue(const char *value, T &out);

template<>
bool ParseConfigValue(const char *value, const char *&out)
{
	out = value;
	return true;
}

template<>
bool ParseConfigValue(const char *value, SteamNetworkingIPAddr &out)
{
	out.Clear();
	return out.ParseString(value) && out.m_port != 0;
}

template<typename T>
bool ParseConfigValue(const char *value, std::optional<T> &out)
{
	T result = {};
	if (!ParseConfigValue(value, result))
		return false;

	out = result;
	return true;
}

template<typename T>
void ClearConfigValue(T &out);

template<>
void ClearConfigValue(const char *&out)
{
	out = nullptr;
}

template<typename T>
void ClearConfigValue(std::optional<T> &out)
{
	out = std::nullopt;
}

template<typename T>
bool GetEnv(const char *name, T &out)
{
	const char *raw = std::getenv(name);
	if (!raw || raw[0] == '\0')
		return true; // Not set or empty; keep default

	return ParseConfigValue(raw, out);
}

enum class EArgResult
{
	NotMatched,
	Parsed,
	Failed,
};

template<typename T>
EArgResult TryParseArg(const char *argname, int &index, int argc, char **argv, T &out)
{
	if (std::strcmp(argname, argv[index]) != 0)
		return EArgResult::NotMatched;

	if (index + 1 >= argc)
		return EArgResult::Failed;

	const char *val = argv[++index];
	if (val[0] == '\0')
	{
		ClearConfigValue(out);
		return EArgResult::Parsed;
	}

	if (!ParseConfigValue(val, out))
		return EArgResult::Failed;

	return EArgResult::Parsed;
}

#define GET_ENV(v, type, envname)                                                   \
	do                                                                              \
	{                                                                               \
		if (!GetEnv(envname, v))                                                    \
		{                                                                           \
			std::println("Failed to parse " envname " as {}", typeid(type).name()); \
			return false;                                                           \
		}                                                                           \
	} while (false)

#define GET_ARG(v, type, argname)                                                   \
	do                                                                              \
	{                                                                               \
		auto res = TryParseArg(argname, i, argc, argv, v);                          \
		if (res == EArgResult::Parsed)                                              \
			continue;                                                               \
		if (res == EArgResult::Failed)                                              \
		{                                                                           \
			std::println("Failed to parse " argname " as {}", typeid(type).name()); \
			return false;                                                           \
		}                                                                           \
	} while (false)

bool CConfig::Init(int argc, char **argv)
{
	GET_ENV(dbpath, const char *, "DB_PATH");
	GET_ENV(listenaddress, SteamNetworkingIPAddr, "LISTEN_ADDRESS");
	GET_ENV(cachebypasspassword, const char *, "CACHE_BYPASS_PASSWORD");

	for (int i = 1; i < argc; i++)
	{
		GET_ARG(dbpath, const char *, "-dbpath");
		GET_ARG(listenaddress, SteamNetworkingIPAddr, "-listen");
		GET_ARG(cachebypasspassword, const char *, "-cachebypasspassword");
	}

	return true;
}

CConfig &Config()
{
	static CConfig s_Config;
	return s_Config;
}
