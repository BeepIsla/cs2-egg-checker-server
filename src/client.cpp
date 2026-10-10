#include "client.hpp"
#include "buffer.hpp"
#include "gcclient.hpp"
#include "gcmessages.hpp"
#include "helpers.hpp"
#include "protocol.hpp"
#include <connectionless_netmessages.pb.h>
#include <gcsdk_gcmessages.pb.h>
#include <gcsystemmsgs.pb.h>
#include <print>
#include <steam/steamnetworkingtypes.h>
#include <usermessages.pb.h>
#include <utility>

void CClient::MarkAuthenticated()
{
	if (m_authenticated)
		return;
	m_authenticated = true;

	// The GC will now send us CacheSubscriptionCheck
	// ^ Seems like Valve broke this a few days ago? We now just get CacheSubscribed immediately with all the data...
}

void CClient::Track(CDB::EUserResult result, const char *extra, const CMsgSOCacheSubscribed *socache)
{
	if (m_tracked)
		return;
	m_tracked = true;

	if (m_steamID.IsValid() && m_steamID.ConvertToUint64() != 0)
		DB().SyncTrackUser(m_steamID, result, extra, socache);
}

void CClient::Close(ENetworkDisconnectionReason reason, bool allowLinger)
{
	if (m_closed)
		return;
	m_closed = true;

	if (!m_tracked && m_steamID.IsValid() && m_steamID.ConvertToUint64() != 0)
	{
		m_tracked         = true;
		std::string extra = std::to_string(std::to_underlying(reason));
		DB().SyncTrackUser(m_steamID, CDB::EUserResult::Disconnected, extra.c_str());
	}

	if (m_steamID.IsValid() && m_steamID.ConvertToUint64() != 0)
		SteamGameServer()->EndAuthSession(m_steamID);

	SteamGameServerNetworkingSockets()->CloseConnection(m_conn, std::to_underlying(k_ESteamNetConnectionEnd_App_Generic) + std::to_underlying(reason), nullptr, allowLinger);
	std::println("Closing client connection: {} (AllowLinger={})", std::to_underlying(reason), allowLinger ? "TRUE" : "FALSE");
}

bool CClient::BTimedOut() const
{
	auto delta = std::chrono::duration_cast<std::chrono::seconds>(clock::now() - m_lastrecv);
	return delta.count() > 15;
}

void CClient::RunFrame()
{
	if (!m_closed && BTimedOut())
	{
		Track(CDB::EUserResult::Timeout, "Inactivity");
		Close(NETWORK_DISCONNECT_TIMEDOUT);
	}
	else if (!m_closed && GetConnectTime().count() >= 60)
	{
		// If we take more than 60 seconds just kick the client
		PrintToConsole("\n\n\n\n\n\n\n\n\n\nFailed to receive inventory data from GameCoordinator\n\n\n\n\n\n\n\n\n\n");
		Track(CDB::EUserResult::Timeout, "GC inventory timeout");
		Close(NETWORK_DISCONNECT_TIMEDOUT, true);
	}
	else if (!m_closed && !m_havesocache && GetConnectTime().count() >= 15)
	{
		// After 15 seconds of no cache try to request it from the GC anyways
		m_havesocache = true;

		std::println("No cache for {} after 15 seconds, requesting SOCache...", m_steamID);

		CGCProtoMsg<CMsgSOCacheSubscriptionRefresh> msg(k_ESOMsg_CacheSubscriptionRefresh);
		if (CMsgSOIDOwner *owner = msg.Body().mutable_owner_soid())
		{
			owner->set_type(1);
			owner->set_id(m_steamID.ConvertToUint64());
		}
		GCClient().Send(msg);
	}
}

void CClient::OnPacket(const void *data, size_t size)
{
	if (m_closed)
		return;
	m_lastrecv = clock::now();

#ifdef DEBUG
	std::println("[ {} - {} bytes ]", m_steamID, size);
	for (size_t i = 0; i < size; i++)
	{
		if (i > 0 && (i % 16) == 0)
			std::print("\n");
		std::print("{:02x} ", static_cast<int>(reinterpret_cast<const uint8_t *>(data)[i]) & 0xff);
	}
	std::print("\n");
#endif

	if (size >= sizeof(uint32_t) && *reinterpret_cast<const uint32_t *>(data) == CONNECTIONLESS_HEADER)
	{
		CBuffer buf(const_cast<void *>(data), size, 32 /* Skip header */);
		uint8_t type = buf.Read<uint8_t>();
		switch (type)
		{
			case A2S_GETCHALLENGE:
			{
				CBuffer resp(64);
				resp.Write<uint32_t>(CONNECTIONLESS_HEADER);                           // Header
				resp.Write<uint8>(S2C_CHALLENGE);                                      // Type
				resp.Write<uint32>(0);                                                 // ChallengeNr, should be random for this user, we don't care
				resp.Write<uint32>(3);                                                 // Steam Auth
				resp.Write<uint16>(0);                                                 // Steam Key
				resp.Write<uint64>(SteamGameServer()->GetSteamID().ConvertToUint64()); // SteamID
				resp.Write<uint8>(0);                                                  // VAC Secured
				resp.Write("connect0x00000000");                                       // Context
				Send(resp, k_nSteamNetworkingSend_Reliable);
				break;
			}
			case C2S_CONNECT:
			{
				C2S_CONNECT_Message msg;
				buf.ReadProtobuf(msg);

				const std::string &ticket = msg.auth_steam();
				if (ticket.size() <= sizeof(uint64_t))
				{
					Track(CDB::EUserResult::BadTicket, "Malformed ticket");
					Close(NETWORK_DISCONNECT_STEAM_AUTHINVALID);
					break;
				}

				const uint8_t *data = reinterpret_cast<const uint8_t *>(ticket.data());
				size_t         size = ticket.size();

				uint64_t steamID64 = 0;
				std::memcpy(&steamID64, data, sizeof(uint64_t));
				data += sizeof(uint64_t);
				size -= sizeof(uint64_t);

				CSteamID steamID;
				steamID.SetFromUint64(steamID64);
				m_steamID = steamID;

				// If we already have this user cached simply return what we have and don't bother doing the auth stuff
				// Once we do authentication the GC will send us the players inventory and maybe that is the reason why things are breaking, too many GC fetches?
				// Yes that means technically you can see the pet data of another player by spoofing their SteamID, oh no! Doesn't matter.
				CGCClient::ItemCache *cache = GCClient().FindCache(m_steamID);
				if (!cache || cache->BExpired())
				{
					cache = nullptr;

					EBeginAuthSessionResult result = SteamGameServer()->BeginAuthSession(data, size, steamID);
					if (result != k_EBeginAuthSessionResultOK)
					{
						std::string extra = std::to_string(std::to_underlying(result));
						Track(CDB::EUserResult::BadTicket, extra.c_str());
						Close(NETWORK_DISCONNECT_STEAM_AUTHINVALID);
						break;
					}
				}

				// Write response
				C2S_CONNECTION_Message respProto;
				respProto.set_addon_name("");

				CBuffer resp(16);
				resp.Write<uint32_t>(CONNECTIONLESS_HEADER);
				resp.Write<uint8_t>(S2C_CONNECTION);
				resp.WriteProtobuf(respProto);
				Send(resp, k_nSteamNetworkingSend_Reliable);

				// Send the response first, so the client is in the correct state when we try to print to their console
				if (cache)
				{
					m_havesocache = true;
					GCClient().OnSOCache(cache->m_cache);
				}
				break;
			}
			default:
			{
				break;
			}
		}
	}
	else
	{
		// NetChannel stuff, doesn't matter for us, we should be done long before the client times out
	}
}

void CClient::Send(const CBuffer &buf, int flags)
{
	if (buf.IsOverflowed())
		return;

	Send(buf.GetData(), buf.GetNumBytesWritten(), flags);
}

void CClient::Send(const void *data, size_t size, int flags)
{
	SteamGameServerNetworkingSockets()->SendMessageToConnection(m_conn, data, size, flags, nullptr);
}

void CClient::SendNetMsg(uint32_t type, const google::protobuf::Message &msg, bool reliable)
{
	CBuffer buf(64 + msg.ByteSizeLong());
	buf.WriteVarUInt32(1);  // Packet Type (Reliable)
	buf.WriteVarUInt32(0);  // Server Tick
	buf.WriteUBitVar(type); // Command Type
	buf.WriteProtobuf(msg);
	Send(buf, k_nSteamNetworkingSend_Reliable);
}

void CClient::PrintToConsole(const std::string &text)
{
	// CSVCMsg_Print collapses multiple newline characters into one, so we have to add zws inbetween
	// Instead just use HUD_PRINTCONSOLE, that works fine
#if 0
	CSVCMsg_Print msg;
	msg.set_text(text);
	SendNetMsg(svc_Print, msg, true);
#else
	CUserMessageTextMsg msg;
	msg.set_dest(2); // HUD_PRINTCONSOLE
	msg.add_param(text);
	SendNetMsg(UM_TextMsg, msg, true);
#endif
}
