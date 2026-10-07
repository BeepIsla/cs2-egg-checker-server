#include "asserts.hpp"
#include "db.hpp"
#include "gcclient.hpp"
#include "helpers.hpp"
#include "server.hpp"
#include <atomic>
#include <base_gcmessages.pb.h>
#include <connectionless_netmessages.pb.h>
#include <csignal>
#include <cstrike15_gcmessages.pb.h>
#include <gcsdk_gcmessages.pb.h>
#include <gcsystemmsgs.pb.h>
#include <network_connection.pb.h>
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

int main(int argc, char **argv)
{
	struct Options
	{
		// "-dbpath"
		const char *dbpath = nullptr;

		// "-listen"
		const char *listenaddress = nullptr;
	} opts;
	for (int i = 1; i < argc; i++)
	{
		if (std::strcmp(argv[i], "-dbpath") == 0 && i + 1 < argc)
		{
			opts.dbpath = argv[i + 1];
			i += 1;
		}
		else if (std::strcmp(argv[i], "-listen") == 0 && i + 1 < argc)
		{
			opts.listenaddress = argv[i + 1];
			i += 1;
		}
	}

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

	SteamNetworkingIPAddr addr;
	addr.Clear();
	if (opts.listenaddress && (!addr.ParseString(opts.listenaddress) || addr.m_port == 0))
	{
		std::println("Failed to parse given address '{}'", opts.listenaddress);
		return 1;
	}

	// Cleaned up by CServer
	HSteamListenSocket listensocket = k_HSteamListenSocket_Invalid;
	if (addr.m_port == 0)
	{
		listensocket = SteamGameServerNetworkingSockets()->CreateListenSocketP2P(0, 0, nullptr);
		std::println("Listening on Steam P2P socket");
	}
	else
	{
		listensocket = SteamGameServerNetworkingSockets()->CreateListenSocketIP(addr, 0, nullptr);

		char addrStr[SteamNetworkingIPAddr::k_cchMaxString];
		addr.ToString(addrStr, sizeof(addrStr), true);
		std::println("Listening on {}", addrStr);
	}
	if (listensocket == k_HSteamListenSocket_Invalid)
	{
		std::println("Failed to create P2P listen socket!");
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
	DB().Init(opts.dbpath);
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
