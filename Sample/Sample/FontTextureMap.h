#pragma once

#include <memory>
#include <unordered_map>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <wrl/client.h>
#include <d3d11.h>

namespace dxstg {

// 文字のテクスチャを保持する
// operator [] で生成できる
// メンバ関数は大体 unordered_map 準拠。
// const関数以外はマルチスレッド非対応です。
class FontTextureMap final {
public:
	struct GlyphData {
		GLYPHMETRICS glyphmetrics;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> shaderResourceView;
	};
	using DataMap = std::unordered_map<wchar_t, GlyphData>;

	FontTextureMap(ID3D11Device *device, const LOGFONTW& font, bool preMultipliedAlpha);
	FontTextureMap(const FontTextureMap&) = delete;              // コピー不可
	FontTextureMap& operator = (const FontTextureMap&) = delete; // コピー不可
	FontTextureMap(FontTextureMap&&);              // ムーブ可
	FontTextureMap& operator = (FontTextureMap&&); // ムーブ可
	~FontTextureMap();

	bool empty() const noexcept { return m_dataMap.empty(); }
	DataMap::size_type size() const noexcept { return m_dataMap.size(); }
	DataMap::const_iterator cbegin() const noexcept { return m_dataMap.cbegin(); }
	DataMap::const_iterator cend() const noexcept { return m_dataMap.cend(); }
	DataMap::size_type erase(wchar_t code) { return m_dataMap.erase(code); }
	void clear() { m_dataMap.clear(); }
	DataMap::const_iterator find(wchar_t code) const { return m_dataMap.find(code); }
	DataMap::size_type count(wchar_t code) const { return m_dataMap.count(code); }
	const GlyphData& at(wchar_t code) const { return m_dataMap.at(code); }
	const GlyphData& operator [] (wchar_t code); // フォントデータが作成されてない場合は作成する

	const TEXTMETRICW& getTextMetric() const noexcept { return m_textmetric; }
	const LOGFONTW& getLogFont() const noexcept { return m_logfont; }

	// 生成されるフォントが乗算済みアルファなら true, 普通のアルファなら false
	bool isPreMultipliedAlpha() const noexcept { return m_preMultipliedAlpha; }

private:
	Microsoft::WRL::ComPtr<ID3D11Device> m_device;
	HDC m_hdc;		// デバイスコンテキスト
	HFONT m_hfont;	// フォントハンドル
	LOGFONTW m_logfont;
	TEXTMETRICW m_textmetric;
	bool m_preMultipliedAlpha;
	DataMap m_dataMap;

	void release();
};

}
