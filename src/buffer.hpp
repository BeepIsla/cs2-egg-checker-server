#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <google/protobuf/message.h>

class CBuffer
{
private:
	inline static constexpr size_t MAX_VARINT32_BYTES = 5;

private:
	uint8_t *m_data;
	size_t   m_sizeInBits;
	size_t   m_offsetInBits; // In bits!
	bool     m_overflowed;
	bool     m_owned;

public:
	// size in bytes
	CBuffer(void *data, size_t size, size_t offsetInBits = 0)
	    : m_data(static_cast<uint8_t *>(data)),
	      m_sizeInBits(size * 8),
	      m_offsetInBits(offsetInBits),
	      m_overflowed(false),
	      m_owned(false)
	{
	}

	template<typename T, size_t Size>
	CBuffer(T (&data)[Size])
	    : CBuffer(data, sizeof(T) * Size)
	{
	}

	// size in bytes
	template<typename T>
	CBuffer(std::unique_ptr<T> &data, size_t size)
	    : CBuffer(data.get(), size)
	{
	}

	// size in bytes
	CBuffer(size_t size)
	    : m_data(static_cast<uint8_t *>(std::calloc(1, size))),
	      m_sizeInBits(size * 8),
	      m_offsetInBits(0),
	      m_overflowed(false),
	      m_owned(true)
	{
	}

	~CBuffer()
	{
		if (m_owned && m_data)
			std::free(m_data);
	}

	CBuffer(const CBuffer &other)            = delete;
	CBuffer(CBuffer &&other)                 = delete;
	CBuffer &operator=(const CBuffer &other) = delete;
	CBuffer &operator=(CBuffer &&other)      = delete;

	inline bool IsOverflowed() const
	{
		return m_overflowed;
	}

	inline void SetOverflowed()
	{
		m_offsetInBits = m_sizeInBits;
		m_overflowed   = true;
	}

	template<typename T = void *>
	inline T GetData() const
	{
		return reinterpret_cast<T>(m_data);
	}

	inline size_t GetNumBitsLeft() const
	{
		if (m_offsetInBits >= m_sizeInBits)
			return 0;
		return m_sizeInBits - m_offsetInBits;
	}

	inline size_t GetNumBytesLeft() const
	{
		return GetNumBitsLeft() >> 3;
	}

	inline size_t GetNumBitsWritten() const
	{
		return m_offsetInBits;
	}

	inline size_t GetNumBytesWritten() const
	{
		return (GetNumBitsWritten() + 7) >> 3;
	}

	inline void SeekRelative(size_t bits)
	{
		if (GetNumBitsLeft() < bits)
			SetOverflowed();
		else
			m_offsetInBits += bits;
	}

	inline static constexpr int GetVarInt32Size(uint32_t value)
	{
		int size = 0;
		while (value > 0x7F)
		{
			size += 1;
			value >>= 7;
		}
		return size + 1;
	}

#pragma region Reading
	uint32_t ReadUBitLong(size_t numBits);

	// Linker will complain if you fucked up
	template<typename T>
	inline T Read();

	template<std::integral T>
	inline T Read()
	{
		if constexpr (sizeof(T) == 8)
		{
			uint64_t low  = Read<uint32_t>();
			uint64_t high = Read<uint32_t>();
			return static_cast<T>((high << 32) | low);
		}
		else
		{
			return static_cast<T>(ReadUBitLong(sizeof(T) * 8));
		}
	}

	// May read partial strings if your maxlen is too small
	void Read(char *str, size_t maxlen);

	// May read partial strings if your size is too small
	template<size_t Size>
	inline void Read(char str[Size])
	{
		Read(str, Size);
	}

	void ReadBits(void *out, size_t numBits);

	template<typename T>
	inline void ReadBytes(T buf, size_t bytes)
	{
		if (bytes == 0)
			return;

		size_t numBits = bytes * 8;
		if (GetNumBitsLeft() < numBits)
		{
			SetOverflowed();
			return;
		}

		if ((m_offsetInBits & 7) == 0)
		{
			std::memcpy(buf, m_data + (m_offsetInBits >> 3), bytes);
			m_offsetInBits += numBits;
			return;
		}

		ReadBits(buf, numBits);
	}

	uint32_t ReadUBitVar();
	uint32_t ReadVarUInt32();
	void     ReadProtobuf(google::protobuf::Message &msg, size_t size);

	inline void ReadProtobuf(google::protobuf::Message &msg)
	{
		uint32_t size = ReadVarUInt32();
		ReadProtobuf(msg, size);
	}
#pragma endregion

#pragma region Writing
	void WriteUBitLong(uint32_t data, size_t numBits);

	// Linker will complain if you fucked up
	template<typename T>
	inline T Write();

	template<std::integral T>
	inline void Write(T value)
	{
		if constexpr (sizeof(T) == 8)
		{
			uint32_t low  = static_cast<uint32_t>(value & 0xFFFFFFFF);
			uint32_t high = static_cast<uint32_t>((value >> 32) & 0xFFFFFFFF);
			WriteUBitLong(low, 32);
			WriteUBitLong(high, 32);
		}
		else
		{
			WriteUBitLong(static_cast<uint32_t>(value), sizeof(T) * 8);
		}
	}

	void Write(const char *str);
	void WriteBits(const void *in, size_t numBits);
	void WriteBytes(const void *in, size_t numBytes);
	void WriteUBitVar(uint32_t val);
	void WriteVarUInt32(uint32_t val);
	void WriteProtobuf(const google::protobuf::Message &msg, bool writeSizePrefix = true);
#pragma endregion
};
