#pragma once

#include "gcmessages.hpp"
#include <base_gcmessages.pb.h>
#include <chrono>
#include <gcsdk_gcmessages.pb.h>
#include <steam/isteamgamecoordinator.h>
#include <steam/steam_gameserver.h>

class CClient;

class CGCClient
{
	using clock = std::chrono::steady_clock;

public:
	struct ItemCache
	{
		CSteamID              m_owner;
		clock::time_point     m_timecached;
		clock::time_point     m_timecacheexpiresat;
		CMsgSOCacheSubscribed m_cache;

		ItemCache(CSteamID owner)
		    : m_owner(owner),
		      m_timecached(clock::now()),
		      m_timecacheexpiresat(m_timecached + std::chrono::minutes(30))
		{
		}

		bool BExpired() const
		{
			auto delta = std::chrono::duration_cast<std::chrono::seconds>(clock::now() - m_timecached);
			return delta.count() >= 30 * 60; // We cache for 30 minutes
		}
	};

private:
	ISteamGameCoordinator *m_gc;
	bool                   m_connected = false;
	clock::time_point      m_lasthello = clock::time_point(std::chrono::seconds(0));
	struct
	{
		clock::time_point m_lastupdate = clock::time_point(std::chrono::seconds(0));
		uint32_t          m_version    = 0;

		bool BShouldUpdate() const
		{
			if (m_version == 0)
				return true;

			auto delta = std::chrono::duration_cast<std::chrono::seconds>(clock::now() - m_lastupdate);
			return delta.count() >= 60;
		}
	} m_versioncache;
	std::vector<ItemCache> m_socache;

public:
	CGCClient();
	void       RunFrame();
	void       Send(const IGCProtoMsg &msg);
	void       OnSOCache(const CMsgSOCacheSubscribed &cache);
	ItemCache *FindCache(CSteamID owner, bool createIfNotFound = false);
	STEAM_GAMESERVER_CALLBACK(CGCClient, OnGCMessageAvailable, GCMessageAvailable_t);
	STEAM_GAMESERVER_CALLBACK(CGCClient, OnSteamServersDisconnected, SteamServersDisconnected_t);
	STEAM_GAMESERVER_CALLBACK(CGCClient, OnSteamServerConnectFailure, SteamServerConnectFailure_t);

	inline bool BConnectedToGC() const
	{
		return m_connected;
	}

private:
	bool BHelloTimedOut() const;
	void SendGCHello();
	void OnPet(CClient *client, CSOEconItem &pet);
	void OnMessage(uint32_t wireType, std::unique_ptr<uint8_t[]> &data, size_t size);

	void                                           OnHTTPRequestCompleted(HTTPRequestCompleted_t *pResult, bool bIOFailure);
	CCallResult<CGCClient, HTTPRequestCompleted_t> m_HTTPRequestCompleted;
};

CGCClient &GCClient();
