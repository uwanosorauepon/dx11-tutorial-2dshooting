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
		return it->second; // �\�z�ς�
	} else {
		// �܂��f�[�^���Ȃ�
		GlyphData& charData = m_dataMap[code];

		// �t�H���g�f�[�^�̎擾
		const MAT2 mat = { { 0,1 },{ 0,0 },{ 0,0 },{ 0,1 } };
		HFONT oldFont = (HFONT)SelectObject(m_hdc, m_hfont);
		DWORD size = GetGlyphOutlineW(m_hdc, code, GGO_GRAY4_BITMAP, &charData.glyphmetrics, 0, NULL, &mat);

		if (size == GDI_ERROR) {
			OutputDebugStringW(L"failed: GetGlyphOutlineW (1)\n");
			SelectObject(m_hdc, oldFont);
			throw 1;
		}

		if (iswspace(code)) {
			// �󔒕����̂Ƃ��̓e�N�X�`���͍쐬���Ȃ��B
			SelectObject(m_hdc, oldFont);
		} else {
			// �󔒕����łȂ��Ƃ�
			std::unique_ptr<BYTE[]> byteData = std::make_unique<BYTE[]>(size);
			if (GetGlyphOutlineW(m_hdc, code, GGO_GRAY4_BITMAP, &charData.glyphmetrics, size, byteData.get(), &mat) == GDI_ERROR) {
				OutputDebugStringW(L"failed: GetGlyphOutlineW (2)\n");
				SelectObject(m_hdc, oldFont);
				throw 1;
			}
			SelectObject(m_hdc, oldFont);

			// �t�H���g�f�[�^�̃e�N�X�`���ւ̏����o��
			// glyphmetrics �Ȃǂ̐��l�̉�� http://marupeke296.com/WINT_GetGlyphOutline.html
#if 1
			D3D11_TEXTURE2D_DESC tex2dDesc;
			tex2dDesc.Width = charData.glyphmetrics.gmBlackBoxX;
			tex2dDesc.Height = charData.glyphmetrics.gmBlackBoxY;
			tex2dDesc.MipLevels = 1;
			tex2dDesc.ArraySize = 1;
			tex2dDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;		// RGBA(255,255,255,255)�^�C�v
			tex2dDesc.SampleDesc.Count = 1;
			tex2dDesc.SampleDesc.Quality = 0;
			tex2dDesc.Usage = D3D11_USAGE_IMMUTABLE;			// �ύX�s��
			tex2dDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;	// �V�F�[�_���\�[�X�Ƃ��Ďg��	
			tex2dDesc.CPUAccessFlags = 0;						// CPU����A�N�Z�X�s��
			tex2dDesc.MiscFlags = 0;
			
			std::unique_ptr<DWORD[]> sysData = std::make_unique<DWORD[]>(tex2dDesc.Width * tex2dDesc.Height);
			
			// �t�H���g���̏�������
			// iBmp_w : �t�H���g�r�b�g�}�b�v�̕�
			int iBmp_w = charData.glyphmetrics.gmBlackBoxX + (4 - (charData.glyphmetrics.gmBlackBoxX % 4)) % 4;
			constexpr int Level = 17; // ���l�̒i�K (GGO_GRAY4_BITMAP�Ȃ̂�17�i�K)
			unsigned int x, y;
			DWORD Alpha;

			// memset(pBits, 0, textureSize);
			for (y = 0; y < tex2dDesc.Height; y++) {
				for (x = 0; x < tex2dDesc.Width; x++) {
					Alpha = (255 * byteData[x + iBmp_w * y]) / (Level - 1);
					if (m_preMultipliedAlpha) {
						// ��Z�ς݃A���t�@
						sysData[x + tex2dDesc.Width * y] = (Alpha << 24) | (Alpha << 16) | (Alpha << 8) | Alpha;
					} else {
						// �⊮�A���t�@
						sysData[x + tex2dDesc.Width * y] = 0x00ffffff | (Alpha << 24);
					}
				}
			}

			D3D11_SUBRESOURCE_DATA initialData;
			initialData.pSysMem = sysData.get();
			initialData.SysMemPitch = tex2dDesc.Width * 4;  // 4��rgba���4�o�C�g�Ȃ̂�
			ComPtr<ID3D11Texture2D> texture;

			ThrowIfFailed(L"CreateTexture2D",
				m_device->CreateTexture2D(&tex2dDesc, &initialData, texture.ReleaseAndGetAddressOf()));

			// �V�F�[�_�[���\�[�X�r���[�̎擾
			ThrowIfFailed(L"CreateShaderResourceView",
				m_device->CreateShaderResourceView(texture.Get(), nullptr, charData.shaderResourceView.ReleaseAndGetAddressOf()));

#else
			// �e�N�X�`���Ƀ}�[�W����������o�[�W����

			D3D11_TEXTURE2D_DESC tex2dDesc;
			tex2dDesc.Width = charData.glyphmetrics.gmCellIncX;
			tex2dDesc.Height = m_textmetric.tmHeight;
			tex2dDesc.MipLevels = 1;
			tex2dDesc.ArraySize = 1;
			tex2dDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;		// RGBA(255,255,255,255)�^�C�v
			tex2dDesc.SampleDesc.Count = 1;
			tex2dDesc.SampleDesc.Quality = 0;
			tex2dDesc.Usage = D3D11_USAGE_IMMUTABLE;			// �ύX�s��
			tex2dDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;	// �V�F�[�_���\�[�X�Ƃ��Ďg��	
			tex2dDesc.CPUAccessFlags = 0;						// CPU����A�N�Z�X�s��
			tex2dDesc.MiscFlags = 0;
			
			// �t�H���g�����e�N�X�`���ɏ�������
			const size_t textureSize = tex2dDesc.Width * tex2dDesc.Height * 4;
			std::unique_ptr<BYTE[]> sysData = std::make_unique<BYTE[]>(textureSize);
			
			BYTE* pBits = sysData.get();
			// �t�H���g���̏�������
			// iOfs_x, iOfs_y : �����o���ʒu(����)
			// iBmp_w, iBmp_h : �t�H���g�r�b�g�}�b�v�̕���
			// Level : ���l�̒i�K (GGO_GRAY4_BITMAP�Ȃ̂�17�i�K)
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
						// ��Z�ς݃A���t�@
						Color = (Alpha << 24) | (Alpha << 16) | (Alpha << 8) | Alpha;
					} else {
						// �⊮�A���t�@
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
			initialData.SysMemPitch = tex2dDesc.Width * 4;  // 4��rgba���4�o�C�g�Ȃ̂�
			ComPtr<ID3D11Texture2D> texture;

			ThrowIfFailed(m_device->CreateTexture2D(&tex2dDesc, &initialData, texture.ReleaseAndGetAddressOf()));

			// �V�F�[�_�[���\�[�X�r���[�̎擾
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
