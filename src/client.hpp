#pragma once

#include "buffer.hpp"
#include "db.hpp"
#include <chrono>
#include <network_connection.pb.h>
#include <steam/steam_gameserver.h>

class CMsgSOCacheSubscribed;

class CClient
{
	using clock = std::chrono::steady_clock;

private:
	HSteamNetConnection m_conn;
	CSteamID            m_steamID; // Based on ISteamNetworkingSockets, when using "-listen" this could be an invalid SteamID, auth ticket will override this
	bool                m_authenticated = false;
	clock::time_point   m_connected     = clock::now();
	clock::time_point   m_lastrecv      = clock::now();
	bool                m_closed        = false;
	bool                m_tracked       = false;

public:
	CClient(HSteamNetConnection conn, CSteamID steamID)
	    : m_conn(conn),
	      m_steamID(steamID)
	{
	}

	CClient(const CClient &other)            = delete;
	CClient(CClient &&other)                 = delete;
	CClient &operator=(const CClient &other) = delete;
	CClient &operator=(CClient &&other)      = delete;

	~CClient()
	{
		Close(NETWORK_DISCONNECT_UNUSUAL);
	}

	inline bool operator==(HSteamNetConnection conn) const
	{
		return m_conn == conn;
	}

	inline bool operator==(CSteamID steamID) const
	{
		return m_steamID == steamID;
	}

	inline HSteamNetConnection GetConnectionHandle() const
	{
		return m_conn;
	}

	inline CSteamID GetSteamID() const
	{
		return m_steamID;
	}

	inline bool IsAuthenticated() const
	{
		return m_authenticated;
	}

	inline bool IsClosed() const
	{
		return m_closed;
	}

	inline std::chrono::seconds GetConnectTime() const
	{
		return std::chrono::duration_cast<std::chrono::seconds>(clock::now() - m_connected);
	}

	void Track(CDB::EUserResult result, const char *extra = nullptr, const CMsgSOCacheSubscribed *socache = nullptr);
	void MarkAuthenticated();
	void Close(ENetworkDisconnectionReason reason, bool allowLinger = false);
	bool BTimedOut() const;
	void RunFrame();
	void OnPacket(const void *data, size_t size);
	void Send(const CBuffer &buf, int flags);
	void Send(const void *data, size_t size, int flags);
	// TODO: reliable parameter is currently ignored, always sent as reliable
	//       once we fix "OnPacket" to not use a hardcoded buffer and use this method we'll fix this parameter
	// Does no buffering, immediately sends the data
	void SendNetMsg(uint32_t type, const google::protobuf::Message &msg, bool reliable);
	void PrintToConsole(const std::string &text);
};
