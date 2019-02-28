#pragma once

#include <string>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace dxstg {
constexpr UINT clientWidth = 640;
constexpr UINT clientHeight = 480;

inline void ThrowIfFailed(const wchar_t* mes, HRESULT hr)
{
	if (FAILED(hr)) {
		OutputDebugStringW(L"falied (HRESULT = ");
		OutputDebugStringW(std::to_wstring(hr).c_str());
		OutputDebugStringW(L"): ");
		OutputDebugStringW(mes);
		OutputDebugStringW(L"\n");
		throw hr;
	}
}

}
