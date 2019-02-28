#include "FontTextureMap.h"

#include <sstream>

#include "Common.h"

namespace dxstg {

using Microsoft::WRL::ComPtr;

FontTextureMap::FontTextureMap(ID3D11Device *device, const LOGFONTW& font, bool preMultipliedAlpha) :
	m_device(device),
	m_hdc(GetDC(nullptr)),
	m_hfont(CreateFontIndirectW(&font)),
	m_logfont(font),
	m_textmetric(),
	m_preMultipliedAlpha(preMultipliedAlpha),
	m_dataMap()
{
	if (m_hdc == nullptr) throw std::exception("GetDC");
	if (m_hfont == nullptr) throw std::exception("CreateFontIndirectW");

	HFONT oldFont = (HFONT)SelectObject(m_hdc, m_hfont);
	GetTextMetrics(m_hdc, &m_textmetric);
	SelectObject(m_hdc, oldFont);
}

FontTextureMap::FontTextureMap(FontTextureMap&& moved) :
	m_device(std::move(moved.m_device)),
	m_hdc(moved.m_hdc),
	m_hfont(moved.m_hfont),
	m_logfont(moved.m_logfont),
	m_textmetric(moved.m_textmetric),
	m_preMultipliedAlpha(moved.m_preMultipliedAlpha),
	m_dataMap(std::move(moved.m_dataMap))
{
	moved.m_hdc = nullptr;
	moved.m_hfont = nullptr;
}

FontTextureMap& FontTextureMap::operator = (FontTextureMap&& moved)
{
	if (this == &moved) return *this;

	release();

	m_device = std::move(moved.m_device);
	m_hdc = moved.m_hdc;
	m_hfont = moved.m_hfont;
	m_logfont = moved.m_logfont;
	m_textmetric = moved.m_textmetric;
	m_preMultipliedAlpha = moved.m_preMultipliedAlpha;
	m_dataMap = std::move(moved.m_dataMap);

	moved.m_hdc = nullptr;
	moved.m_hfont = nullptr;

	return *this;
}

FontTextureMap::~FontTextureMap()
{
	release();
}

const FontTextureMap::GlyphData& FontTextureMap::operator [] (wchar_t code)
{
	auto it = m_dataMap.find(code);
	if (it != m_dataMap.end()) {
		return it->second; // 構築済み
	} else {
		// まだデータがない
		GlyphData& charData = m_dataMap[code];

		// フォントデータの取得
		const MAT2 mat = { { 0,1 },{ 0,0 },{ 0,0 },{ 0,1 } };
		HFONT oldFont = (HFONT)SelectObject(m_hdc, m_hfont);
		DWORD size = GetGlyphOutlineW(m_hdc, code, GGO_GRAY4_BITMAP, &charData.glyphmetrics, 0, NULL, &mat);

		if (size == GDI_ERROR) {
			OutputDebugStringW(L"failed: GetGlyphOutlineW (1)\n");
			SelectObject(m_hdc, oldFont);
			throw 1;
		}

		if (iswspace(code)) {
			// 空白文字のときはテクスチャは作成しない。
			SelectObject(m_hdc, oldFont);
		} else {
			// 空白文字でないとき
			std::unique_ptr<BYTE[]> byteData = std::make_unique<BYTE[]>(size);
			if (GetGlyphOutlineW(m_hdc, code, GGO_GRAY4_BITMAP, &charData.glyphmetrics, size, byteData.get(), &mat) == GDI_ERROR) {
				OutputDebugStringW(L"failed: GetGlyphOutlineW (2)\n");
				SelectObject(m_hdc, oldFont);
				throw 1;
			}
			SelectObject(m_hdc, oldFont);

			// フォントデータのテクスチャへの書き出し
			// glyphmetrics などの数値の解説 http://marupeke296.com/WINT_GetGlyphOutline.html
#if 1
			D3D11_TEXTURE2D_DESC tex2dDesc;
			tex2dDesc.Width = charData.glyphmetrics.gmBlackBoxX;
			tex2dDesc.Height = charData.glyphmetrics.gmBlackBoxY;
			tex2dDesc.MipLevels = 1;
			tex2dDesc.ArraySize = 1;
			tex2dDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;		// RGBA(255,255,255,255)タイプ
			tex2dDesc.SampleDesc.Count = 1;
			tex2dDesc.SampleDesc.Quality = 0;
			tex2dDesc.Usage = D3D11_USAGE_IMMUTABLE;			// 変更不可
			tex2dDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;	// シェーダリソースとして使う	
			tex2dDesc.CPUAccessFlags = 0;						// CPUからアクセス不可
			tex2dDesc.MiscFlags = 0;
			
			std::unique_ptr<DWORD[]> sysData = std::make_unique<DWORD[]>(tex2dDesc.Width * tex2dDesc.Height);
			
			// フォント情報の書き込み
			// iBmp_w : フォントビットマップの幅
			int iBmp_w = charData.glyphmetrics.gmBlackBoxX + (4 - (charData.glyphmetrics.gmBlackBoxX % 4)) % 4;
			constexpr int Level = 17; // α値の段階 (GGO_GRAY4_BITMAPなので17段階)
			unsigned int x, y;
			DWORD Alpha;

			// memset(pBits, 0, textureSize);
			for (y = 0; y < tex2dDesc.Height; y++) {
				for (x = 0; x < tex2dDesc.Width; x++) {
					Alpha = (255 * byteData[x + iBmp_w * y]) / (Level - 1);
					if (m_preMultipliedAlpha) {
						// 乗算済みアルファ
						sysData[x + tex2dDesc.Width * y] = (Alpha << 24) | (Alpha << 16) | (Alpha << 8) | Alpha;
					} else {
						// 補完アルファ
						sysData[x + tex2dDesc.Width * y] = 0x00ffffff | (Alpha << 24);
					}
				}
			}

			D3D11_SUBRESOURCE_DATA initialData;
			initialData.pSysMem = sysData.get();
			initialData.SysMemPitch = tex2dDesc.Width * 4;  // 4はrgba情報が4バイトなので
			ComPtr<ID3D11Texture2D> texture;

			ThrowIfFailed(L"CreateTexture2D",
				m_device->CreateTexture2D(&tex2dDesc, &initialData, texture.ReleaseAndGetAddressOf()));

			// シェーダーリソースビューの取得
			ThrowIfFailed(L"CreateShaderResourceView",
				m_device->CreateShaderResourceView(texture.Get(), nullptr, charData.shaderResourceView.ReleaseAndGetAddressOf()));

#else
			// テクスチャにマージンを取ったバージョン

			D3D11_TEXTURE2D_DESC tex2dDesc;
			tex2dDesc.Width = charData.glyphmetrics.gmCellIncX;
			tex2dDesc.Height = m_textmetric.tmHeight;
			tex2dDesc.MipLevels = 1;
			tex2dDesc.ArraySize = 1;
			tex2dDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;		// RGBA(255,255,255,255)タイプ
			tex2dDesc.SampleDesc.Count = 1;
			tex2dDesc.SampleDesc.Quality = 0;
			tex2dDesc.Usage = D3D11_USAGE_IMMUTABLE;			// 変更不可
			tex2dDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;	// シェーダリソースとして使う	
			tex2dDesc.CPUAccessFlags = 0;						// CPUからアクセス不可
			tex2dDesc.MiscFlags = 0;
			
			// フォント情報をテクスチャに書き込む
			const size_t textureSize = tex2dDesc.Width * tex2dDesc.Height * 4;
			std::unique_ptr<BYTE[]> sysData = std::make_unique<BYTE[]>(textureSize);
			
			BYTE* pBits = sysData.get();
			// フォント情報の書き込み
			// iOfs_x, iOfs_y : 書き出し位置(左上)
			// iBmp_w, iBmp_h : フォントビットマップの幅高
			// Level : α値の段階 (GGO_GRAY4_BITMAPなので17段階)
			int iOfs_x = charData.glyphmetrics.gmptGlyphOrigin.x;
			int iOfs_y = m_textmetric.tmAscent - charData.glyphmetrics.gmptGlyphOrigin.y;
			int iBmp_w = charData.glyphmetrics.gmBlackBoxX + (4 - (charData.glyphmetrics.gmBlackBoxX % 4)) % 4;
			int iBmp_h = charData.glyphmetrics.gmBlackBoxY;
			int Level = 17;
			int x, y;
			DWORD Alpha, Color;

			memset(pBits, 0, textureSize);
			for (y = iOfs_y; y < iOfs_y + iBmp_h; y++) {
				for (x = iOfs_x; x < iOfs_x + iBmp_w; x++) {
					Alpha = (255 * byteData[x - iOfs_x + iBmp_w * (y - iOfs_y)]) / (Level - 1);

					if (m_preMultipliedAlpha) {
						// 乗算済みアルファ
						Color = (Alpha << 24) | (Alpha << 16) | (Alpha << 8) | Alpha;
					} else {
						// 補完アルファ
						Color = 0x00ffffff | (Alpha << 24);
					}

					memcpy(
						(BYTE*)pBits + tex2dDesc.Width * 4 * y + 4 * x,
						&Color,
						sizeof(DWORD));
				}
			}

			D3D11_SUBRESOURCE_DATA initialData;
			initialData.pSysMem = pBits;
			initialData.SysMemPitch = tex2dDesc.Width * 4;  // 4はrgba情報が4バイトなので
			ComPtr<ID3D11Texture2D> texture;

			ThrowIfFailed(m_device->CreateTexture2D(&tex2dDesc, &initialData, texture.ReleaseAndGetAddressOf()));

			// シェーダーリソースビューの取得
			ThrowIfFailed(m_device->CreateShaderResourceView(texture.Get(), nullptr, charData.shaderResourceView.ReleaseAndGetAddressOf()));
#endif
		}

		return charData;
	}
}

void FontTextureMap::release()
{
	if (m_hfont) {
		DeleteObject(m_hfont);
	}
	if (m_hdc) {
		ReleaseDC(nullptr, m_hdc);
	}
}

}
