// 標準ライブラリ
#include <iostream>
#include <chrono>
#include <sstream>
#include <memory>
#include <list>
#include <algorithm>
#include <fstream>

// Windows系ライブラリ
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <tchar.h>
#include <wrl/client.h>
#include <wincodec.h>
#include <d3d11.h>
#include <DirectXMath.h>  // 行列の演算など

// 自作ヘッダー
#include "Common.h"
#include "FontTextureMap.h"
#include "Game.h"
#include "StgObject.h"

// ライブラリファイルのリンク
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

// テクスチャの読み込みの実装用
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

// テクスチャの読み込み
HRESULT LoadTexture(ID3D11Device* device, LPCWSTR filename, ID3D11ShaderResourceView** ppShaderResourceView)
{
	if (ppShaderResourceView == nullptr) {
		return S_OK;
	}

	HRESULT hr;
	UINT w, h;  // 画像のサイズ
	std::unique_ptr<BYTE[]> buf;  // ピクセルの色値
	const auto targetFormat = GUID_WICPixelFormat32bppRGBA;

	{
		// PNG画像をバイト列として読み込む
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

		// フォーマットを直す
		if (pixelFormat == targetFormat) {
			bitmapSource->CopyPixels(nullptr, w * 4, w * h * 4, buf.get());
		} else {
			hr = wicFactory->CreateFormatConverter(&converter);
			if (FAILED(hr)) { OutputDebugString(_T("FAILED: CreateFormatConverter\n")); return hr; }

			// GUID_WICPixelFormat32bppRGBA  --- 普通のアルファ
			// GUID_WICPixelFormat32bppPRGBA --- 乗算済みアルファ
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
	texture2dDesc.MipLevels = 1;  // ミニマップを作成しない。ここでミニマップをつくるのはハードウェア的に対応していない場合が多く、エラーになるため。
	texture2dDesc.ArraySize = 1;
	texture2dDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	texture2dDesc.SampleDesc.Count = 1;
	texture2dDesc.SampleDesc.Quality = 0;
	texture2dDesc.Usage = D3D11_USAGE_IMMUTABLE;  // 変更不可
	texture2dDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	texture2dDesc.CPUAccessFlags = 0;
	texture2dDesc.MiscFlags = 0;

	D3D11_SUBRESOURCE_DATA initialData;
	initialData.pSysMem = buf.get();
	initialData.SysMemPitch = w * 4;
	initialData.SysMemSlicePitch = w * h * 4;  // これは意味はない

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

// シューティング関連
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

// デバイス関連リソース
const TCHAR wndclassName[] = _T("DX11TutorialWindowClass");
HWND hWnd;
ComPtr<ID3D11Device> device;
ComPtr<IDXGISwapChain> swapChain;
ComPtr<ID3D11DeviceContext> immediateContext;
D3D_FEATURE_LEVEL featureLevel;
ComPtr<ID3D11Texture2D> backBuffer;
ComPtr<ID3D11RenderTargetView> renderTargetView;
D3D11_VIEWPORT viewports[1];
ComPtr<ID3D11ShaderResourceView> srvXchu;   // xchuのテクスチャ
ComPtr<ID3D11ShaderResourceView> srvBullet; // bulletのテクスチャ
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

// リソースの初期化
void Init(HINSTANCE hInstance)
{
	using namespace dxstg;

	// Windowを作成
	WNDCLASS wndclass;
	wndclass.style = 0;  // ここに使える定数一覧: https://docs.microsoft.com/ja-jp/windows/desktop/winmsg/window-class-styles
	wndclass.lpfnWndProc = WndProc;
	wndclass.cbClsExtra = 0;
	wndclass.cbWndExtra = 0;
	wndclass.hInstance = hInstance;
	wndclass.hIcon = nullptr;
	wndclass.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wndclass.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);  // ソースはDirectXのSample
	wndclass.lpszMenuName = nullptr;
	wndclass.lpszClassName = wndclassName;

	if (!RegisterClass(&wndclass)) { // エラー
		OutputDebugStringW(L"failed: RegisterClass\n");
		throw 0;
	}

	// クライアント領域が 640 x 480 のウィンドウを作成
	RECT rc = { 0, 0, dxstg::clientWidth, dxstg::clientHeight };
	AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, false);

	hWnd = CreateWindow(wndclassName, _T("裏講座サンプル"),
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top, nullptr,
		nullptr, hInstance, nullptr);

	if (hWnd == NULL) { // エラー
		OutputDebugStringW(L"failed: CreateWindow\n");
		throw 0;
	}

	// ここから DirectX11 の初期化
	// スワップチェイン、デバイス、デバイスコンテクストの作成
	{
		DXGI_SWAP_CHAIN_DESC swapChainDesc;
		swapChainDesc.BufferDesc.Width = clientWidth;   // 画面解像度
		swapChainDesc.BufferDesc.Height = clientHeight; // 画面解像度
		swapChainDesc.BufferDesc.RefreshRate.Numerator = 60; // リフレッシュレートの分子
		swapChainDesc.BufferDesc.RefreshRate.Denominator = 1; // リフレッシュレートの分母
		// swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; // フォーマット。 https://docs.microsoft.com/ja-jp/previous-versions/direct-x/ee418116%28v%3dvs.85%29
		swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // 上はsRGB。こっちのほうがそのままで見えるからこっちにしておく。 https://docs.microsoft.com/ja-jp/previous-versions/direct-x/ee418116%28v%3dvs.85%29
		swapChainDesc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;  // 走査線の順序を指定しない
		swapChainDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;  // スケール(?)
		swapChainDesc.SampleDesc.Count = 1;  // ピクセル単位のマルチサンプリングの数
		swapChainDesc.SampleDesc.Quality = 0;  // イメージの品質レベル。0は最低。
		swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; // 出力レンダーターゲットとして使用。ここに DXGI_USAGE_SHADER_INPUT も入れればシェーダーの入力としても使用可能。
		swapChainDesc.BufferCount = 2; // フロントバッファも含めたバッファ数
		swapChainDesc.OutputWindow = hWnd; // 出力先のウィンドウ
		swapChainDesc.Windowed = true; // ウィンドウモード
		swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;  // バックバッファをフロントに表示した後はバックバッファの内容を破棄する。
		swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;  // IDXGISwapChain::ResizeTarget によるウィンドウサイズの変更に対応。

		ThrowIfFailed(L"D3D11CreateDeviceAndSwapChain",
			D3D11CreateDeviceAndSwapChain( // リファレンス: https://docs.microsoft.com/ja-jp/previous-versions/direct-x/ee416033%28v%3dvs.85%29
				nullptr, // ビデオアダプター(ビデオカード): nullptrでデフォルトのものを使用
				D3D_DRIVER_TYPE_HARDWARE, // ドライバー: ハードウェアドライバー。高速に動作する
				nullptr, // ソフトウェア: 上でハードウェアドライバーを使用するときは nullptr を指定する。
				0, // フラグ: 指定はしない。シングルスレッドでの使用はあり。 https://docs.microsoft.com/ja-jp/previous-versions/direct-x/ee416076%28v%3dvs.85%29
				nullptr, // 作成を試みる機能レベルの配列。nullptrの場合最大の機能レベルを作成する。
				0, // 上の配列の要素数
				D3D11_SDK_VERSION, // SDKのバージョン。この値を指定する。
				&swapChainDesc,  // 上で設定したスワップチェインの設定
				swapChain.ReleaseAndGetAddressOf(),  // 戻り値
				device.ReleaseAndGetAddressOf(),  // 戻り値
				&featureLevel,  // 戻り値
				immediateContext.ReleaseAndGetAddressOf()  // 戻り値
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

	// スワップチェインからバックバッファを得る
	ThrowIfFailed(L"SwapChain::GetBuffer",
		swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)backBuffer.ReleaseAndGetAddressOf()));

	// レンダーターゲットビューを作成
	ThrowIfFailed(L"CreateRenderTargetView",
		device->CreateRenderTargetView(backBuffer.Get(), nullptr, renderTargetView.ReleaseAndGetAddressOf()));

	// レンダーターゲットを設定
	// 深度ステンシルは使わない。
	immediateContext->OMSetRenderTargets(1, renderTargetView.GetAddressOf(), nullptr);

	// ビューポートを設定
	viewports[0].Width = clientWidth;
	viewports[0].Height = clientHeight;
	viewports[0].MinDepth = 0.0f;
	viewports[0].MaxDepth = 1.0f;
	viewports[0].TopLeftX = 0;
	viewports[0].TopLeftY = 0;
	immediateContext->RSSetViewports(1, viewports);

	// DirectX11 初期化終わり


	// テクスチャを読み込み、ShaderResourceViewを作成。

	ThrowIfFailed(L"LoadTexture (xchu.png)",
		LoadTexture(device.Get(), L"data/xchu.png", &srvXchu)); // TODO

	ThrowIfFailed(L"LoadTexture (bullet.png)",
		LoadTexture(device.Get(), L"data/bullet.png", &srvBullet));

	// 頂点シェーダーを作成
	{
		BinFile vsBin(L"data/VertexShader.cso");
		if (!vsBin) {
			OutputDebugStringW(L"failed: BinFile (VertexShader.cso)\n");
			throw 0;
		}
		ThrowIfFailed(L"CreateVertexShader",
			device->CreateVertexShader(vsBin.get(), vsBin.size(), nullptr, vertexShader.ReleaseAndGetAddressOf()));

		// インプットレイアウトの作成
		D3D11_INPUT_ELEMENT_DESC inputElems[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(Vertex, u), D3D11_INPUT_PER_VERTEX_DATA, 0 }
		};
		ThrowIfFailed(L"CreateInputLayout",
			device->CreateInputLayout(inputElems, ARRAYSIZE(inputElems), vsBin.get(), vsBin.size(), inputLayout.ReleaseAndGetAddressOf()));
	}

	// 頂点シェーダの定数バッファを作成
	{
		D3D11_BUFFER_DESC bufferDesc;
		bufferDesc.ByteWidth = 4 * 4 * 4;  // 16の倍数である必要がある。
		bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
		bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		bufferDesc.MiscFlags = 0;

		ThrowIfFailed(L"CreateBuffer (vs cbuffer)",
			device->CreateBuffer(&bufferDesc, nullptr, vsCBuffer.ReleaseAndGetAddressOf()));
	}

	// ピクセルシェーダーを作成
	{
		BinFile psBin(L"data/PixelShader.cso");
		if (!psBin) {
			OutputDebugStringW(L"failed: BinFile (PixelShader.cso)\n");
			throw 0;
		}
		ThrowIfFailed(L"CreatePixelShader",
			device->CreatePixelShader(psBin.get(), psBin.size(), nullptr, pixelShader.ReleaseAndGetAddressOf()));
	}

	// ピクセルシェーダーの定数バッファを作成
	// 頂点シェーダの定数バッファを作成
	{
		D3D11_BUFFER_DESC bufferDesc;
		bufferDesc.ByteWidth = 16;  // 16の倍数である必要がある。
		bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
		bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		bufferDesc.MiscFlags = 0;

		ThrowIfFailed(L"CreateBuffer (ps cbuffer)",
			device->CreateBuffer(&bufferDesc, nullptr, psCBuffer.ReleaseAndGetAddressOf()));
	}

	// ピクセルシェーダーのサンプラーステートを作成
	{
		D3D11_SAMPLER_DESC samplerDesc;
		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;  // ポイントサンプリング
		samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		// samplerDesc.MipLODBias = 0;  // デフォルト
		// samplerDesc.MaxAnisotropy = 16;  // デフォルト
		// samplerDesc.MinLOD = -FLT_MAX;  // デフォルト
		// samplerDesc.MaxLOD = FLT_MAX;  // デフォルト
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

	// 頂点バッファを作成
	{
		D3D11_BUFFER_DESC bufferDesc;
		bufferDesc.ByteWidth = 4 * sizeof(Vertex);  // 16の倍数である必要はない。
		bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
		bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		bufferDesc.MiscFlags = 0;

		ThrowIfFailed(L"CreateBuffer (vertex buffer)",
			device->CreateBuffer(&bufferDesc, nullptr, vertexBuffer.ReleaseAndGetAddressOf()));
	}

	// ラスタライザーステートを作成
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

	// ブレンドステートを作成
	{
		D3D11_BLEND_DESC blendDesc;
		blendDesc.AlphaToCoverageEnable = false;
		blendDesc.IndependentBlendEnable = false;
		blendDesc.RenderTarget[0].BlendEnable = true;
		// blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;    // アルファブレンド無し
		// blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ZERO;  // アルファブレンド無し
		blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;      // アルファブレンド有り
		blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA; // アルファブレンド有り
		blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
		blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

		ThrowIfFailed(L"CreateBlendState",
			device->CreateBlendState(&blendDesc, blendState.ReleaseAndGetAddressOf()));
	}

	// レンダリングパイプラインの設定
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

	// LOGFONTの解説 https://msdn.microsoft.com/ja-jp/windows/desktop/dd145037
	LOGFONTW logfont;
	logfont.lfHeight = 30;
	logfont.lfWidth = 0;
	logfont.lfEscapement = 0;
	logfont.lfOrientation = 0;
	logfont.lfWeight = FW_DONTCARE; // 普通
	// logfont.lfWeight = FW_BOLD;  // ボールド
	logfont.lfItalic = FALSE;
	logfont.lfUnderline = FALSE;
	logfont.lfStrikeOut = FALSE;
	logfont.lfCharSet = SHIFTJIS_CHARSET;
	logfont.lfOutPrecision = OUT_TT_ONLY_PRECIS;
	logfont.lfClipPrecision = CLIP_DEFAULT_PRECIS;
	logfont.lfQuality = PROOF_QUALITY;
	logfont.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
	wchar_t fontName[] = L"メイリオ";
	CopyMemory(logfont.lfFaceName, fontName, sizeof(fontName));

	font = std::make_unique<FontTextureMap>(device.Get(), logfont, false);

	// window を表示
	ShowWindow(hWnd, SW_SHOW);
}

// リソースの解放
void CleanUp(HINSTANCE hInstance)
{
	if (hWnd) {
		ShowWindow(hWnd, SW_HIDE); // window を非表示
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

// 左上を x, y として文字列を描画
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
				// 頂点座標を設定
				// 参考: http://marupeke296.com/WINT_GetGlyphOutline.html

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

				// テクスチャを設定
				immediateContext->PSSetShaderResources(0, 1, glyph.shaderResourceView.GetAddressOf());

				// 描画
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
			CoInitialize(nullptr));  // テクスチャの読み込みがCOMのため、これを呼び出す必要がある。

		OutputDebugString(_T("こんにちわーるど\n"));

		Init(hInstance);  // リソース初期化

		// 初期ゲームオブジェクトの追加
		{
			auto player = std::make_unique<Player>();
			SetPlayer(player.get());
			AddObject(std::move(player));
			AddObject(std::make_unique<Enemy>(3.f, 0.f));
		}

		//メインループ
		double frameTime = 0.f;
		auto begin = std::chrono::high_resolution_clock::now();
		MSG hMsg;
		while (true) {
			// ウィンドウメッセージ処理
			// この中でキーボード入力情報も更新される
			while (PeekMessageW(&hMsg, NULL, 0, 0, PM_REMOVE)) {
				if (hMsg.message == WM_QUIT) {
					goto End;
				}
				TranslateMessage(&hMsg);
				DispatchMessage(&hMsg);
			}

			// 画面のクリア
			float clearColor[] = { 0.1f, 0.3f, 0.5f, 1.0f };
			immediateContext->ClearRenderTargetView(renderTargetView.Get(), clearColor);

			// 更新
			for (const auto& obj : _objects) {
				obj->update();
			}

			// 削除可能要素の削除
			{
				auto it = std::remove_if(_objects.begin(), _objects.end(),
					[](const auto& obj) { return obj->removable; });
				_objects.erase(it, _objects.end());
			}

			// 衝突判定の実施
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

			// 削除可能要素の削除
			{
				auto it = std::remove_if(_objects.begin(), _objects.end(),
					[](const auto& obj) { return obj->removable; });
				_objects.erase(it, _objects.end());
			}

			// レンダリング

			// カメラの配置を決定
			{
				using namespace DirectX;
				XMMATRIX viewProj
					= XMMatrixLookAtLH(XMVectorSet(0, 0, -8, 1), XMVectorSet(0, 0, 0, 1), XMVectorSet(0, 1, 0, 1))
					* XMMatrixPerspectiveFovLH(XMConvertToRadians(45), (float)clientWidth / clientHeight, 0.1f, 100.f);

				D3D11_MAPPED_SUBRESOURCE subresource;
				immediateContext->Map(vsCBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &subresource);
				XMStoreFloat4x4((XMFLOAT4X4*)subresource.pData, XMMatrixTranspose(viewProj));  // 行列は転置します。
				immediateContext->Unmap(vsCBuffer.Get(), 0);
			}

			// オブジェクトの描画
			for (const auto& obj : _objects) {
				// 頂点座標を設定
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

				// 色を設定
				{
					D3D11_MAPPED_SUBRESOURCE subresource;
					immediateContext->Map(psCBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &subresource);
					*reinterpret_cast<Color*>(subresource.pData) = obj->getColor();
					immediateContext->Unmap(psCBuffer.Get(), 0);
				}

				// テクスチャを設定
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

				// 描画
				immediateContext->Draw(4, 0);
			}

			// 文字の描画
			// スクリーン座標系に設定
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
				XMStoreFloat4x4((XMFLOAT4X4*)subresource.pData, XMMatrixTranspose(viewProj));  // 行列は転置します。
				immediateContext->Unmap(vsCBuffer.Get(), 0);
			}

			// 色を設定
			{
				D3D11_MAPPED_SUBRESOURCE subresource;
				immediateContext->Map(psCBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &subresource);
				*reinterpret_cast<Color*>(subresource.pData) = Color(1, 1, 1, 0.8f);
				immediateContext->Unmap(psCBuffer.Get(), 0);
			}

			// 文字を描画
			{
				std::wostringstream buf;
				buf << L"fps: " << (1.0 / frameTime * 1000) << std::endl;
				buf << L"日本語も書けるよ。";

				DrawString(0, 0, buf.str().c_str());
			}
			// 表示
			// 第一引数に1を入れることで、1回垂直同期をとる。
			swapChain->Present(1, 0);

			// フレームレートの計算
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

	// CoUninitialize();  // これやっちゃうとWICImagingFactoryの解放に失敗する
	return 0;
}
