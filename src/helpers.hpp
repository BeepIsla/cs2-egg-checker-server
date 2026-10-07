#pragma once

#include <chrono>
#include <functional>
#include <optional>
#include <steam/steam_gameserver.h>
#include <string>

template<>
struct std::formatter<CSteamID>
{
	constexpr auto parse(std::format_parse_context &ctx)
	{
		auto it = ctx.begin();
		if (it != ctx.end() && *it != '}')
			throw std::format_error("CSteamID does not support format specifiers.");
		return it;
	}

	auto format(const CSteamID &p, std::format_context &ctx) const
	{
		const EAccountType eAccountType = p.GetEAccountType();
		const uint32_t     unUniverse   = static_cast<uint32_t>(p.GetEUniverse());
		const uint32_t     unAccountID  = p.GetAccountID();
		const uint32_t     unInstance   = p.GetUnAccountInstance();

		if (k_EAccountTypeAnonGameServer == eAccountType)
		{
			return std::format_to(ctx.out(), "[A:{}:{}:{}]", unUniverse, unAccountID, unInstance);
		}
		else if (k_EAccountTypeGameServer == eAccountType)
		{
			return std::format_to(ctx.out(), "[G:{}:{}]", unUniverse, unAccountID);
		}
		else if (k_EAccountTypeMultiseat == eAccountType)
		{
			return std::format_to(ctx.out(), "[M:{}:{}:{}]", unUniverse, unAccountID, unInstance);
		}
		else if (k_EAccountTypePending == eAccountType)
		{
			return std::format_to(ctx.out(), "[P:{}:{}]", unUniverse, unAccountID);
		}
		else if (k_EAccountTypeContentServer == eAccountType)
		{
			return std::format_to(ctx.out(), "[C:{}:{}]", unUniverse, unAccountID);
		}
		else if (k_EAccountTypeClan == eAccountType)
		{
			// 'g' for "group"
			return std::format_to(ctx.out(), "[g:{}:{}]", unUniverse, unAccountID);
		}
		else if (k_EAccountTypeChat == eAccountType)
		{
			if (unInstance & k_EChatInstanceFlagClan)
			{
				return std::format_to(ctx.out(), "[c:{}:{}]", unUniverse, unAccountID);
			}
			else if (unInstance & k_EChatInstanceFlagLobby)
			{
				return std::format_to(ctx.out(), "[L:{}:{}]", unUniverse, unAccountID);
			}
			else // Anon chat
			{
				return std::format_to(ctx.out(), "[T:{}:{}]", unUniverse, unAccountID);
			}
		}
		else if (k_EAccountTypeInvalid == eAccountType)
		{
			return std::format_to(ctx.out(), "[I:{}:{}]", unUniverse, unAccountID);
		}
		else if (k_EAccountTypeIndividual == eAccountType)
		{
			if (unInstance != k_unSteamUserDefaultInstance)
				return std::format_to(ctx.out(), "[U:{}:{}:{}]", unUniverse, unAccountID, unInstance);
			else
				return std::format_to(ctx.out(), "[U:{}:{}]", unUniverse, unAccountID);
		}
		else if (k_EAccountTypeAnonUser == eAccountType)
		{
			return std::format_to(ctx.out(), "[a:{}:{}]", unUniverse, unAccountID);
		}
		else
		{
			return std::format_to(ctx.out(), "[i:{}:{}]", unUniverse, unAccountID);
		}
	}
};

class CAutoRelease
{
public:
	using Fn = std::function<void()>;

private:
	Fn m_fn;

public:
	CAutoRelease(Fn fn)
	    : m_fn(fn)
	{
	}

	~CAutoRelease()
	{
		if (m_fn)
			m_fn();
	}

	inline void Cancel()
	{
		m_fn = nullptr;
	}
};

inline constexpr const char *GetOrdinalSuffix(int day)
{
	if (day >= 11 && day <= 13)
		return "th";
	switch (day % 10)
	{
		case 1: return "st";
		case 2: return "nd";
		case 3: return "rd";
		default: return "th";
	}
}

inline std::string FormatTimestampToUTCString(std::int64_t timestamp_seconds)
{
	using namespace std::chrono;

	// Convert seconds to a time_point with second precision
	sys_seconds tp {seconds {timestamp_seconds}};

	// Split into calendar date and time-of-day
	auto           days_since_epoch = floor<days>(tp);
	year_month_day ymd {days_since_epoch};
	hh_mm_ss       tod {tp - days_since_epoch};

	int         day   = static_cast<unsigned>(ymd.day());
	int         year  = static_cast<int>(ymd.year());
	std::string month = std::format("{:%B}", ymd.month()); // "October"

	int hours_24    = tod.hours().count();
	int minutes     = tod.minutes().count();
	int seconds_val = tod.seconds().count();

	// 12-hour clock and am/pm conversion
	bool is_pm    = hours_24 >= 12;
	int  hours_12 = hours_24 % 12;
	if (hours_12 == 0)
		hours_12 = 12;

	return std::format("{}{} {} {} at {:02}:{:02}:{:02}{} UTC", day, GetOrdinalSuffix(day), month, year, hours_12, minutes, seconds_val, is_pm ? "pm" : "am");
}

inline std::optional<std::string> FormatTimeLeftDuration(std::int64_t timestamp_seconds)
{
	using namespace std::chrono;

	const sys_seconds tp {seconds {timestamp_seconds}};
	const auto        now = system_clock::now();
	if (now >= tp)
		return std::nullopt;

	const auto diff = duration_cast<seconds>(tp - now);
	if (diff <= seconds {0})
		return std::nullopt;

	const auto d = duration_cast<days>(diff).count();
	const auto h = duration_cast<hours>(diff % days {1}).count();
	const auto m = duration_cast<minutes>(diff % hours {1}).count();
	const auto s = duration_cast<seconds>(diff % minutes {1}).count();

	std::string result;

	if (d > 0)
		result += std::format("{}d ", d);
	if (h > 0)
		result += std::format("{}h ", h);
	if (m > 0)
		result += std::format("{}m ", m);
	result += std::format("{}s", s);

	return result;
}
