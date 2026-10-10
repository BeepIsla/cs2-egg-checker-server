#include "gcclient.hpp"
#include "asserts.hpp"
#include "client.hpp"
#include "db.hpp"
#include "helpers.hpp"
#include "server.hpp"
#include <base_gcmessages.pb.h>
#include <cstddef>
#include <cstrike15_gcmessages.pb.h>
#include <gcsdk_gcmessages.pb.h>
#include <gcsystemmsgs.pb.h>
#include <netmessages.pb.h>
#include <print>
#include <ranges>
#include <steam/steam_gameserver.h>
#include <string>
#include <utility>

template<int id, typename T>
struct ItemAttribute
{
	static constexpr int ID = id;

	static T Parse(const std::string &bytes)
	{
		// Don't decay, we don't allow "const std::string &" for instance
		if constexpr (std::is_same_v<T, std::string>)
		{
			CAttribute_String msg;
			if (msg.ParseFromString(bytes))
				return msg.value();
			else
				return "";
		}
		else if constexpr (std::is_same_v<T, uint32_t> || std::is_same_v<T, int32_t> || std::is_same_v<T, float>)
		{
			if (bytes.size() >= sizeof(uint32_t))
			{
				T val {};
				std::memcpy(&val, bytes.data(), sizeof(T));
				return val;
			}
			else
			{
				return T {};
			}
		}
		else
		{
			static_assert(false, "Unknown template type for ItemAttribute");
		}
	}

	static std::optional<T> Get(const CSOEconItem &item)
	{
		// Doing this loop for every item isn't very good performance wise but we don't have more than 10 attributes anyways, its fine for now
		for (int i = 0; i < item.attribute_size(); i++)
		{
			const CSOEconItemAttribute &attrib = item.attribute(i);
			if (attrib.def_index() == ID)
				return Parse(attrib.value_bytes());
		}
		return std::nullopt;
	}
};

CGCClient::CGCClient()
{
	m_gc = reinterpret_cast<ISteamGameCoordinator *>(
	    SteamClient()->GetISteamGenericInterface(
	        SteamGameServer_GetHSteamUser(),
	        SteamGameServer_GetHSteamPipe(),
	        STEAMGAMECOORDINATOR_INTERFACE_VERSION
	    )
	);
}

void CGCClient::RunFrame()
{
	if (!m_connected && SteamGameServer()->BLoggedOn() && BHelloTimedOut() && !m_HTTPRequestCompleted.IsActive())
	{
		m_lasthello = clock::now();
		if (m_versioncache.BShouldUpdate())
		{
			HTTPRequestHandle request = SteamGameServerHTTP()->CreateHTTPRequest(k_EHTTPMethodGET, "https://raw.githubusercontent.com/SteamDatabase/GameTracking-CS2/master/game/csgo/steam.inf");
			SteamGameServerHTTP()->SetHTTPRequestAbsoluteTimeoutMS(request, 30 * 1000);

			SteamAPICall_t call = k_uAPICallInvalid;
			if (!SteamGameServerHTTP()->SendHTTPRequest(request, &call))
			{
				std::println("Failed to send steam.inf request");
				SteamGameServerHTTP()->ReleaseHTTPRequest(request);
			}
			else
			{
				std::println("Requesting steam.inf");
				m_HTTPRequestCompleted.Set(call, this, &CGCClient::OnHTTPRequestCompleted);
				m_HTTPRequestCompleted.SetGameserverFlag();
			}
		}
		else
		{
			SendGCHello();
		}
	}
	else if (m_connected && SteamGameServer()->BLoggedOn() && BShouldSendReservationUpdate())
	{
		std::println("Sending reservation update (Version={})", m_versioncache.m_gameversion);
		m_lastreservationupdate = clock::now();

		CGCProtoMsg<CMsgGCCStrike15_v2_MatchmakingServerReservationResponse> msg(k_EMsgGCCStrike15_v2_MatchmakingServerReservationResponse);
		msg.Body().set_map("graphics_settings");
		msg.Body().set_server_version(m_versioncache.m_gameversion);
		msg.Body().set_system_load(1);
		msg.Body().set_cpus_online(1);
		GCClient().Send(msg);
	}

	std::erase_if(m_socache, [](const ItemCache &item) {
		return item.BExpired();
	});
}

void CGCClient::Send(const IGCProtoMsg &msg)
{
	const auto &[data, size] = msg.Serialize();
	EGCResults result        = m_gc->SendMessage(msg.GetWireType(), data.get(), size);
	if (result != k_EGCResultOK)
		std::println("Failed to send GC message: {}", std::to_underlying(result));
}

void CGCClient::OnGCMessageAvailable(GCMessageAvailable_t *pParam)
{
	uint32_t                                      size = 0;
	std::pair<std::unique_ptr<uint8_t[]>, size_t> data = {nullptr, 0};
	while (m_gc->IsMessageAvailable(&size))
	{
		if (data.second < size)
		{
			data.first  = std::make_unique<uint8_t[]>(size);
			data.second = size;
		}

		uint32_t   type   = 0;
		EGCResults result = m_gc->RetrieveMessage(&type, data.first.get(), size, &size);
		if (result != k_EGCResultOK)
		{
			std::println("Failed to receive GC message: {}", std::to_underlying(result));
			continue;
		}

		std::println("Received GC message {}/{} (Size={})", type, type & ~PROTO_MASK, size);
		OnMessage(type, data.first, size);
	}
}

void CGCClient::OnSteamServersDisconnected(SteamServersDisconnected_t *pParam)
{
	m_connected = false;
	std::println("Disconnected from GC");
}

void CGCClient::OnSteamServerConnectFailure(SteamServerConnectFailure_t *pParam)
{
	m_connected = false;
	std::println("Disconnected from GC");
}

bool CGCClient::BHelloTimedOut() const
{
	auto delta = std::chrono::duration_cast<std::chrono::seconds>(clock::now() - m_lasthello);
	return delta.count() >= 5;
}

bool CGCClient::BShouldSendReservationUpdate() const
{
	auto delta = std::chrono::duration_cast<std::chrono::seconds>(clock::now() - m_lastreservationupdate);
	return delta.count() >= 60;
}

void CGCClient::SendGCHello()
{
	std::println("Sending GC hello (Version={})", m_versioncache.m_gcversion);

	CGCProtoMsg<CMsgServerHello> msg(k_EMsgGCServerHello);
	msg.Body().set_version(m_versioncache.m_gcversion);
	msg.Body().set_client_launcher(0);
	Send(msg);
}

void CGCClient::OnPet(CClient *client, CSOEconItem &pet)
{
	if (!client)
		return;

	// Known attributes on a pet:
	// - 111=custom name attr
	// - 180=deployment date
	// - 185=campaign completion bitfield
	// - 268=upgrade level
	// - 296=pet id
	// - 303=pet food expiration date
	// - 304=pet next upgrade date
	// - 313=pet seed

	std::vector<std::string> lines;
	if (auto name = ItemAttribute<111, std::string>::Get(pet))
		lines.push_back(std::format("[ {} : {} ]", pet.id(), name.value()));
	else
		lines.push_back(std::format("[ {} ]", pet.id()));

	if (auto deployDate = ItemAttribute<180, uint32_t>::Get(pet))
		lines.push_back(std::format("- Hatched: {}", FormatTimestampToUTCString(deployDate.value())));

	if (auto upgradeLevel = ItemAttribute<268, uint32_t>::Get(pet))
	{
		static constexpr const char *levels[] = {
		    "Egg",
		    "Chick",
		    "Pullet",
		    "Hen",
		};
		static constexpr const int levelsSize = sizeof(levels) / sizeof(*levels);
		if (upgradeLevel.value() < 0 || upgradeLevel.value() >= levelsSize)
			lines.push_back(std::format("- Upgrade Level: {}", upgradeLevel.value()));
		else
			lines.push_back(std::format("- Upgrade Level: {}", levels[upgradeLevel.value()]));
	}

	if (auto petId = ItemAttribute<296, uint32_t>::Get(pet))
	{
		static constexpr const char *petTypes[] = {
		    nullptr,
		    "Egg",
		    "Chick",
		    "Catalana",
		    "Silkie",
		    "Polish",
		};
		static constexpr const int petTypesSize = sizeof(petTypes) / sizeof(*petTypes);
		if (petId.value() < 0 || petId.value() >= petTypesSize || petTypes[petId.value()] == nullptr)
			lines.push_back(std::format("- Pet ID: {}", petId.value()));
		else
			lines.push_back(std::format("- Pet ID: {}", petTypes[petId.value()]));

		// Style names are based on the filenames, they are shit but whatever
		static const std::vector<const char *> petStyles[] = {
		    {},

		    // Egg
		    {"Egg basic"},

		    // Chick
		    {"Yellow"},

		    // Catalana
		    {"Tan", "Tan", "Black white head", "White", "Yellow and red", "Light blue and red", "Black with red head", "Blue", "Dark orange", "Blue and light orange", "Orange blue neck", "Yellow red gradient", "Grey", "Black"},

		    // Silkie
		    {"White", "White", "Splash", "Blue with red beard", "Leopard", "Red", "Red beetle", "Orange", "Purple", "Aqua with yellow"},

		    // Polish
		    {"Brown", "Black with white head", "Brown", "White with black wing tips", "Brown with black wing tips", "White black head color", "Blue", "Green with red head", "Green", "Pink", "Blue yellow", "Orange white blue", "Orange with black stripes"},
		};
		static constexpr const int petStylesSize = sizeof(petStyles) / sizeof(*petStyles);
		if (petId.value() < 0 || petId.value() >= petStylesSize || pet.style() < 0 || pet.style() >= petStyles[petId.value()].size())
			lines.push_back(std::format("- Style: {}", pet.style()));
		else
			lines.push_back(std::format("- Style: {}", petStyles[petId.value()][pet.style()]));
	}

	if (auto foodExpiration = ItemAttribute<303, uint32_t>::Get(pet))
	{
		std::optional<std::string> duration = FormatTimeLeftDuration(foodExpiration.value());
		lines.push_back(std::format("- Food Expiration: {} ({})", FormatTimestampToUTCString(foodExpiration.value()), duration.value_or("Expired!")));
	}

	if (auto nextUpgrade = ItemAttribute<304, uint32_t>::Get(pet))
	{
		std::optional<std::string> duration = FormatTimeLeftDuration(nextUpgrade.value());
		lines.push_back(std::format("- Next Upgrade: {} ({})", FormatTimestampToUTCString(nextUpgrade.value()), duration.value_or("Upgraded!")));
	}

	if (auto seed = ItemAttribute<313, uint32_t>::Get(pet))
		lines.push_back(std::format("- Pet Seed: {}", seed.value()));

	if (auto achievements = ItemAttribute<185, uint32_t>::Get(pet))
	{
		struct AchievementCategory
		{
			const char                               *categoryName;
			std::vector<std::pair<int, const char *>> achievements;
		};
		static const AchievementCategory achievementCategories[] = {
		    {
		        "Visited",
		        {
		            {0, "Ancient"},
		            {1, "Anubis"},
		            {2, "Baggage"},
		            {3, "Cache"},
		            {4, "Dust II"},
		            {5, "Inferno"},
		            {6, "Italy"},
		            {7, "Mirage"},
		            {8, "Nuke"},
		            {9, "Office"},
		            {10, "Overpass"},
		            {11, "Train"},
		            {12, "Vertigo"},
		        },
		    },
		    {
		        "Killed by",
		        {
		            {13, nullptr}, // This is just regular "Killed" in whatever way, doesn't seem to be used by Panorama anyways
		            {14, "Gun"},   // Also not used by Panorama?
		            {15, "Fire"},
		            {16, "Knife"}, // Also not used by Panorama?
		            {17, "C4"},
		            {18, "Zeus"},
		        },
		    },
		};

		for (bool showUnlocked : {false, true})
		{
			lines.push_back(showUnlocked ? "- Unlocked Achievements:" : "- Locked Achievements:");

			for (const AchievementCategory &category : achievementCategories)
			{
				std::vector<std::string> parts;
				for (const auto &[bit, name] : category.achievements)
				{
					if (!name)
						continue;

					bool isUnlocked = (achievements.value() & (1U << bit)) != 0;
					if (isUnlocked == showUnlocked)
						parts.push_back(name);
				}
				if (parts.empty())
					continue;

				using namespace std::string_view_literals;
				lines.push_back(std::format("  - {}: {}", category.categoryName, parts | std::views::join_with(", "sv) | std::ranges::to<std::string>()));
			}
		}
	}

	std::string result = lines | std::views::join_with('\n') | std::ranges::to<std::string>();
	client->PrintToConsole(result);
}

void CGCClient::OnMessage(uint32_t wireType, std::unique_ptr<uint8_t[]> &data, size_t size)
{
	bool     isProtobuf = (wireType & PROTO_MASK) == PROTO_MASK;
	uint32_t msgType    = wireType & ~PROTO_MASK;

	// For now we only handle protobufs
	if (!isProtobuf)
		return;

	switch (msgType)
	{
		case k_EMsgGCServerWelcome:
		{
			CGCProtoMsg<CMsgClientWelcome> msg(data, size);

			CMsgCStrike15Welcome inner;
			std::ignore = inner.ParseFromString(msg.Body().game_data());
			std::println("Received GameCoordinator welcome (ReservationID={})", inner.gscookieid());

			m_connected = true;
			break;
		}
		case k_EMsgGCServerConnectionStatus:
		case k_EMsgGCClientConnectionStatus:
		{
			CGCProtoMsg<CMsgConnectionStatus> msg(data, size);

			std::println("Received GameCoordinator connection status: {}", GCConnectionStatus_Name(msg.Body().status()));
			if (msg.Body().status() != GCConnectionStatus_HAVE_SESSION)
				m_connected = false;
			break;
		}
		case k_ESOMsg_CacheSubscribed:
		{
			CGCProtoMsg<CMsgSOCacheSubscribed> msg(data, size);

			CSteamID steamID;
			steamID.SetFromUint64(msg.Body().owner_soid().id());

			CClient *client = Server().FindClient(steamID);
			if (client)
				client->SetHaveSOCache();

			OnSOCache(msg.Body(), true);
			break;
		}
		case k_ESOMsg_CacheSubscriptionCheck:
		{
			CGCProtoMsg<CMsgSOCacheSubscriptionCheck> msg(data, size);

			CSteamID steamID;
			steamID.SetFromUint64(msg.Body().owner_soid().id());

			ItemCache *cache = FindCache(steamID, false);
			if (cache && cache->m_cache.version() == msg.Body().version())
			{
				CClient *client = Server().FindClient(steamID);
				if (client)
					client->SetHaveSOCache();

				std::println("Found cached data for {}", steamID);

				// The cache did not change, so update the cache duration
				GCClient().OnSOCache(cache->m_cache, true);
			}
			else
			{
				std::println("Requesting SOCache for {} ({})", steamID, cache ? "Version mismatch" : "Not cached");

				CGCProtoMsg<CMsgSOCacheSubscriptionRefresh> msg(k_ESOMsg_CacheSubscriptionRefresh);
				if (CMsgSOIDOwner *owner = msg.Body().mutable_owner_soid())
				{
					owner->set_type(1);
					owner->set_id(steamID.ConvertToUint64());
				}
				GCClient().Send(msg);
			}

			break;
		}
		// We don't have to take care of Create, Update, Destroy, or UpdateMultiple
		// We kick the client too quickly anyways
		// Also CacheUnsubscribed isn't even used by the real server, it keeps the SOCache in memory anyways
		default:
		{
			break;
		}
	}
}

void CGCClient::OnSOCache(const CMsgSOCacheSubscribed &cache, bool pushBackCacheTime)
{
	CSteamID steamID;
	steamID.SetFromUint64(cache.owner_soid().id());

	ItemCache *memoryCache = FindCache(steamID, true);
	DEBUG_ASSERT(memoryCache);
	// Don't update m_timecached, we count from the time we first got the data, not from the last time it was accessed
	// We pass a reference of ItemCache into here sometimes, so only CopyFrom if its different
	if (&memoryCache->m_cache != &cache)
		memoryCache->m_cache.CopyFrom(cache);

	if (pushBackCacheTime)
		memoryCache->UpdateCachedTime();

	CClient *client = Server().FindClient(steamID);
	if (!client)
		return; // Client left between us requesting the cache and receiving it

	client->PrintToConsole("\n\n\n\n\n\n\n\n\n\n");

	bool foundAnyEgg = false;
	for (int i = 0; i < cache.objects_size(); i++)
	{
		const CMsgSOCacheSubscribed_SubscribedType &type = cache.objects(i);
		if (type.type_id() == 1)
		{
			for (int j = 0; j < type.object_data_size(); j++)
			{
				CSOEconItem item;
				if (!item.ParseFromString(type.object_data(j)))
				{
					std::println("Failed to parse CSOEconItem index {} from {}", j, steamID);
					continue;
				}

				if (item.def_index() == 4681)
				{
					foundAnyEgg = true;
					OnPet(client, item);
				}
			}
		}
	}

	if (!foundAnyEgg)
		client->PrintToConsole("Failed to find pet");

	client->PrintToConsole(std::format("\nResult cached for {} minutes ({} minutes left)", ItemCache::CACHE_DURATION.count(), memoryCache->GetCacheTimeLeft().count()));
	client->PrintToConsole(std::format("Open Source: https://github.com/BeepIsla/cs2-egg-checker-server"));
	client->PrintToConsole("\n\n\n\n\n\n\n\n\n\n");
	client->Track(foundAnyEgg ? CDB::EUserResult::Success : CDB::EUserResult::NoEgg, nullptr, &cache);
	client->Close(NETWORK_DISCONNECT_UNUSUAL, true);
}

CGCClient::ItemCache *CGCClient::FindCache(CSteamID owner, bool createIfNotFound)
{
	for (ItemCache &cache : m_socache)
	{
		if (cache.m_owner == owner)
			return &cache;
	}

	if (createIfNotFound)
	{
		ItemCache &cache = m_socache.emplace_back(owner);
		return &cache;
	}

	return nullptr;
}

void CGCClient::OnHTTPRequestCompleted(HTTPRequestCompleted_t *pResult, bool bIOFailure)
{
	CAutoRelease steamRelease([pResult]() {
		if (pResult)
			SteamGameServerHTTP()->ReleaseHTTPRequest(pResult->m_hRequest);
	});

	if (!pResult || bIOFailure)
	{
		std::println("Failed to send HTTP request");
		return;
	}

	if (!pResult->m_bRequestSuccessful || pResult->m_eStatusCode != k_EHTTPStatusCode200OK)
	{
		std::println("HTTP request failed: {}", std::to_underlying(pResult->m_eStatusCode));
		return;
	}

	uint32_t size = 0;
	if (!SteamGameServerHTTP()->GetHTTPResponseBodySize(pResult->m_hRequest, &size))
	{
		std::println("Failed to get HTTP response body size");
		return;
	}

	std::unique_ptr<uint8_t[]> data = std::make_unique<uint8_t[]>(size + 1);
	if (!SteamGameServerHTTP()->GetHTTPResponseBodyData(pResult->m_hRequest, data.get(), size))
	{
		std::println("Failed to get HTTP response body data");
		return;
	}
	*(data.get() + size) = 0;

	try
	{
		// Go line by line until we find "ServerVersion"
		std::string       s(reinterpret_cast<char *>(data.get()));
		std::stringstream ss(s);
		std::string       line;
		int               found = 0;
		while (std::getline(ss, line))
		{
			size_t pos = line.find("=");
			if (pos == std::string::npos)
				continue;

			std::string key = line.substr(0, pos);
			if (key == "ServerVersion")
			{
				found++;

				std::string value          = line.substr(pos + 1);
				m_versioncache.m_gcversion = std::stoi(value);
				std::println("Parsed steam.inf and found ServerVersion: {}", m_versioncache.m_gcversion);
			}
			else if (key == "PatchVersion")
			{
				found++;

				std::string value = line.substr(pos + 1);
				std::erase(value, '.');
				m_versioncache.m_gameversion = std::stoi(value);
				std::println("Parsed steam.inf and found PatchVersion: {}", m_versioncache.m_gameversion);
			}
		}

		if (found != 2)
		{
			std::println("Failed to find ServerVersion or PatchVersion in steam.inf");
			return;
		}

		m_versioncache.m_lastupdate = clock::now();
		SendGCHello();
	}
	catch (const std::exception &ex)
	{
		std::println("Failed to parse steam.inf: {}", ex.what());
	}
}

CGCClient &GCClient()
{
	static CGCClient s_GCClient;
	return s_GCClient;
}
