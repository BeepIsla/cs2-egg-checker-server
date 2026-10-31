#include "asserts.hpp"
#include "config.hpp"
#include "db.hpp"
#include "gcclient.hpp"
#include "helpers.hpp"
#include "server.hpp"
#include <atomic>
#include <base_gcmessages.pb.h>
#include <connectionless_netmessages.pb.h>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstrike15_gcmessages.pb.h>
#include <gcsdk_gcmessages.pb.h>
#include <gcsystemmsgs.pb.h>
#include <network_connection.pb.h>
#include <optional>
#include <print>
#include <steam/isteamgamecoordinator.h>
#include <steam/steam_gameserver.h>
#include <steam/steamnetworkingtypes.h>
#include <steammessages.pb.h>
#include <thread>

static std::atomic<bool> s_bQuit = false;

void SignalHandler(int signal)
{
	if (signal == SIGINT || signal == SIGTERM)
		s_bQuit = true;
}

static void OnSteamNetworkingSocketsDebugOutput(ESteamNetworkingSocketsDebugOutputType nType, const char *pszMsg)
{
	static constexpr const char *DebugOutputLevels[] = {
	    "None",
	    "Bug",
	    "Error",
	    "Important",
	    "Warning",
	    "Msg",
	    "Verbose",
	    "Debug",
	    "Everything",
	};
	static constexpr size_t DebugOutputLevelSize = sizeof(DebugOutputLevels) / sizeof(*DebugOutputLevels);
	if (nType < 0 || nType >= DebugOutputLevelSize)
		std::println("[SteamNetworkingSockets :: {}] {}", std::to_underlying(nType), pszMsg);
	else
		std::println("[SteamNetworkingSockets :: {}] {}", DebugOutputLevels[nType], pszMsg);
}

int main(int argc, char **argv)
{
	std::setvbuf(stdout, nullptr, _IONBF, 0);

// We first do this to initialise the server
#ifdef __linux__
#define _putenv putenv
#define putenv_cast(x) (char *)(x)
#else
#define putenv_cast(x) x
#endif
	_putenv(putenv_cast("SteamAppId=730"));
	_putenv(putenv_cast("SteamGameId=730"));

	SteamErrMsg err;
	RELEASE_ASSERT_FMT(
	    SteamGameServer_InitEx(0, 27015, STEAMGAMESERVER_QUERY_PORT_SHARED, eServerModeAuthentication, "0.0.0.0", &err) == k_ESteamAPIInitResult_OK,
	    "Failed to initialise Steam: {}", err
	);
	CAutoRelease steamShutdown([]() {
		SteamGameServer_Shutdown();
	});

#ifdef DEBUG
	SteamNetworkingUtils()->SetDebugOutputFunction(k_ESteamNetworkingSocketsDebugOutputType_Verbose, OnSteamNetworkingSocketsDebugOutput);
#else
	SteamNetworkingUtils()->SetDebugOutputFunction(k_ESteamNetworkingSocketsDebugOutputType_Msg, OnSteamNetworkingSocketsDebugOutput);
#endif

	// SteamNetworkingIPAddr parser relies on ISteamNetworkingUtils, so we have to initialise the config after Steam or we crash
	if (!Config().Init(argc, argv))
		return 1; // Error already logged


	std::optional<SteamNetworkingIPAddr> listenaddress = Config().listenaddress;

	// Cleaned up by CServer
	HSteamListenSocket listensocket = k_HSteamListenSocket_Invalid;
	if (!listenaddress.has_value())
	{
		listensocket = SteamGameServerNetworkingSockets()->CreateListenSocketP2P(0, 0, nullptr);
		std::println("Listening on Steam P2P socket");
	}
	else
	{
		listensocket = SteamGameServerNetworkingSockets()->CreateListenSocketIP(listenaddress.value(), 0, nullptr);

		char addrStr[SteamNetworkingIPAddr::k_cchMaxString];
		listenaddress->ToString(addrStr, sizeof(addrStr), true);
		std::println("Listening on {}", addrStr);
	}
	if (listensocket == k_HSteamListenSocket_Invalid)
	{
		std::println("Failed to create listen socket!");
		return 1;
	}

	// Cleaned up by CServer
	HSteamNetPollGroup pollgroup = SteamGameServerNetworkingSockets()->CreatePollGroup();
	if (pollgroup == k_HSteamNetPollGroup_Invalid)
	{
		std::println("Failed to create poll group!");
		SteamGameServerNetworkingSockets()->CloseListenSocket(listensocket);
		return 1;
	}

	// Database
	DB().Init();
	CAutoRelease dbShutdown([]() {
		DB().Close();
	});

	Server().Init(listensocket, pollgroup);
	std::signal(SIGINT, SignalHandler);
	std::signal(SIGTERM, SignalHandler);

	while (!s_bQuit)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(10));

		SteamGameServer_RunCallbacks();
		Server().RunFrame();
		GCClient().RunFrame();
	}

	std::println("Shutting down...");
	Server().Close();
	return 0;
}
