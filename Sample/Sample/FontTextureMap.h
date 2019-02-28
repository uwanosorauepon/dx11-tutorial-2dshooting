#pragma once

#include <memory>
#include <unordered_map>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <wrl/client.h>
#include <d3d11.h>

namespace dxstg {

// �����̃e�N�X�`����ێ�����
// operator [] �Ő����ł���
// �����o�֐��͑�� unordered_map �����B
// const�֐��ȊO�̓}���`�X���b�h��Ή��ł��B
class FontTextureMap final {
public:
	struct GlyphData {
		GLYPHMETRICS glyphmetrics;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> shaderResourceView;
	};
	using DataMap = std::unordered_map<wchar_t, GlyphData>;

	FontTextureMap(ID3D11Device *device, const LOGFONTW& font, bool preMultipliedAlpha);
	FontTextureMap(const FontTextureMap&) = delete;              // �R�s�[�s��
	FontTextureMap& operator = (const FontTextureMap&) = delete; // �R�s�[�s��
	FontTextureMap(FontTextureMap&&);              // ���[�u��
	FontTextureMap& operator = (FontTextureMap&&); // ���[�u��
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
	const GlyphData& operator [] (wchar_t code); // �t�H���g�f�[�^���쐬����ĂȂ��ꍇ�͍쐬����

	const TEXTMETRICW& getTextMetric() const noexcept { return m_textmetric; }
	const LOGFONTW& getLogFont() const noexcept { return m_logfont; }

	// ���������t�H���g����Z�ς݃A���t�@�Ȃ� true, ���ʂ̃A���t�@�Ȃ� false
	bool isPreMultipliedAlpha() const noexcept { return m_preMultipliedAlpha; }

private:
	Microsoft::WRL::ComPtr<ID3D11Device> m_device;
	HDC m_hdc;		// �f�o�C�X�R���e�L�X�g
	HFONT m_hfont;	// �t�H���g�n���h��
	LOGFONTW m_logfont;
	TEXTMETRICW m_textmetric;
	bool m_preMultipliedAlpha;
	DataMap m_dataMap;

	void release();
};

}
