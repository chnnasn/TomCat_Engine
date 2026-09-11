#pragma once

#include <xhash>

namespace TomCat {

	class UUID
	{
	public:
		UUID();
		UUID(uint64_t uuid);
		UUID(const UUID&) = default;
		UUID& operator=(const UUID&) = default;

		operator uint64_t() const { return m_UUID; }
		bool operator==(const UUID& other) const { return m_UUID == other.m_UUID; }
		bool operator!=(const UUID& other) const { return !(*this == other); }
	private:
		uint64_t m_UUID;
	};

}

namespace std {

	template<>
	struct hash<TomCat::UUID>
	{
		std::size_t operator()(const TomCat::UUID& uuid) const
		{
			return hash<uint64_t>()((uint64_t)uuid);
		}
	};

}
