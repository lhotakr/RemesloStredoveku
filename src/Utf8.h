#pragma once
#if __cpp_char8_t
#define U8(x) reinterpret_cast<const char*>(u8##x)
#else
#define U8(x) x
#endif