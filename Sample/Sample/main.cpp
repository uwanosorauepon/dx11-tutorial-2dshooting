// �W�����C�u����
#include <iostream>
#include <chrono>
#include <sstream>
#include <memory>
#include <list>
#include <algorithm>
#include <fstream>

// Windows�n���C�u����
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <tchar.h>
#include <wrl/client.h>
#include <wincodec.h>
#include <d3d11.h>
#include <DirectXMath.h>  // �s��̉��Z�Ȃ�

// ����w�b�_�[
#include "Common.h"
#include "FontTextureMap.h"
#include "Game.h"
#include "StgObject.h"

// ���C�u�����t�@�C���̃����N
#pragma comment(lib, "d3d11.lib")

using Microsoft::WRL::ComPtr;

namespace {

class BinFile {
public:
	BinFile(const wchar_t* fpath)
	{
		std::ifstream binfile(fpath, std::ios::in|std::ios::binary);

		if (binfile.is_open()) {
			m_size = static_cast<int>(binfile.seekg(0, std::ios::end).tellg());
			binfile.seekg(0, std::ios::beg);
			m_data = std::make_unique<char[]>(m_size);
			binfile.read(m_data.get(), m_size);
			m_ok = true;
		}
	}

	explicit operator bool() const noexcept { return m_ok; }
	const void* get() const noexcept { return m_data.get(); }
	size_t size() const noexcept { return m_size; }

private:
	bool m_ok = false;
	size_t m_size = 0;
	std::unique_ptr<char[]> m_data;
};

struct Vertex {
	float x, y, z;
	float u, v;
};

// �e�N�X�`���̓ǂݍ��݂̎����p
HRESULT GetWICFactory(IWICImagingFactory** factory)
{
	static ComPtr<IWICImagingFactory> m_pWICFactory;
	HRESULT hr = S_OK;

	if (nullptr == m_pWICFactory) {
		hr = CoCreateInstance(
			CLSID_WICImagingFactory, nullptr,
			CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_pWICFactory));
	}

	if (SUCCEEDED(hr)) {
		*factory = m_pWICFactory.Get();
		(*factory)->AddRef();
	}

	return hr;
}

// �e�N�X�`���̓ǂݍ���
HRESULT LoadTexture(ID3D11Device* device, LPCWSTR filename, ID3D11ShaderResourceView** ppShaderResourceView)
{
	if (ppShaderResourceView == nullptr) {
		return S_OK;
	}

	HRESULT hr;
	UINT w, h;  // �摜�̃T�C�Y
	std::unique_ptr<BYTE[]> buf;  // �s�N�Z���̐F�l
	const auto targetFormat = GUID_WICPixelFormat32bppRGBA;

	{
		// PNG�摜���o�C�g��Ƃ��ēǂݍ���
		ComPtr<IWICBitmapDecoder> decoder;
		ComPtr<IWICBitmapFrameDecode> bitmapSource;
		ComPtr<IWICFormatConverter> converter;
		ComPtr<IWICImagingFactory> wicFactory;

		hr = GetWICFactory(&wicFactory);
		if (FAILED(hr)) { OutputDebugString(_T("FAILED: GetWICFactory\n")); return hr; }

		hr = wicFactory->CreateDecoderFromFilename(
			filename,
			nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder);
		if (FAILED(hr)) { OutputDebugString(_T("FAILED: CreateDecoderFromFilename\n")); return hr; }

		hr = decoder->GetFrame(0, &bitmapSource);
		if (FAILED(hr)) { OutputDebugString(_T("FAILED: GetFrame\n")); return hr; }

		hr = bitmapSource->GetSize(&w, &h);
		if (FAILED(hr)) { OutputDebugString(_T("FAILED: GetSize\n")); return hr; }
    	buf = std::make_unique<BYTE[]>(w * h * 4);

		WICPixelFormatGUID pixelFormat;
        hr = bitmapSource->GetPixelFormat(&pixelFormat);
		if (FAILED(hr)) { OutputDebugString(_T("FAILED: GetPixelFormat\n")); return hr; }

		// �t�H�[�}�b�g�𒼂�
		if (pixelFormat == targetFormat) {
			bitmapSource->CopyPixels(nullptr, w * 4, w * h * 4, buf.get());
		} else {
			hr = wicFactory->CreateFormatConverter(&converter);
			if (FAILED(hr)) { OutputDebugString(_T("FAILED: CreateFormatConverter\n")); return hr; }

			// GUID_WICPixelFormat32bppRGBA  --- ���ʂ̃A���t�@
			// GUID_WICPixelFormat32bppPRGBA --- ��Z�ς݃A���t�@
			hr = converter->Initialize(
				bitmapSource.Get(), targetFormat,
				WICBitmapDitherTypeErrorDiffusion, nullptr, 0.f,
				WICBitmapPaletteTypeMedianCut);
			if (FAILED(hr)) { OutputDebugString(_T("FAILED: Initialize\n")); return hr; }
			converter->CopyPixels(nullptr, w * 4, w * h * 4, buf.get());
		}
	}

	D3D11_TEXTURE2D_DESC texture2dDesc;
	texture2dDesc.Width = w;
	texture2dDesc.Height = h;
	texture2dDesc.MipLevels = 1;  // �~�j�}�b�v���쐬���Ȃ��B�����Ń~�j�}�b�v������̂̓n�[�h�E�F�A�I�ɑΉ����Ă��Ȃ��ꍇ�������A�G���[�ɂȂ邽�߁B
	texture2dDesc.ArraySize = 1;
	texture2dDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	texture2dDesc.SampleDesc.Count = 1;
	texture2dDesc.SampleDesc.Quality = 0;
	texture2dDesc.Usage = D3D11_USAGE_IMMUTABLE;  // �ύX�s��
	texture2dDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	texture2dDesc.CPUAccessFlags = 0;
	texture2dDesc.MiscFlags = 0;

	D3D11_SUBRESOURCE_DATA initialData;
	initialData.pSysMem = buf.get();
	initialData.SysMemPitch = w * 4;
	initialData.SysMemSlicePitch = w * h * 4;  // ����͈Ӗ��͂Ȃ�

	ComPtr<ID3D11Texture2D> texture2d;
	hr = device->CreateTexture2D(&texture2dDesc, &initialData, &texture2d);
	if (FAILED(hr)) { OutputDebugString(_T("FAILED: CreateTexture2D\n")); return hr; }

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
	srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = 1;
	
	hr = device->CreateShaderResourceView(texture2d.Get(), &srvDesc, ppShaderResourceView);
	if (FAILED(hr)) { OutputDebugString(_T("FAILED: CreateShaderResourceView\n")); return hr; }
	
	OutputDebugStringW(filename);
	OutputDebugStringW(L" read\n");
	return S_OK;
}

// �V���[�e�B���O�֘A
dxstg::Input _input;
std::list<std::unique_ptr<dxstg::StgObject>> _objects;
dxstg::Player* _player = nullptr;

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message) {
		case WM_KEYDOWN:
			switch (wParam) {
				case VK_LEFT:
					_input.left = true;
					break;
				case VK_RIGHT:
					_input.right = true;
					break;
				case VK_UP:
					_input.up = true;
					break;
				case VK_DOWN:
					_input.down = true;
					break;
			}
			break;
		case WM_KEYUP:
			switch (wParam) {
				case VK_LEFT:
					_input.left = false;
					break;
				case VK_RIGHT:
					_input.right = false;
					break;
				case VK_UP:
					_input.up = false;
					break;
				case VK_DOWN:
					_input.down = false;
					break;
			}
			break;
		case WM_CLOSE:
			PostMessage(hWnd, WM_DESTROY, 0, 0);
			break;
		case WM_DESTROY:
			PostQuitMessage(0);
			break;
	}

	return DefWindowProc(hWnd, message, wParam, lParam);
}

// �f�o�C�X�֘A���\�[�X
const TCHAR wndclassName[] = _T("DX11TutorialWindowClass");
HWND hWnd;
ComPtr<ID3D11Device> device;
ComPtr<IDXGISwapChain> swapChain;
ComPtr<ID3D11DeviceContext> immediateContext;
D3D_FEATURE_LEVEL featureLevel;
ComPtr<ID3D11Texture2D> backBuffer;
ComPtr<ID3D11RenderTargetView> renderTargetView;
D3D11_VIEWPORT viewports[1];
ComPtr<ID3D11ShaderResourceView> srvXchu;   // xchu�̃e�N�X�`��
ComPtr<ID3D11ShaderResourceView> srvBullet; // bullet�̃e�N�X�`��
ComPtr<ID3D11InputLayout> inputLayout;
ComPtr<ID3D11VertexShader> vertexShader;
ComPtr<ID3D11Buffer> vsCBuffer;
ComPtr<ID3D11PixelShader> pixelShader;
ComPtr<ID3D11Buffer> psCBuffer;
ComPtr<ID3D11SamplerState> psSamplerState;
ComPtr<ID3D11Buffer> vertexBuffer;
ComPtr<ID3D11RasterizerState> rasterizerState;
ComPtr<ID3D11BlendState> blendState;
std::unique_ptr<dxstg::FontTextureMap> font;

// ���\�[�X�̏�����
void Init(HINSTANCE hInstance)
{
	using namespace dxstg;

	// Window���쐬
	WNDCLASS wndclass;
	wndclass.style = 0;  // �����Ɏg����萔�ꗗ: https://docs.microsoft.com/ja-jp/windows/desktop/winmsg/window-class-styles
	wndclass.lpfnWndProc = WndProc;
	wndclass.cbClsExtra = 0;
	wndclass.cbWndExtra = 0;
	wndclass.hInstance = hInstance;
	wndclass.hIcon = nullptr;
	wndclass.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wndclass.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);  // �\�[�X��DirectX��Sample
	wndclass.lpszMenuName = nullptr;
	wndclass.lpszClassName = wndclassName;

	if (!RegisterClass(&wndclass)) { // �G���[
		OutputDebugStringW(L"failed: RegisterClass\n");
		throw 0;
	}

	// �N���C�A���g�̈悪 640 x 480 �̃E�B���h�E���쐬
	RECT rc = { 0, 0, dxstg::clientWidth, dxstg::clientHeight };
	AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, false);

	hWnd = CreateWindow(wndclassName, _T("���u���T���v��"),
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top, nullptr,
		nullptr, hInstance, nullptr);

	if (hWnd == NULL) { // �G���[
		OutputDebugStringW(L"failed: CreateWindow\n");
		throw 0;
	}

	// �������� DirectX11 �̏�����
	// �X���b�v�`�F�C���A�f�o�C�X�A�f�o�C�X�R���e�N�X�g�̍쐬
	{
		DXGI_SWAP_CHAIN_DESC swapChainDesc;
		swapChainDesc.BufferDesc.Width = clientWidth;   // ��ʉ𑜓x
		swapChainDesc.BufferDesc.Height = clientHeight; // ��ʉ𑜓x
		swapChainDesc.BufferDesc.RefreshRate.Numerator = 60; // ���t���b�V�����[�g�̕��q
		swapChainDesc.BufferDesc.RefreshRate.Denominator = 1; // ���t���b�V�����[�g�̕���
		// swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; // �t�H�[�}�b�g�B https://docs.microsoft.com/ja-jp/previous-versions/direct-x/ee418116%28v%3dvs.85%29
		swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // ���sRGB�B�������̂ق������̂܂܂Ō����邩�炱�����ɂ��Ă����B https://docs.microsoft.com/ja-jp/previous-versions/direct-x/ee418116%28v%3dvs.85%29
		swapChainDesc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;  // �������̏������w�肵�Ȃ�
		swapChainDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;  // �X�P�[��(?)
		swapChainDesc.SampleDesc.Count = 1;  // �s�N�Z���P�ʂ̃}���`�T���v�����O�̐�
		swapChainDesc.SampleDesc.Quality = 0;  // �C���[�W�̕i�����x���B0�͍Œ�B
		swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; // �o�̓����_�[�^�[�Q�b�g�Ƃ��Ďg�p�B������ DXGI_USAGE_SHADER_INPUT �������΃V�F�[�_�[�̓��͂Ƃ��Ă��g�p�\�B
		swapChainDesc.BufferCount = 2; // �t�����g�o�b�t�@���܂߂��o�b�t�@��
		swapChainDesc.OutputWindow = hWnd; // �o�͐�̃E�B���h�E
		swapChainDesc.Windowed = true; // �E�B���h�E���[�h
		swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;  // �o�b�N�o�b�t�@���t�����g�ɕ\��������̓o�b�N�o�b�t�@�̓��e��j������B
		swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;  // IDXGISwapChain::ResizeTarget �ɂ��E�B���h�E�T�C�Y�̕ύX�ɑΉ��B

		ThrowIfFailed(L"D3D11CreateDeviceAndSwapChain",
			D3D11CreateDeviceAndSwapChain( // ���t�@�����X: https://docs.microsoft.com/ja-jp/previous-versions/direct-x/ee416033%28v%3dvs.85%29
				nullptr, // �r�f�I�A�_�v�^�[(�r�f�I�J�[�h): nullptr�Ńf�t�H���g�̂��̂��g�p
				D3D_DRIVER_TYPE_HARDWARE, // �h���C�o�[: �n�[�h�E�F�A�h���C�o�[�B�����ɓ��삷��
				nullptr, // �\�t�g�E�F�A: ��Ńn�[�h�E�F�A�h���C�o�[���g�p����Ƃ��� nullptr ���w�肷��B
				0, // �t���O: �w��͂��Ȃ��B�V���O���X���b�h�ł̎g�p�͂���B https://docs.microsoft.com/ja-jp/previous-versions/direct-x/ee416076%28v%3dvs.85%29
				nullptr, // �쐬�����݂�@�\���x���̔z��Bnullptr�̏ꍇ�ő�̋@�\���x�����쐬����B
				0, // ��̔z��̗v�f��
				D3D11_SDK_VERSION, // SDK�̃o�[�W�����B���̒l���w�肷��B
				&swapChainDesc,  // ��Őݒ肵���X���b�v�`�F�C���̐ݒ�
				swapChain.ReleaseAndGetAddressOf(),  // �߂�l
				device.ReleaseAndGetAddressOf(),  // �߂�l
				&featureLevel,  // �߂�l
				immediateContext.ReleaseAndGetAddressOf()  // �߂�l
			));
	}

	switch (featureLevel) {
		case D3D_FEATURE_LEVEL_9_1:  OutputDebugString(_T("feature level 9.1\n")); break;
		case D3D_FEATURE_LEVEL_9_2:  OutputDebugString(_T("feature level 9.2\n")); break;
		case D3D_FEATURE_LEVEL_9_3:  OutputDebugString(_T("feature level 9.3\n")); break;
		case D3D_FEATURE_LEVEL_10_0: OutputDebugString(_T("feature level 10.0\n")); break;
		case D3D_FEATURE_LEVEL_10_1: OutputDebugString(_T("feature level 10.1\n")); break;
		case D3D_FEATURE_LEVEL_11_0: OutputDebugString(_T("feature level 11.0\n")); break;
		case D3D_FEATURE_LEVEL_11_1: OutputDebugString(_T("feature level 11.1\n")); break;
		case D3D_FEATURE_LEVEL_12_0: OutputDebugString(_T("feature level 12.0\n")); break;
		case D3D_FEATURE_LEVEL_12_1: OutputDebugString(_T("feature level 12.1\n")); break;
		default:
			break;
	}

	// �X���b�v�`�F�C������o�b�N�o�b�t�@�𓾂�
	ThrowIfFailed(L"SwapChain::GetBuffer",
		swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)backBuffer.ReleaseAndGetAddressOf()));

	// �����_�[�^�[�Q�b�g�r���[���쐬
	ThrowIfFailed(L"CreateRenderTargetView",
		device->CreateRenderTargetView(backBuffer.Get(), nullptr, renderTargetView.ReleaseAndGetAddressOf()));

	// �����_�[�^�[�Q�b�g��ݒ�
	// �[�x�X�e���V���͎g��Ȃ��B
	immediateContext->OMSetRenderTargets(1, renderTargetView.GetAddressOf(), nullptr);

	// �r���[�|�[�g��ݒ�
	viewports[0].Width = clientWidth;
	viewports[0].Height = clientHeight;
	viewports[0].MinDepth = 0.0f;
	viewports[0].MaxDepth = 1.0f;
	viewports[0].TopLeftX = 0;
	viewports[0].TopLeftY = 0;
	immediateContext->RSSetViewports(1, viewports);

	// DirectX11 �������I���


	// �e�N�X�`����ǂݍ��݁AShaderResourceView���쐬�B

	ThrowIfFailed(L"LoadTexture (xchu.png)",
		LoadTexture(device.Get(), L"data/xchu.png", &srvXchu)); // TODO

	ThrowIfFailed(L"LoadTexture (bullet.png)",
		LoadTexture(device.Get(), L"data/bullet.png", &srvBullet));

	// ���_�V�F�[�_�[���쐬
	{
		BinFile vsBin(L"data/VertexShader.cso");
		if (!vsBin) {
			OutputDebugStringW(L"failed: BinFile (VertexShader.cso)\n");
			throw 0;
		}
		ThrowIfFailed(L"CreateVertexShader",
			device->CreateVertexShader(vsBin.get(), vsBin.size(), nullptr, vertexShader.ReleaseAndGetAddressOf()));

		// �C���v�b�g���C�A�E�g�̍쐬
		D3D11_INPUT_ELEMENT_DESC inputElems[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(Vertex, u), D3D11_INPUT_PER_VERTEX_DATA, 0 }
		};
		ThrowIfFailed(L"CreateInputLayout",
			device->CreateInputLayout(inputElems, ARRAYSIZE(inputElems), vsBin.get(), vsBin.size(), inputLayout.ReleaseAndGetAddressOf()));
	}

	// ���_�V�F�[�_�̒萔�o�b�t�@���쐬
	{
		D3D11_BUFFER_DESC bufferDesc;
		bufferDesc.ByteWidth = 4 * 4 * 4;  // 16�̔{���ł���K�v������B
		bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
		bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		bufferDesc.MiscFlags = 0;

		ThrowIfFailed(L"CreateBuffer (vs cbuffer)",
			device->CreateBuffer(&bufferDesc, nullptr, vsCBuffer.ReleaseAndGetAddressOf()));
	}

	// �s�N�Z���V�F�[�_�[���쐬
	{
		BinFile psBin(L"data/PixelShader.cso");
		if (!psBin) {
			OutputDebugStringW(L"failed: BinFile (PixelShader.cso)\n");
			throw 0;
		}
		ThrowIfFailed(L"CreatePixelShader",
			device->CreatePixelShader(psBin.get(), psBin.size(), nullptr, pixelShader.ReleaseAndGetAddressOf()));
	}

	// �s�N�Z���V�F�[�_�[�̒萔�o�b�t�@���쐬
	// ���_�V�F�[�_�̒萔�o�b�t�@���쐬
	{
		D3D11_BUFFER_DESC bufferDesc;
		bufferDesc.ByteWidth = 16;  // 16�̔{���ł���K�v������B
		bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
		bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		bufferDesc.MiscFlags = 0;

		ThrowIfFailed(L"CreateBuffer (ps cbuffer)",
			device->CreateBuffer(&bufferDesc, nullptr, psCBuffer.ReleaseAndGetAddressOf()));
	}

	// �s�N�Z���V�F�[�_�[�̃T���v���[�X�e�[�g���쐬
	{
		D3D11_SAMPLER_DESC samplerDesc;
		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;  // �|�C���g�T���v�����O
		samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		// samplerDesc.MipLODBias = 0;  // �f�t�H���g
		// samplerDesc.MaxAnisotropy = 16;  // �f�t�H���g
		// samplerDesc.MinLOD = -FLT_MAX;  // �f�t�H���g
		// samplerDesc.MaxLOD = FLT_MAX;  // �f�t�H���g
		samplerDesc.MipLODBias = 0.0f;
		samplerDesc.MaxAnisotropy = 1;
		samplerDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
		samplerDesc.BorderColor[0] = 0;
		samplerDesc.BorderColor[1] = 0;
		samplerDesc.BorderColor[2] = 0;
		samplerDesc.BorderColor[3] = 0;
		samplerDesc.MinLOD = 0;
		samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

		ThrowIfFailed(L"CreateSamplerState",
			device->CreateSamplerState(&samplerDesc, psSamplerState.ReleaseAndGetAddressOf()));
	}

	// ���_�o�b�t�@���쐬
	{
		D3D11_BUFFER_DESC bufferDesc;
		bufferDesc.ByteWidth = 4 * sizeof(Vertex);  // 16�̔{���ł���K�v�͂Ȃ��B
		bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
		bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		bufferDesc.MiscFlags = 0;

		ThrowIfFailed(L"CreateBuffer (vertex buffer)",
			device->CreateBuffer(&bufferDesc, nullptr, vertexBuffer.ReleaseAndGetAddressOf()));
	}

	// ���X�^���C�U�[�X�e�[�g���쐬
	{
		D3D11_RASTERIZER_DESC rasterizerDesc;
		rasterizerDesc.FillMode = D3D11_FILL_SOLID;
		rasterizerDesc.CullMode = D3D11_CULL_NONE;
		rasterizerDesc.FrontCounterClockwise = false;
		rasterizerDesc.DepthBias = 0;
		rasterizerDesc.DepthBiasClamp = 0;
		rasterizerDesc.SlopeScaledDepthBias = 0;
		rasterizerDesc.DepthClipEnable = true;
		rasterizerDesc.ScissorEnable = false;
		rasterizerDesc.MultisampleEnable = false;
		rasterizerDesc.AntialiasedLineEnable = false;

		ThrowIfFailed(L"CreateRasterizerState",
			device->CreateRasterizerState(&rasterizerDesc, rasterizerState.ReleaseAndGetAddressOf()));
	}

	// �u�����h�X�e�[�g���쐬
	{
		D3D11_BLEND_DESC blendDesc;
		blendDesc.AlphaToCoverageEnable = false;
		blendDesc.IndependentBlendEnable = false;
		blendDesc.RenderTarget[0].BlendEnable = true;
		// blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;    // �A���t�@�u�����h����
		// blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ZERO;  // �A���t�@�u�����h����
		blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;      // �A���t�@�u�����h�L��
		blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA; // �A���t�@�u�����h�L��
		blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
		blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

		ThrowIfFailed(L"CreateBlendState",
			device->CreateBlendState(&blendDesc, blendState.ReleaseAndGetAddressOf()));
	}

	// �����_�����O�p�C�v���C���̐ݒ�
	{
		UINT strides[1] = { sizeof(Vertex) };
		UINT offsets[1] = { 0 };
		immediateContext->IASetVertexBuffers(0, 1, vertexBuffer.GetAddressOf(), strides, offsets);
		immediateContext->IASetInputLayout(inputLayout.Get());
		immediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
		immediateContext->VSSetShader(vertexShader.Get(), nullptr, 0);
		immediateContext->VSSetConstantBuffers(0, 1, vsCBuffer.GetAddressOf());
		immediateContext->PSSetShader(pixelShader.Get(), nullptr, 0);
		immediateContext->PSSetConstantBuffers(0, 1, psCBuffer.GetAddressOf());
		immediateContext->PSSetSamplers(0, 1, psSamplerState.GetAddressOf());
		immediateContext->RSSetState(rasterizerState.Get());

		float blendColor[] = { 1, 1, 1, 1 };
		immediateContext->OMSetBlendState(blendState.Get(), blendColor, 0xffffffff);
	}

	// LOGFONT�̉�� https://msdn.microsoft.com/ja-jp/windows/desktop/dd145037
	LOGFONTW logfont;
	logfont.lfHeight = 30;
	logfont.lfWidth = 0;
	logfont.lfEscapement = 0;
	logfont.lfOrientation = 0;
	logfont.lfWeight = FW_DONTCARE; // ����
	// logfont.lfWeight = FW_BOLD;  // �{�[���h
	logfont.lfItalic = FALSE;
	logfont.lfUnderline = FALSE;
	logfont.lfStrikeOut = FALSE;
	logfont.lfCharSet = SHIFTJIS_CHARSET;
	logfont.lfOutPrecision = OUT_TT_ONLY_PRECIS;
	logfont.lfClipPrecision = CLIP_DEFAULT_PRECIS;
	logfont.lfQuality = PROOF_QUALITY;
	logfont.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
	wchar_t fontName[] = L"���C���I";
	CopyMemory(logfont.lfFaceName, fontName, sizeof(fontName));

	font = std::make_unique<FontTextureMap>(device.Get(), logfont, false);

	// window ��\��
	ShowWindow(hWnd, SW_SHOW);
}

// ���\�[�X�̉��
void CleanUp(HINSTANCE hInstance)
{
	if (hWnd) {
		ShowWindow(hWnd, SW_HIDE); // window ���\��
	}

	font.reset();

	backBuffer.Reset();
	renderTargetView.Reset();
	srvXchu.Reset();
	srvBullet.Reset();
	inputLayout.Reset();
	vertexShader.Reset();
	vsCBuffer.Reset();
	pixelShader.Reset();
	psCBuffer.Reset();
	psSamplerState.Reset();
	vertexBuffer.Reset();
	rasterizerState.Reset();
	blendState.Reset();

	immediateContext.Reset();
	swapChain.Reset();
	device.Reset();

	if (hWnd) {
		DestroyWindow(hWnd);
	}
	UnregisterClassW(wndclassName, hInstance);
}

// ����� x, y �Ƃ��ĕ������`��
void DrawString(float x, float y, _In_z_ const wchar_t *str)
{
	const float x0 = x;
	for (; *str != L'\0'; ++str) {
		if (*str == L'\n') {
			x = x0;
			y += font->getTextMetric().tmHeight;
		} else {
			const auto& glyph = (*font)[*str];

			if (!iswspace(*str)) {
				// ���_���W��ݒ�
				// �Q�l: http://marupeke296.com/WINT_GetGlyphOutline.html

				D3D11_MAPPED_SUBRESOURCE subresource;
				immediateContext->Map(vertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &subresource);

				float xtemp = x + glyph.glyphmetrics.gmptGlyphOrigin.x;
				float ytemp = y + font->getTextMetric().tmAscent - glyph.glyphmetrics.gmptGlyphOrigin.y;

				auto vertexes = (Vertex*)subresource.pData;
				vertexes[0].x = xtemp;                                  vertexes[0].y = ytemp;
				vertexes[1].x = xtemp + glyph.glyphmetrics.gmBlackBoxX; vertexes[1].y = ytemp;
				vertexes[2].x = xtemp;                                  vertexes[2].y = ytemp + glyph.glyphmetrics.gmBlackBoxY;
				vertexes[3].x = xtemp + glyph.glyphmetrics.gmBlackBoxX; vertexes[3].y = ytemp + glyph.glyphmetrics.gmBlackBoxY;

				for (int i = 0; i < 4; ++i) {
					vertexes[i].z = 0.f;
				}

				vertexes[0].u = 0.f; vertexes[0].v = 0.f;
				vertexes[1].u = 1.f; vertexes[1].v = 0.f;
				vertexes[2].u = 0.f; vertexes[2].v = 1.f;
				vertexes[3].u = 1.f; vertexes[3].v = 1.f;

				immediateContext->Unmap(vertexBuffer.Get(), 0);

				// �e�N�X�`����ݒ�
				immediateContext->PSSetShaderResources(0, 1, glyph.shaderResourceView.GetAddressOf());

				// �`��
				immediateContext->Draw(4, 0);
			}

			x += glyph.glyphmetrics.gmCellIncX;
		}
	}
}

} // end unnamed namespace


namespace dxstg {

void AddObject(std::unique_ptr<StgObject>&& newObject)
{
	_objects.emplace_back(std::move(newObject));
}

Player* GetPlayer()
{
	return _player;
}

void SetPlayer(Player* p)
{
	_player = p;
}

Input GetInput()
{
	return _input;
}

} // end dxstg


int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
	using namespace dxstg;

	try {
		ThrowIfFailed(L"CoInitialize",
			CoInitialize(nullptr));  // �e�N�X�`���̓ǂݍ��݂�COM�̂��߁A������Ăяo���K�v������B

		OutputDebugString(_T("����ɂ���[���\n"));

		Init(hInstance);  // ���\�[�X������

		// �����Q�[���I�u�W�F�N�g�̒ǉ�
		{
			auto player = std::make_unique<Player>();
			SetPlayer(player.get());
			AddObject(std::move(player));
			AddObject(std::make_unique<Enemy>(3.f, 0.f));
		}

		//���C�����[�v
		double frameTime = 0.f;
		auto begin = std::chrono::high_resolution_clock::now();
		MSG hMsg;
		while (true) {
			// �E�B���h�E���b�Z�[�W����
			// ���̒��ŃL�[�{�[�h���͏����X�V�����
			while (PeekMessageW(&hMsg, NULL, 0, 0, PM_REMOVE)) {
				if (hMsg.message == WM_QUIT) {
					goto End;
				}
				TranslateMessage(&hMsg);
				DispatchMessage(&hMsg);
			}

			// ��ʂ̃N���A
			float clearColor[] = { 0.1f, 0.3f, 0.5f, 1.0f };
			immediateContext->ClearRenderTargetView(renderTargetView.Get(), clearColor);

			// �X�V
			for (const auto& obj : _objects) {
				obj->update();
			}

			// �폜�\�v�f�̍폜
			{
				auto it = std::remove_if(_objects.begin(), _objects.end(),
					[](const auto& obj) { return obj->removable; });
				_objects.erase(it, _objects.end());
			}

			// �Փ˔���̎��{
			for (auto it1 = _objects.begin(); it1 != _objects.end(); ++it1) {
				auto it2 = it1;
				++it2;
				for (; it2 != _objects.end(); ++it2) {
					if ((*it1)->getHitRect().intersects((*it2)->getHitRect())) {
						(*it1)->hit(**it2);
						(*it2)->hit(**it1);
					}
				}
			}

			// �폜�\�v�f�̍폜
			{
				auto it = std::remove_if(_objects.begin(), _objects.end(),
					[](const auto& obj) { return obj->removable; });
				_objects.erase(it, _objects.end());
			}

			// �����_�����O

			// �J�����̔z�u������
			{
				using namespace DirectX;
				XMMATRIX viewProj
					= XMMatrixLookAtLH(XMVectorSet(0, 0, -8, 1), XMVectorSet(0, 0, 0, 1), XMVectorSet(0, 1, 0, 1))
					* XMMatrixPerspectiveFovLH(XMConvertToRadians(45), (float)clientWidth / clientHeight, 0.1f, 100.f);

				D3D11_MAPPED_SUBRESOURCE subresource;
				immediateContext->Map(vsCBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &subresource);
				XMStoreFloat4x4((XMFLOAT4X4*)subresource.pData, XMMatrixTranspose(viewProj));  // �s��͓]�u���܂��B
				immediateContext->Unmap(vsCBuffer.Get(), 0);
			}

			// �I�u�W�F�N�g�̕`��
			for (const auto& obj : _objects) {
				// ���_���W��ݒ�
				{
					D3D11_MAPPED_SUBRESOURCE subresource;
					immediateContext->Map(vertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &subresource);

					auto vertexes = (Vertex*)subresource.pData;
					vertexes[0].x = obj->getDrawRect().maxX; vertexes[0].y = obj->getDrawRect().maxY;
					vertexes[1].x = obj->getDrawRect().minX; vertexes[1].y = obj->getDrawRect().maxY;
					vertexes[2].x = obj->getDrawRect().maxX; vertexes[2].y = obj->getDrawRect().minY;
					vertexes[3].x = obj->getDrawRect().minX; vertexes[3].y = obj->getDrawRect().minY;

					for (int i = 0; i < 4; ++i) {
						vertexes[i].z = 0.f;
					}

					vertexes[0].u = obj->isMirrorX() ? 0.f : 1.f; vertexes[0].v = obj->isMirrorY() ? 1.f : 0.f;
					vertexes[1].u = obj->isMirrorX() ? 1.f : 0.f; vertexes[1].v = obj->isMirrorY() ? 1.f : 0.f;
					vertexes[2].u = obj->isMirrorX() ? 0.f : 1.f; vertexes[2].v = obj->isMirrorY() ? 0.f : 1.f;
					vertexes[3].u = obj->isMirrorX() ? 1.f : 0.f; vertexes[3].v = obj->isMirrorY() ? 0.f : 1.f;

					immediateContext->Unmap(vertexBuffer.Get(), 0);
				}

				// �F��ݒ�
				{
					D3D11_MAPPED_SUBRESOURCE subresource;
					immediateContext->Map(psCBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &subresource);
					*reinterpret_cast<Color*>(subresource.pData) = obj->getColor();
					immediateContext->Unmap(psCBuffer.Get(), 0);
				}

				// �e�N�X�`����ݒ�
				ComPtr<ID3D11ShaderResourceView> selectedSrv;
				switch (obj->getTextureID()) {
					case StgObject::TextureID::XCHU:
						selectedSrv = srvXchu;
						break;
					case StgObject::TextureID::BULLET:
						selectedSrv = srvBullet;
						break;
				}
				immediateContext->PSSetShaderResources(0, 1, selectedSrv.GetAddressOf());

				// �`��
				immediateContext->Draw(4, 0);
			}

			// �����̕`��
			// �X�N���[�����W�n�ɐݒ�
			{
				using namespace DirectX;
				XMMATRIX viewProj = XMMatrixSet(
					2 / (float)clientWidth, 0, 0, 0,
					0, -2 / (float)clientHeight, 0, 0,
					0, 0, 1, 0,
					-1, 1, 0, 1
				);

				D3D11_MAPPED_SUBRESOURCE subresource;
				immediateContext->Map(vsCBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &subresource);
				XMStoreFloat4x4((XMFLOAT4X4*)subresource.pData, XMMatrixTranspose(viewProj));  // �s��͓]�u���܂��B
				immediateContext->Unmap(vsCBuffer.Get(), 0);
			}

			// �F��ݒ�
			{
				D3D11_MAPPED_SUBRESOURCE subresource;
				immediateContext->Map(psCBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &subresource);
				*reinterpret_cast<Color*>(subresource.pData) = Color(1, 1, 1, 0.8f);
				immediateContext->Unmap(psCBuffer.Get(), 0);
			}

			// ������`��
			{
				std::wostringstream buf;
				buf << L"fps: " << (1.0 / frameTime * 1000) << std::endl;
				buf << L"���{����������B";

				DrawString(0, 0, buf.str().c_str());
			}
			// �\��
			// ��������1�����邱�ƂŁA1�񐂒��������Ƃ�B
			swapChain->Present(1, 0);

			// �t���[�����[�g�̌v�Z
			auto end = std::chrono::high_resolution_clock::now();
			std::chrono::duration<double, std::milli> duration = end - begin;
			frameTime = frameTime * 0.95 + duration.count() * 0.05;

			begin = std::move(end);
		}

	End:
		CleanUp(hInstance);
	} catch (...) {
		CleanUp(hInstance);
	}

	// CoUninitialize();  // ���������Ⴄ��WICImagingFactory�̉���Ɏ��s����
	return 0;
}
