#include <cstddef>
#include <memory>
#include <steam/steam_gameserver.h>
#include <vector>

class CClient;

class CServer
{
private:
	HSteamListenSocket                    m_socket;
	HSteamNetPollGroup                    m_pollGroup;
	std::vector<std::unique_ptr<CClient>> m_clients;
	size_t                                m_clientcount              = 0;
	size_t                                m_lastReconnectClientCount = 0;

public:
	CServer();
	void     Init(HSteamListenSocket socket, HSteamNetPollGroup pollGroup);
	void     Close();
	void     RunFrame();
	// Don't keep this around for too long! The pointer may change or get invalidated next frame.
	CClient *FindClient(CSteamID steamID);

private:
	STEAM_GAMESERVER_CALLBACK(CServer, OnSteamServersConnected, SteamServersConnected_t);
	STEAM_GAMESERVER_CALLBACK(CServer, OnSteamServersDisconnected, SteamServersDisconnected_t);
	STEAM_GAMESERVER_CALLBACK(CServer, OnSteamServerConnectFailure, SteamServerConnectFailure_t);
	STEAM_GAMESERVER_CALLBACK(CServer, OnSteamNetConnectionStatusChangedCallback, SteamNetConnectionStatusChangedCallback_t);
	STEAM_GAMESERVER_CALLBACK(CServer, OnValidateAuthTicketResponse, ValidateAuthTicketResponse_t);
};

CServer &Server();
