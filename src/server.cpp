#include "server.hpp"
#include "asserts.hpp"
#include "client.hpp"
#include "db.hpp"
#include "gcclient.hpp"
#include "helpers.hpp"
#include <print>
#include <steam/steamnetworkingtypes.h>
#include <utility>

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

CServer::CServer()
    : m_socket(k_HSteamListenSocket_Invalid),
      m_pollGroup(k_HSteamNetPollGroup_Invalid)
{
}

void CServer::Close()
{
	for (std::unique_ptr<CClient> &client : m_clients)
		client->Close(NETWORK_DISCONNECT_SERVER_SHUTDOWN);
	m_clients.clear();
	SteamGameServerNetworkingSockets()->DestroyPollGroup(m_pollGroup);
	SteamGameServerNetworkingSockets()->CloseListenSocket(m_socket);
	SteamGameServer()->LogOff();
}

void CServer::Init(HSteamListenSocket socket, HSteamNetPollGroup pollGroup)
{
	m_socket    = socket;
	m_pollGroup = pollGroup;

	// The client cannot connect to persistent servers via P2P/SteamID so always use anonymous
	SteamGameServer()->LogOnAnonymous();
#ifdef DEBUG
	SteamNetworkingUtils()->SetDebugOutputFunction(k_ESteamNetworkingSocketsDebugOutputType_Everything, OnSteamNetworkingSocketsDebugOutput);
#else
	SteamNetworkingUtils()->SetDebugOutputFunction(k_ESteamNetworkingSocketsDebugOutputType_Msg, OnSteamNetworkingSocketsDebugOutput);
#endif
}

void CServer::RunFrame()
{
	std::erase_if(m_clients, [](const std::unique_ptr<CClient> &client) {
		return client->IsClosed();
	});

	SteamNetworkingMessage_t *msgs[16];
	int                       count = SteamGameServerNetworkingSockets()->ReceiveMessagesOnPollGroup(m_pollGroup, msgs, sizeof(msgs) / sizeof(msgs[0]));
	for (int i = 0; i < count; i++)
	{
		auto it = std::find_if(m_clients.begin(), m_clients.end(), [conn = msgs[i]->m_conn](const std::unique_ptr<CClient> &client) -> bool {
			return client->GetConnectionHandle() == conn;
		});
		if (it != m_clients.end())
			(*it)->OnPacket(msgs[i]->m_pData, msgs[i]->m_cbSize);
		msgs[i]->Release();
	}

	for (std::unique_ptr<CClient> &client : m_clients)
		client->RunFrame();

	// We randomly stop receiving SOCaches from the GC, no idea why.
	// I've seen it happen after only 25 minutes!
	// Lets try relogging every hour, maybe that will help.
	// This doesn't actually change our SteamID, maybe we have to fully shutdown the API?
	static constexpr size_t RECONNECT_AFTER_N_CLIENTS = 100;
	if (SteamGameServer()->BLoggedOn() && m_clientcount > 0 && m_clientcount != m_lastReconnectClientCount && m_clients.empty() && (m_clientcount % RECONNECT_AFTER_N_CLIENTS) == 0)
	{
		m_lastReconnectClientCount = m_clientcount;

		std::println("Had {} clients connect, relogging...", m_clientcount);
		SteamGameServer()->LogOff();
		SteamGameServer()->LogOnAnonymous();
	}
}

CClient *CServer::FindClient(CSteamID steamID)
{
	for (std::unique_ptr<CClient> &client : m_clients)
	{
		if (!client->IsClosed() && client->GetSteamID() == steamID)
			return client.get();
	}
	return nullptr;
}

void CServer::OnSteamServersConnected(SteamServersConnected_t *pParam)
{
	std::println("Connected to Steam servers: {}", SteamGameServer()->GetSteamID());
}

void CServer::OnSteamServersDisconnected(SteamServersDisconnected_t *pParam)
{
	std::println("Steam servers disconnected: {}", std::to_underlying(pParam->m_eResult));
}

void CServer::OnSteamServerConnectFailure(SteamServerConnectFailure_t *pParam)
{
	std::println("Steam server connect failure: {}{}", std::to_underlying(pParam->m_eResult), pParam->m_bStillRetrying ? " (Retrying)" : "");
}

void CServer::OnSteamNetConnectionStatusChangedCallback(SteamNetConnectionStatusChangedCallback_t *pParam)
{
	switch (pParam->m_info.m_eState)
	{
		case k_ESteamNetworkingConnectionState_None:
		case k_ESteamNetworkingConnectionState_Dead:
		{
			auto it = std::find_if(m_clients.begin(), m_clients.end(), [conn = pParam->m_hConn](const std::unique_ptr<CClient> &client) -> bool {
				return client->GetConnectionHandle() == conn;
			});
			if (it != m_clients.end())
				m_clients.erase(it);
			break;
		}
		case k_ESteamNetworkingConnectionState_ClosedByPeer:
		case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
		{
			auto it = std::find_if(m_clients.begin(), m_clients.end(), [conn = pParam->m_hConn](const std::unique_ptr<CClient> &client) -> bool {
				return client->GetConnectionHandle() == conn;
			});
			if (it != m_clients.end())
			{
				const char *reason;
				if (pParam->m_info.m_eState == k_ESteamNetworkingConnectionState_ProblemDetectedLocally)
					reason = "Problem detected locally";
				else
					reason = "Closed by peer";

				std::println("Client {} disconnected (Reason {}: {}) {}", (*it)->GetSteamID(), reason, pParam->m_info.m_eEndReason, pParam->m_info.m_szEndDebug);
				m_clients.erase(it); // Will automatically destroy the connection on our side
			}
			break;
		}
		case k_ESteamNetworkingConnectionState_Connecting:
		{
			DEBUG_ASSERT(
			    std::find_if(m_clients.begin(), m_clients.end(), [conn = pParam->m_hConn](const std::unique_ptr<CClient> &client) -> bool {
				    return client->GetConnectionHandle() == conn;
			    }) == m_clients.end()
			);

			CSteamID steamID;
			steamID.SetFromUint64(pParam->m_info.m_identityRemote.GetSteamID64());

			// In theory there can be multiple clients with the same SteamID temporarily, if two clients connect via IP they will have SteamID 0
			// So take this log with a grain of salt.
			std::println("Client connected {} (GC={})", steamID, GCClient().BConnectedToGC() ? "Connected" : "Disconnected");

			if (SteamGameServerNetworkingSockets()->AcceptConnection(pParam->m_hConn) != k_EResultOK)
			{
				SteamGameServerNetworkingSockets()->CloseConnection(pParam->m_hConn, 0, nullptr, false);
				std::println("Failed to accept connection from {}", steamID);
				break;
			}

			if (!SteamGameServerNetworkingSockets()->SetConnectionPollGroup(pParam->m_hConn, m_pollGroup))
			{
				SteamGameServerNetworkingSockets()->CloseConnection(pParam->m_hConn, 0, nullptr, false);
				std::println("Failed to set pollgroup for {}", steamID);
				break;
			}

			m_clientcount++;
			m_clients.push_back(std::make_unique<CClient>(pParam->m_hConn, steamID));
			break;
		}
		default:
		{
			break;
		}
	}
}

void CServer::OnValidateAuthTicketResponse(ValidateAuthTicketResponse_t *pParam)
{
	std::print("Received authentication ticket status for {}: {}", pParam->m_SteamID, std::to_underlying(pParam->m_eAuthSessionResponse));
	if (pParam->m_SteamID != pParam->m_OwnerSteamID)
		std::print(" (Owner: {})\n", pParam->m_OwnerSteamID);
	else
		std::print("\n");

	auto it = std::find_if(m_clients.begin(), m_clients.end(), [steamID = pParam->m_SteamID](const std::unique_ptr<CClient> &client) -> bool {
		return client->GetSteamID() == steamID;
	});
	if (it == m_clients.end())
	{
		SteamGameServer()->EndAuthSession(pParam->m_SteamID);
		return;
	}

	if (pParam->m_eAuthSessionResponse == k_EAuthSessionResponseOK)
	{
		(*it)->MarkAuthenticated();
		return;
	}

	std::string extra = std::to_string(std::to_underlying(pParam->m_eAuthSessionResponse));
	(*it)->Track(CDB::EUserResult::BadTicket, extra.c_str());

	switch (pParam->m_eAuthSessionResponse)
	{
		case k_EAuthSessionResponseOK:
		{
			break;
		}
		case k_EAuthSessionResponseUserNotConnectedToSteam:
		{
			(*it)->Close(NETWORK_DISCONNECT_STEAM_LOGON);
			break;
		}
		case k_EAuthSessionResponseNoLicenseOrExpired:
		{
			(*it)->Close(NETWORK_DISCONNECT_STEAM_OWNERSHIP);
			break;
		}
		case k_EAuthSessionResponseVACBanned:
		{
			(*it)->Close(NETWORK_DISCONNECT_STEAM_VACBANSTATE);
			break;
		}
		case k_EAuthSessionResponseLoggedInElseWhere:
		{
			(*it)->Close(NETWORK_DISCONNECT_STEAM_LOGGED_IN_ELSEWHERE);
			break;
		}
		case k_EAuthSessionResponseVACCheckTimedOut:
		{
			(*it)->Close(NETWORK_DISCONNECT_STEAM_VAC_CHECK_TIMEDOUT);
			break;
		}
		case k_EAuthSessionResponseAuthTicketCanceled:
		{
			(*it)->Close(NETWORK_DISCONNECT_STEAM_AUTHCANCELLED);
			break;
		}
		case k_EAuthSessionResponseAuthTicketInvalidAlreadyUsed:
		{
			(*it)->Close(NETWORK_DISCONNECT_STEAM_AUTHALREADYUSED);
			break;
		}
		case k_EAuthSessionResponseAuthTicketInvalid:
		{
			(*it)->Close(NETWORK_DISCONNECT_STEAM_AUTHINVALID);
			break;
		}
		case k_EAuthSessionResponsePublisherIssuedBan:
		{
			(*it)->Close(NETWORK_DISCONNECT_STEAM_BANNED);
			break;
		}
		case k_EAuthSessionResponseAuthTicketNetworkIdentityFailure:
		{
			(*it)->Close(NETWORK_DISCONNECT_REJECT_STEAM);
			break;
		}
		default:
		{
			(*it)->Close(NETWORK_DISCONNECT_REJECT_STEAM);
			break;
		}
	}
}

CServer &Server()
{
	static CServer s_Server;
	return s_Server;
}
