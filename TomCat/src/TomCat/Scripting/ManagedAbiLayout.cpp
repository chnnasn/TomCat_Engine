#include "tcpch.h"

// Compile-time check that the native interop tables match the C# tables in
// Managed/TomCat.Managed/InteropTypes.cs and Managed/TomCat.ScriptHost.
// ManagedAbiLayout.generated.h is generated from the C# side by
//   dotnet run --project Managed/TomCat.Managed.Regression -- --write-interop-abi
// and the managed regression fails when it is stale. A C++ struct that adds,
// removes, reorders or retypes a field without the matching C# change, or the
// reverse, fails this translation unit.

#include "TomCat/Scripting/ScriptTypes.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

#if INTPTR_MAX == INT64_MAX

namespace TomCat::Scripting::AbiLayout {

	// C# has no const and passes enums as their underlying type, so compare the
	// native types after removing both. The calling convention is not part of
	// the type on x64.
	template<typename T, typename = void>
	struct Canonical { using Type = std::remove_cv_t<T>; };

	template<typename T>
	using CanonicalT = typename Canonical<T>::Type;

	template<typename T>
	struct Canonical<T, std::enable_if_t<std::is_enum_v<T>>>
	{
		using Type = std::underlying_type_t<T>;
	};

	template<typename T>
	struct Canonical<T*, void> { using Type = CanonicalT<T>*; };

	template<typename T>
	struct Canonical<T* const, void> { using Type = CanonicalT<T>*; };

	template<typename T, size_t N>
	struct Canonical<T[N], void> { using Type = CanonicalT<T>[N]; };

	template<typename R, typename... A>
	struct Canonical<R(*)(A...), void> { using Type = CanonicalT<R>(*)(CanonicalT<A>...); };

	template<typename T>
	using FieldType = CanonicalT<std::remove_reference_t<T>>;

}

#define TC_ABI_CONSTANT(name, value) \
	static_assert(::TomCat::Scripting::name == (value), \
		"Managed and native " #name " differ; update both sides together.");

#define TC_ABI_STRUCT(type, size) \
	static_assert(std::is_standard_layout_v<::TomCat::Scripting::type>, \
		#type " must stay standard-layout to cross the managed boundary."); \
	static_assert(sizeof(::TomCat::Scripting::type) == (size), \
		"sizeof(" #type ") differs from the C# struct.");

#define TC_ABI_FIELD(type, field, offset, ...) \
	static_assert(offsetof(::TomCat::Scripting::type, field) == (offset), \
		"offsetof(" #type ", " #field ") differs from the C# struct."); \
	static_assert(std::is_same_v< \
		::TomCat::Scripting::AbiLayout::FieldType< \
			decltype(std::declval<::TomCat::Scripting::type&>().field)>, \
		::TomCat::Scripting::AbiLayout::CanonicalT<__VA_ARGS__>>, \
		#type "::" #field " has a different type than the C# field.");

namespace TomCat::Scripting {
#include "ManagedAbiLayout.generated.h"
}

#undef TC_ABI_FIELD
#undef TC_ABI_STRUCT
#undef TC_ABI_CONSTANT

#endif
