#pragma once
#include <cstdint>
#include <cstdarg>

#define b2_lengthUnitsPerMeter 1.0f
#define b2_maxPolygonVertices 8

// TomCat stores entity UUIDs, not addresses, in body/joint user data.
// Keep all 64 bits on wasm32; the same settings compile Box2D and its consumers.
struct B2_API b2BodyUserData { uint64_t pointer = 0; };
struct B2_API b2JointUserData { uint64_t pointer = 0; };
struct B2_API b2FixtureUserData { uintptr_t pointer = 0; };
B2_API void* b2Alloc_Default(int32 size);
B2_API void b2Free_Default(void* memory);
B2_API void b2Log_Default(const char* message, va_list args);
inline void* b2Alloc(int32 size) { return b2Alloc_Default(size); }
inline void b2Free(void* memory) { b2Free_Default(memory); }
inline void b2Log(const char* message, ...) {
  va_list args;
  va_start(args, message);
  b2Log_Default(message, args);
  va_end(args);
}
