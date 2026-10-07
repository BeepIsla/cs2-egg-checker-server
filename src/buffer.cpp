#include "buffer.hpp"

uint32_t CBuffer::ReadUBitLong(size_t numBits)
{
	if (numBits == 0)
		return 0;

	if (numBits > 32)
		numBits = 32;

	if (GetNumBitsLeft() < numBits)
	{
		SetOverflowed();
		return 0;
	}

	uint32_t value = 0;
	for (size_t i = 0; i < numBits;)
	{
		size_t  remaining   = numBits - i;
		size_t  bitOffset   = m_offsetInBits & 7;
		uint8_t currentByte = m_data[m_offsetInBits >> 3];

		size_t   read     = std::min(remaining, 8 - bitOffset);
		uint32_t mask     = (1u << read) - 1u;
		uint32_t readBits = (currentByte >> bitOffset) & mask;

		value |= (readBits << i);

		m_offsetInBits += read;
		i += read;
	}

	return value;
}

void CBuffer::Read(char *str, size_t maxlen)
{
	if (!str || maxlen == 0)
		return;

	size_t i = 0;
	while (!IsOverflowed())
	{
		char c = ReadUBitLong(8);
		if (c == '\0')
			break;

		if (i < maxlen)
			str[i++] = c;
	}
	str[i < maxlen ? i : maxlen - 1] = '\0';
}

void CBuffer::ReadBits(void *out, size_t numBits)
{
	uint8_t *pOut = static_cast<uint8_t *>(out);
	while (numBits >= 8)
	{
		*pOut++ = static_cast<uint8_t>(ReadUBitLong(8));
		numBits -= 8;
	}

	if (numBits > 0)
		*pOut = static_cast<uint8_t>(ReadUBitLong(numBits));
}

uint32_t CBuffer::ReadUBitVar()
{
	uint32_t ret = ReadUBitLong(6);
	switch (ret & (16 | 32))
	{
		case 16:
			return (ret & 15) | (ReadUBitLong(4) << 4);
		case 32:
			return (ret & 15) | (ReadUBitLong(8) << 4);
		case 48:
			return (ret & 15) | (ReadUBitLong(32 - 4) << 4);
		default:
			return ret;
	}
}

uint32_t CBuffer::ReadVarUInt32()
{
	uint32_t result = 0;
	size_t   count  = 0;
	uint32_t b      = 0;
	do
	{
		if (count == MAX_VARINT32_BYTES)
			return result;

		b = Read<uint8_t>();
		result |= (b & 0x7F) << (7 * count);
		count++;
	} while (b & 0x80);

	return result;
}

void CBuffer::ReadProtobuf(google::protobuf::Message &msg, size_t size)
{
	if (GetNumBytesLeft() < size)
	{
		msg.Clear();
		SetOverflowed();
		return;
	}

	if ((m_offsetInBits & 7) == 0)
	{
		std::ignore = msg.ParseFromArray(m_data + (m_offsetInBits >> 3), static_cast<int>(size));
		m_offsetInBits += size * 8;
	}
	else
	{
		std::vector<uint8_t> temp(size);
		ReadBytes(temp.data(), size);
		std::ignore = msg.ParseFromArray(temp.data(), static_cast<int>(size));
	}
}

void CBuffer::WriteUBitLong(uint32_t data, size_t numBits)
{
	if (numBits == 0)
		return;

	if (numBits > 32)
		numBits = 32;

	if (GetNumBitsLeft() < numBits)
	{
		SetOverflowed();
		return;
	}

	for (size_t i = 0; i < numBits;)
	{
		size_t remaining  = numBits - i;
		size_t bitOffset  = m_offsetInBits & 7;
		size_t byteOffset = m_offsetInBits >> 3;
		size_t wrote      = std::min(remaining, 8 - bitOffset);

		uint32_t mask      = (1u << wrote) - 1u;
		uint32_t writeBits = data & mask;
		data >>= wrote;

		uint8_t destMask   = static_cast<uint8_t>(~(mask << bitOffset));
		m_data[byteOffset] = static_cast<uint8_t>((m_data[byteOffset] & destMask) | (writeBits << bitOffset));

		m_offsetInBits += wrote;
		i += wrote;
	}
}

void CBuffer::Write(const char *str)
{
	if (!str)
	{
		WriteUBitLong(0, 8);
		return;
	}

	for (const char *c = str; *c != '\0'; c++)
		WriteUBitLong(*c, 8);
	WriteUBitLong(0, 8);
}

void CBuffer::WriteBits(const void *in, size_t numBits)
{
	if (GetNumBitsLeft() < numBits)
	{
		SetOverflowed();
		return;
	}

	const uint8_t *pIn = static_cast<const uint8_t *>(in);
	while (numBits >= 8)
	{
		WriteUBitLong(*pIn++, 8);
		numBits -= 8;
	}

	if (numBits > 0)
		WriteUBitLong(*pIn, numBits);
}

void CBuffer::WriteBytes(const void *in, size_t numBytes)
{
	if (numBytes == 0)
		return;

	size_t numBits = numBytes * 8;
	if (GetNumBitsLeft() < numBits)
	{
		SetOverflowed();
		return;
	}

	if ((m_offsetInBits & 7) == 0)
	{
		std::memcpy(m_data + (m_offsetInBits >> 3), in, numBytes);
		m_offsetInBits += numBits;
		return;
	}

	WriteBits(in, numBits);
}

void CBuffer::WriteUBitVar(uint32_t val)
{
	if (val < 16)
	{
		WriteUBitLong(val, 6);
	}
	else if (val < 256)
	{
		WriteUBitLong((val & 15) | 16 | ((val & (128 | 64 | 32 | 16)) << 2), 10);
	}
	else if (val < 4096)
	{
		WriteUBitLong((val & 15) | 32 | ((val & (2048 | 1024 | 512 | 256 | 128 | 64 | 32 | 16)) << 2), 14);
	}
	else
	{
		WriteUBitLong((val & 15) | 48, 6);
		WriteUBitLong(val >> 4, 32 - 4);
	}
}

void CBuffer::WriteVarUInt32(uint32_t val)
{
	while (val > 0x7F)
	{
		WriteUBitLong((val & 0x7F) | 0x80, 8);
		val >>= 7;
	}
	WriteUBitLong(val & 0x7F, 8);
}

void CBuffer::WriteProtobuf(const google::protobuf::Message &msg, bool writeSizePrefix)
{
	uint32_t size       = static_cast<uint32_t>(msg.ByteSizeLong());
	size_t   prefixSize = writeSizePrefix ? GetVarInt32Size(size) : 0;
	if (GetNumBytesLeft() < size + prefixSize)
	{
		SetOverflowed();
		return;
	}

	if (writeSizePrefix)
		WriteVarUInt32(size);

	if ((m_offsetInBits & 7) == 0)
	{
		std::ignore = msg.SerializeToArray(m_data + (m_offsetInBits >> 3), size);
		m_offsetInBits += size * 8;
	}
	else
	{
		std::string data = msg.SerializeAsString();
		WriteBytes(data.data(), data.size());
	}
}
