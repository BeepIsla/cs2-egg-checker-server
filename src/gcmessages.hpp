#pragma once

#include "buffer.hpp"
#include <cstddef>
#include <cstdint>
#include <google/protobuf/message.h>
#include <memory>
#include <steammessages.pb.h>
#include <type_traits>
#include <utility>

static inline constexpr const uint32_t PROTO_MASK = 0x80000000;

class IGCProtoMsg
{
public:
	virtual uint32_t                                      GetWireType() const = 0;
	virtual size_t                                        GetSize() const     = 0;
	virtual std::pair<std::unique_ptr<uint8_t[]>, size_t> Serialize() const   = 0;
};

template<typename T>
    requires(std::is_base_of_v<google::protobuf::Message, T>)
class CGCProtoMsg : public IGCProtoMsg
{
private:
	uint32_t           m_type;
	CMsgProtoBufHeader m_header;
	T                  m_body;

public:
	CGCProtoMsg(uint32_t type)
	    : m_type(type)
	{
	}

	CGCProtoMsg(std::unique_ptr<uint8_t[]> &data, size_t size)
	{
		CBuffer buf(data.get(), size);
		m_type = buf.Read<uint32_t>() & ~PROTO_MASK;

		uint32_t hdrLen = buf.Read<uint32_t>();
		if (hdrLen > 0)
			buf.ReadProtobuf(m_header, hdrLen);

		buf.ReadProtobuf(m_body, buf.GetNumBytesLeft());
	}

	uint32_t GetType() const
	{
		return m_type;
	}

	CMsgProtoBufHeader &Hdr()
	{
		return m_header;
	}

	T &Body()
	{
		return m_body;
	}

	virtual uint32_t GetWireType() const override
	{
		return m_type | PROTO_MASK;
	}

	virtual size_t GetSize() const override
	{
		return sizeof(uint32_t) + sizeof(uint32_t) + m_header.ByteSizeLong() + m_body.ByteSizeLong();
	}

	virtual std::pair<std::unique_ptr<uint8_t[]>, size_t> Serialize() const override
	{
		size_t                     size = GetSize();
		std::unique_ptr<uint8_t[]> data = std::make_unique<uint8_t[]>(size);
		CBuffer                    buf(data, size);
		buf.Write<uint32_t>(GetWireType());
		buf.Write<uint32_t>(m_header.ByteSizeLong());
		buf.WriteProtobuf(m_header, false);
		buf.WriteProtobuf(m_body, false);
		return {std::move(data), size};
	}
};
