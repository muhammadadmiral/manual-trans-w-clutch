/*
	THIS FILE IS A PART OF GTA V SCRIPT HOOK SDK
				http://dev-c.com			
			(C) Alexander Blade 2015
*/

#pragma once

#include "main.h"

template <typename T>
static inline void nativePush(T val)
{
	UINT64 val64 = 0;
	if (sizeof(T) > sizeof(UINT64))
	{
		throw "error, value size > 64 bit";
	}
	*reinterpret_cast<T *>(&val64) = val;
	nativePush64(val64);
}

template <typename R, typename... Args>
struct invoke_helper {
	static inline R call(UINT64 hash, Args... args) {
		nativeInit(hash);
		int dummy[] = { 0, (nativePush(args), 0)... };
		(void)dummy;
		return *reinterpret_cast<R *>(nativeCall());
	}
};

template <typename... Args>
struct invoke_helper<void, Args...> {
	static inline void call(UINT64 hash, Args... args) {
		nativeInit(hash);
		int dummy[] = { 0, (nativePush(args), 0)... };
		(void)dummy;
		nativeCall();
	}
};

template <typename R, typename... Args>
static inline R invoke(UINT64 hash, Args... args)
{
	return invoke_helper<R, Args...>::call(hash, args...);
}