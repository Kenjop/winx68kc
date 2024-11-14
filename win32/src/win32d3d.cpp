/* -----------------------------------------------------------------------------------
  スクリーン管理層（Direct3D11）
                                                        (c) 2024- Kengo Takagi (Kenjo)
----------------------------------------------------------------------------------- */

#include "win32d3d.h"
#include <d3d11.h>
#include <d3dx11.h>
#include <directxmath.h>

#include "vs_0.h"
#include "ps_0.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dx11.lib")

using namespace DirectX;

typedef struct {
    XMFLOAT3 position;
    XMFLOAT2 uv;
} Vertex;

typedef struct {
    XMMATRIX m_WP;
} ConstantBuffer;

typedef struct {
	// 基礎情報
	HWND                       m_hwnd;                // ウィンドウハンドル
	INFO_SCRNBUF*              m_screen;              // エミュ出力スクリーンバッファ
	UINT32                     m_disp_w;              // 描画サイズ
	UINT32                     m_disp_h;
	UINT32                     m_win_w;               // ウィンドウサイズ
	UINT32                     m_win_h;
	ST_DISPAREA                m_area;                // CRTC表示領域情報
	UINT32                     m_stat_h;              // ステータスバーの高さ
	D3DDRAW_ASPECT             m_aspect;              // アスペクト比設定
	D3DDRAW_FILTER             m_filter;              // 補間の有無

	// D3Dオブジェクト
	ID3D11Device*              m_device;              // デバイスインターフェイス
	ID3D11DeviceContext*       m_context;             // コンテキスト
	IDXGISwapChain*            m_swapChain;           // スワップチェインインターフェイス
	ID3D11RenderTargetView*    m_renderTargetView;    // レンダーターゲットビュー
	ID3D11InputLayout*         m_layout;              // インプットレイアウト
	ID3D11VertexShader*        m_vertexShader;        // 頂点シェーダ
	ID3D11PixelShader*         m_pixelShader;         // ピクセルシェーダ
	ID3D11Buffer*              m_vertexBuffer;        // 頂点バッファ
	ID3D11Buffer*              m_indexBuffer;         // インデックスバッファ
	ID3D11Buffer*              m_constantBuffer;      // 定数バッファ
	ID3D11SamplerState*        m_sampler[2];          // テクスチャサンプラ
	ID3D11Texture2D*           m_texture;             // テクスチャ
	ID3D11ShaderResourceView*  m_shaderResourceView;  // シェーダリソースビュー
} INFO_D3DDRAW;


#define BASE_SCREEN_SIZE_W   768
#define BASE_SCREEN_SIZE_H   512

#define BASE_TEXTURE_SIZE_W  1024
#define BASE_TEXTURE_SIZE_H  512

#define SAFE_RELEASE(p)  if ( p ) { (p)->Release(); (p)=NULL; }


// --------------------------------------------------------------------------
//   内部関数
// --------------------------------------------------------------------------
// デバイス関連初期化
static BOOL InitDevice(INFO_D3DDRAW* di)
{
	static const D3D_DRIVER_TYPE types[] = {
		D3D_DRIVER_TYPE_HARDWARE,
		D3D_DRIVER_TYPE_WARP,
		D3D_DRIVER_TYPE_REFERENCE,
		D3D_DRIVER_TYPE_SOFTWARE,
	};

	// スワップチェインの作成
	DXGI_SWAP_CHAIN_DESC swapChainDesc;
	ZeroMemory(&swapChainDesc, sizeof(swapChainDesc));
	swapChainDesc.BufferDesc.Width  = BASE_SCREEN_SIZE_W;
	swapChainDesc.BufferDesc.Height = BASE_SCREEN_SIZE_H;
	swapChainDesc.BufferDesc.RefreshRate.Numerator = 60;
	swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
	swapChainDesc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;

	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.SampleDesc.Quality = 0;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = 1;
	swapChainDesc.OutputWindow = di->m_hwnd;
	swapChainDesc.Windowed = TRUE;

	// D3D_DRIVER_TYPE_HARDWARE から順に使えるものを探す（いつもの）
	HRESULT hr;
	for (size_t i=0; i<(sizeof(types)/sizeof(types[0])); i++)
	{
		hr = D3D11CreateDeviceAndSwapChain(
			nullptr,
			types[i],
			nullptr,
			0,
			nullptr,
			0,
			D3D11_SDK_VERSION,
			&swapChainDesc,
			&di->m_swapChain,
			&di->m_device,
			nullptr,
			&di->m_context
		);
		if ( SUCCEEDED(hr) ) break;
	}
	if ( FAILED(hr) )
	{
		LOG(("### D3DDraw : error, device does not support DirectX11."));
		return FALSE;
	}

	// D3Dが強制有効化するフルスクリーン受付を禁止
	IDXGIFactory* pfac = nullptr;
	if ( FAILED(di->m_swapChain->GetParent(__uuidof( IDXGIFactory ), (void**)&pfac)) )
	{
		LOG(("### D3DDraw : error in GetParent() of swapchain."));
		return FALSE;
	}
	pfac->MakeWindowAssociation(di->m_hwnd, DXGI_MWA_NO_WINDOW_CHANGES | DXGI_MWA_NO_ALT_ENTER);

	return TRUE;
}

static BOOL InitViewport(INFO_D3DDRAW* di)
{
	// 表示領域を作成
	D3D11_VIEWPORT viewport;
	ZeroMemory(&viewport, sizeof(viewport));
	viewport.Width  = (FLOAT)di->m_disp_w;
	viewport.Height = (FLOAT)di->m_disp_h;
	viewport.MinDepth = D3D11_MIN_DEPTH;    // 0.0f
	viewport.MaxDepth = D3D11_MAX_DEPTH;    // 1.0f
	di->m_context->RSSetViewports(1, &viewport);

	// バックバッファを作成
	ID3D11Texture2D* backBuffer;
	di->m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuffer));
	di->m_device->CreateRenderTargetView(backBuffer, nullptr, &di->m_renderTargetView);
	SAFE_RELEASE(backBuffer);

	// レンダーターゲットをバックバッファに設定
	di->m_context->OMSetRenderTargets(1, &di->m_renderTargetView, nullptr);

	return TRUE;
}

// シェーダ関連初期化
static BOOL InitShader(INFO_D3DDRAW* di)
{
	di->m_device->CreateVertexShader(&g_vs_0, sizeof(g_vs_0), NULL, &di->m_vertexShader);
	di->m_device->CreatePixelShader(&g_ps_0, sizeof(g_ps_0), NULL, &di->m_pixelShader);

	// 頂点インプットレイアウトを定義
	D3D11_INPUT_ELEMENT_DESC inputElementDescs[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};

	// インプットレイアウトのサイズ
	UINT numElements = sizeof(inputElementDescs) / sizeof(inputElementDescs[0]);

	// 頂点インプットレイアウトを作成
	if ( FAILED(di->m_device->CreateInputLayout(inputElementDescs, numElements, &g_vs_0, sizeof(g_vs_0), &di->m_layout)) )
	{
		LOG(("### D3DDraw : error in CreateInputLayout()."));
		return FALSE;
	}

	// 頂点インプットレイアウトをセット
	di->m_context->IASetInputLayout(di->m_layout);

	return TRUE;
}

// バッファ関連初期化
static BOOL InitBuffer(INFO_D3DDRAW* di)
{
	// 四角形のジオメトリを定義（初期ダミー）
	static const Vertex vertices[] = {
		{ { 0.0f,   0.0f,   0.0f }, { 0.0f,                                                 0.0f                                                 } },
		{ { 768.0f, 0.0f,   0.0f }, { (float)BASE_SCREEN_SIZE_W/(float)BASE_TEXTURE_SIZE_W, 0.0f                                                 } },
		{ { 0.0f,   512.0f, 0.0f }, { 0.0f,                                                 (float)BASE_SCREEN_SIZE_H/(float)BASE_TEXTURE_SIZE_H } },
		{ { 768.0f, 512.0f, 0.0f }, { (float)BASE_SCREEN_SIZE_W/(float)BASE_TEXTURE_SIZE_W, (float)BASE_SCREEN_SIZE_H/(float)BASE_TEXTURE_SIZE_H } },
	};

	// バッファを作成
	D3D11_BUFFER_DESC bufferDesc;
	ZeroMemory(&bufferDesc, sizeof(bufferDesc));

	// 頂点バッファの設定
	bufferDesc.Usage = D3D11_USAGE_DEFAULT;
	bufferDesc.ByteWidth = sizeof(Vertex) * 4;
	bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	bufferDesc.CPUAccessFlags = 0;

	// リソースの設定
	D3D11_SUBRESOURCE_DATA initData;
	ZeroMemory(&initData, sizeof(initData));
	initData.pSysMem = vertices;

	// 頂点バッファを作成
	if ( FAILED(di->m_device->CreateBuffer(&bufferDesc, &initData, &di->m_vertexBuffer)) )
	{
		LOG(("### D3DDraw : error in create vertex buffer."));
		return FALSE;
	}

	// 表示する頂点バッファを選択
	UINT stride = sizeof(Vertex);
	UINT offset = 0;
	di->m_context->IASetVertexBuffers(0, 1, &di->m_vertexBuffer, &stride, &offset);

	// 四角形のインデックスを定義
	static const WORD index[] = { /*Triangle1*/ 0, 1, 2,  /*Triangle2*/ 2, 1, 3 };

	// インデックス情報の追加
	bufferDesc.Usage = D3D11_USAGE_DEFAULT;
	bufferDesc.ByteWidth = sizeof(WORD) * 6;
	bufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	bufferDesc.CPUAccessFlags = 0;
	initData.pSysMem = index;

	// インデックスバッファを作成
	if ( FAILED(di->m_device->CreateBuffer(&bufferDesc, &initData, &di->m_indexBuffer)) )
	{
		LOG(("### D3DDraw : error in create index buffer."));
		return FALSE;
	}

	// 表示するインデックスバッファを選択
	di->m_context->IASetIndexBuffer(di->m_indexBuffer, DXGI_FORMAT_R16_UINT, 0);

	// 使用するプリミティブタイプを設定
	di->m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// 定数情報の追加
	bufferDesc.Usage = D3D11_USAGE_DEFAULT;
	bufferDesc.ByteWidth = sizeof(ConstantBuffer);
	bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bufferDesc.CPUAccessFlags = 0;

	// 定数バッファを作成
	if ( FAILED(di->m_device->CreateBuffer(&bufferDesc, nullptr, &di->m_constantBuffer)) )
	{
		LOG(("### D3DDraw : error in create constant buffer."));
		return FALSE;
	}

	return TRUE;
}

// テクスチャ関連初期化
static BOOL InitTexture(INFO_D3DDRAW* di)
{
	// サンプラの設定
	D3D11_SAMPLER_DESC samplerDesc;
	ZeroMemory(&samplerDesc, sizeof(samplerDesc));
	samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
	samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
	samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
	samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
	samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	samplerDesc.BorderColor[0] = 0.0f;
	samplerDesc.BorderColor[1] = 0.0f;
	samplerDesc.BorderColor[2] = 0.0f;
	samplerDesc.BorderColor[3] = 1.0f;
	samplerDesc.MinLOD = 0;
	samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
	if ( FAILED(di->m_device->CreateSamplerState(&samplerDesc, &di->m_sampler[0])) )
	{
		LOG(("### D3DDraw : error in create sampler[0] state."));
		return FALSE;
	}
	// 補間ありサンプラ
	samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	if ( FAILED(di->m_device->CreateSamplerState(&samplerDesc, &di->m_sampler[1])) )
	{
		LOG(("### D3DDraw : error in create sampler[1] state."));
		return FALSE;
	}


	// テクスチャの作成
	D3D11_TEXTURE2D_DESC texDesc;
	ZeroMemory(&texDesc, sizeof(texDesc));
	texDesc.Width  = BASE_TEXTURE_SIZE_W;
	texDesc.Height = BASE_TEXTURE_SIZE_H;
	texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	texDesc.ArraySize = 1;
	texDesc.MipLevels = 1;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	texDesc.Usage = D3D11_USAGE_DEFAULT;
	texDesc.MiscFlags = 0;
	texDesc.Usage = D3D11_USAGE_DYNAMIC;
	texDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	D3D11_SUBRESOURCE_DATA subres = { 0 };
	const D3D11_SUBRESOURCE_DATA *subresArray = 0;

	if ( FAILED(di->m_device->CreateTexture2D(&texDesc, subresArray, &di->m_texture)) )
	{
		LOG(("### D3DDraw : error in create texture."));
		return FALSE;
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
	D3D11_SRV_DIMENSION dimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	ZeroMemory(&srvDesc, sizeof(srvDesc));
	srvDesc.Format = texDesc.Format;
	srvDesc.ViewDimension = dimension;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = texDesc.MipLevels;
	if ( FAILED(di->m_device->CreateShaderResourceView(di->m_texture, &srvDesc, &di->m_shaderResourceView)) )
	{
		LOG(("### D3DDraw : error in create resource view."));
		return FALSE;
	}

	return TRUE;
}

// マトリックス関連初期化
// VertexShader がワールド変換マトリクスを反映してるので一応残してるが、そもそもシェーダ側の処理要らん気がする
static BOOL InitMatrix(INFO_D3DDRAW* di)
{
	// ワールドマトリックスの初期化
	XMMATRIX world = XMMatrixIdentity();  // 引数なしで全ての軸で0.0f（移動なし）

	// プロジェクションマトリックスの初期化（射影行列変換）
	XMMATRIX projection = XMMatrixOrthographicOffCenterLH(0.0f, BASE_SCREEN_SIZE_W, BASE_SCREEN_SIZE_H, 0.0f, 0.0f, 1.0f);

	// 変数代入
	ConstantBuffer cBuffer;
	cBuffer.m_WP = XMMatrixTranspose(world * projection);

	// GPU（シェーダ側）へ転送
	di->m_context->UpdateSubresource(di->m_constantBuffer, 0, nullptr, &cBuffer, 0, 0);

	return TRUE;
}

// D3D初期化
static BOOL D3D_Init(INFO_D3DDRAW* di)
{
	do {
		// デバイス初期化
		if ( !InitDevice(di) ) break;

		// ビューポート初期化
		if ( !InitViewport(di) ) break;

		// シェーダ初期化
		if ( !InitShader(di) ) break;

		// バッファ初期化
		if ( !InitBuffer(di) ) break;

		// テクスチャ初期化
		if ( !InitTexture(di) ) break;

		// マトリックス初期化
		if ( !InitMatrix(di) ) break;

		return TRUE;
	} while ( 0 );

	return FALSE;
}

// D3D破棄
static void D3D_Destroy(INFO_D3DDRAW* di)
{
	if ( di ) {
		SAFE_RELEASE(di->m_device);
		SAFE_RELEASE(di->m_context);
		SAFE_RELEASE(di->m_swapChain);
		SAFE_RELEASE(di->m_renderTargetView);
		SAFE_RELEASE(di->m_layout);
		SAFE_RELEASE(di->m_vertexShader);
		SAFE_RELEASE(di->m_pixelShader);
		SAFE_RELEASE(di->m_vertexBuffer);
		SAFE_RELEASE(di->m_indexBuffer);
		SAFE_RELEASE(di->m_constantBuffer);
		SAFE_RELEASE(di->m_sampler[0]);
		SAFE_RELEASE(di->m_sampler[1]);
		SAFE_RELEASE(di->m_shaderResourceView);
	}
}

// デバイスリセット
static BOOL D3D_Reset(INFO_D3DDRAW* di)
{
	return TRUE;
}

// ウィンドウサイズ変更
static BOOL D3D_Resize(INFO_D3DDRAW* di)
{
	// スワップチェインへのリファレンスを解除
	ID3D11RenderTargetView* nullViews [] = { nullptr };
	di->m_context->OMSetRenderTargets(ARRAYSIZE(nullViews), nullViews, nullptr);
	SAFE_RELEASE(di->m_renderTargetView);  // XXX これでいいのか？
	di->m_context->Flush();

	// リサイズ実行
	HRESULT hr = di->m_swapChain->ResizeBuffers(1, di->m_disp_w, di->m_disp_h + di->m_stat_h, DXGI_FORMAT_B8G8R8A8_UNORM, 0);
	if ( FAILED(hr) )
	{
		// XXX デバイスリセットが必要
		// 現在のD3Dではデバイスロストは限定条件下でしか起こらないので、現状無視しちゃっていいかな…
		LOG(("### D3DDraw : error in resizing swapchain."));
		return FALSE;
	}

	// ビューポート再作成
	if ( !InitViewport(di) ) return FALSE;

//LOG(("D3DDraw:Resize() : changed window size -> %d,%d", di->m_disp_w, di->m_disp_h));

	return TRUE;
}

// Vertexバッファの更新（X68000サイドの解像度/CRTC設定変更）
static void D3D_UpdateVertex(INFO_D3DDRAW* di)
{
	float w, h;
	float win_w = (float)di->m_win_w, win_h = (float)di->m_win_h - (float)di->m_stat_h;
	float padx, pady;
	float emu_w = (float)di->m_area.disp.x2 - (float)di->m_area.disp.x1;
	float emu_h = (float)di->m_area.disp.y2 - (float)di->m_area.disp.y1;
	float t_w = (float)di->m_area.scrn.x2 - (float)di->m_area.scrn.x1;
	float t_h = (float)di->m_area.scrn.y2 - (float)di->m_area.scrn.y1;
	float x0 = (float)di->m_area.disp.x1 - (float)di->m_area.scrn.x1;
	float y0 = (float)di->m_area.disp.y1 - (float)di->m_area.scrn.y1;
	float x1 = (float)di->m_area.disp.x2 - (float)di->m_area.scrn.x1;
	float y1 = (float)di->m_area.disp.y2 - (float)di->m_area.scrn.y1;

	// まずアスペクト比設定に従って、拡大率を決める
	switch ( di->m_aspect )
	{
	case D3DDRAW_ASPECT_3_2:
		w = win_h*3.0f / 2.0f;
		h = win_w*2.0f / 3.0f;
		if ( w > win_w ) {
			w = win_w;
			h = win_w*2.0f / 3.0f;
		} else if ( h > win_h ) {
			w = win_h*3.0f / 2.0f;
			h = win_h;
		}
		break;
	case D3DDRAW_ASPECT_4_3:
		w = win_h*4.0f / 3.0f;
		h = win_w*3.0f / 4.0f;
		if ( w > win_w ) {
			w = win_w;
			h = win_w*3.0f / 4.0f;
		} else if ( h > win_h ) {
			w = win_h*4.0f / 3.0f;
			h = win_h;
		}
		break;
	case D3DDRAW_ASPECT_FREE:  // 使ってない。フルスクリーンで画面一杯に描きたいときとか用
	default:
		w = win_w;
		h = win_h;
		break;
	}

	// 頂点範囲確定
	w = ( w * BASE_SCREEN_SIZE_W ) / win_w;
	h = ( h * BASE_SCREEN_SIZE_H ) / win_h;
	padx = ( (float)BASE_SCREEN_SIZE_W - w ) / 2.0f;
	pady = ( (float)BASE_SCREEN_SIZE_H - h ) / 2.0f;

	// 0～t_w/h を基準にした各頂点を 0～w/h の範囲に落とし込む
	x0 = (x0*w) / t_w;
	x1 = (x1*w) / t_w;
	y0 = (y0*h) / t_h;
	y1 = (y1*h) / t_h;

	// 頂点リストを更新
	Vertex vertices[] = {
		{ { x0+padx, y0+pady, 0.0f }, { 0.0f,                             0.0f                             } },
		{ { x1+padx, y0+pady, 0.0f }, { emu_w/(float)BASE_TEXTURE_SIZE_W, 0.0f                             } },
		{ { x0+padx, y1+pady, 0.0f }, { 0.0f,                             emu_h/(float)BASE_TEXTURE_SIZE_H } },
		{ { x1+padx, y1+pady, 0.0f }, { emu_w/(float)BASE_TEXTURE_SIZE_W, emu_h/(float)BASE_TEXTURE_SIZE_H } },
	};
	di->m_context->UpdateSubresource(di->m_vertexBuffer, 0, NULL, vertices, 0, 0);
}


// --------------------------------------------------------------------------
//   公開関数
// --------------------------------------------------------------------------
D3DDRAWHDL D3DDraw_Create(HWND hwnd, INFO_SCRNBUF* info)
{
	INFO_D3DDRAW* di = NULL;

	do {
		// DRAWMGRINFO構造体を確保
		di = (INFO_D3DDRAW*)_MALLOC(sizeof(INFO_D3DDRAW), "D3DDraw info struct");

		if ( !di ) break;
		memset(di, 0, sizeof(INFO_D3DDRAW));

		if ( !info ) break;
		if ( !info->ptr ) break;
		if ( (info->w<=0)&&(info->h<=0) ) break;

		di->m_hwnd = hwnd;
		di->m_screen = info;
		di->m_aspect = D3DDRAW_ASPECT_3_2;
		di->m_disp_w = BASE_SCREEN_SIZE_W;
		di->m_disp_h = BASE_SCREEN_SIZE_H;
		di->m_win_w = BASE_SCREEN_SIZE_W;
		di->m_win_h = BASE_SCREEN_SIZE_H;
		di->m_area.scrn.x1 = di->m_area.disp.x1 = 0;
		di->m_area.scrn.x2 = di->m_area.disp.x2 = BASE_SCREEN_SIZE_W;
		di->m_area.scrn.y1 = di->m_area.disp.y1 = 0;
		di->m_area.scrn.y2 = di->m_area.disp.y2 = BASE_SCREEN_SIZE_H;

		// D3D関連初期化
		if ( !D3D_Init(di) ) break;

		// ここまでたどり着いたら成功。DRAWINFO構造体のポインタをハンドルとして返す
		LOG(("D3DDraw : Initialized."));
		return ((D3DDRAWHDL)di);
	} while ( 0 );

	// どこかで失敗したらここに来る
	D3DDraw_Dispose((D3DDRAWHDL)di);
	LOG(("D3DDraw : Initialization failed."));
	return NULL;
}

void D3DDraw_Dispose(D3DDRAWHDL hdi)
{
	INFO_D3DDRAW* di = (INFO_D3DDRAW*)hdi;

	// DRAWMGRINFO構造体があれば破棄
	if ( di ) {
		// D3D破棄
		D3D_Destroy(di);

		// DRAWINFO構造体を破棄
		_MFREE(di);
	}
}

void D3DDraw_Draw(D3DDRAWHDL hdi, BOOL wait, const ST_DISPAREA* area, BOOL enable)
{
	INFO_D3DDRAW* di = (INFO_D3DDRAW*)hdi;

	if ( di )
	{
		// 描画がが有効でないフレーム（CRTC値が大きく変動した直後のフレーム）の場合、テクスチャ更新しない
		if ( enable )
		{
			// X68000側の画面解像度が変わっていたら、頂点リストを更新
			if ( memcmp(&di->m_area, area, sizeof(ST_DISPAREA)) ) {
				memcpy(&di->m_area, area, sizeof(ST_DISPAREA));
				D3D_UpdateVertex(di);
			}

			// マップしてスクリーンバッファ内容をテクスチャにコピー
			D3D11_MAPPED_SUBRESOURCE mappedResource;
			if ( SUCCEEDED(di->m_context->Map(di->m_texture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource)) )
			{
				const UINT32 sbpl = di->m_screen->bpl;
				const UINT32 dbpl = BASE_TEXTURE_SIZE_W*4;
				const UINT8* src = di->m_screen->ptr;
				const UINT32 w = di->m_screen->w;
				UINT32 h = di->m_screen->h;
				UINT8* dst = (UINT8*)mappedResource.pData;

				// w が 1024 になる設定使うことはほぼない…はずだが存在はするので一応チェック
				if ( di->m_screen->w < BASE_TEXTURE_SIZE_W ) {
					// 補間時に表示範囲外ピクセルを巻き込むので、1ピクセル外を埋めておく
					while ( h-- ) {
						memcpy(dst, src, w*4);
						memset(dst+w*4, 0, 4);
						src += sbpl;
						dst += dbpl;
					}
					// 補間時に表示範囲外ピクセルを巻き込むので、1ピクセル外を埋めておく
					if ( di->m_screen->h < BASE_TEXTURE_SIZE_H ) {
						memset(dst, 0, w*4+4);
					}
				} else {
					// 1024幅の場合は埋める必要なし
					while ( h-- ) {
						memcpy(dst, src, BASE_TEXTURE_SIZE_W*4);
						src += sbpl;
						dst += dbpl;
					}
					// 高さ方向はチェックして必要なら埋める
					if ( di->m_screen->h < BASE_TEXTURE_SIZE_H ) {
						memset(dst, 0, BASE_TEXTURE_SIZE_W*4);
					}
				}

				di->m_context->Unmap(di->m_texture, 0);
			}
		}

		// レンダーターゲットビューをクリア
		static const FLOAT clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
		di->m_context->ClearRenderTargetView(di->m_renderTargetView, clearColor);

		// GPUバッファをセット
		di->m_context->VSSetShader(di->m_vertexShader, nullptr, 0);
		di->m_context->PSSetShader(di->m_pixelShader, nullptr, 0);
		di->m_context->VSSetConstantBuffers(0, 1, &di->m_constantBuffer);

		// テクスチャをシェーダに登録
		di->m_context->PSSetSamplers(0, 1, &di->m_sampler[di->m_filter]);
		di->m_context->PSSetShaderResources(0, 1, &di->m_shaderResourceView);

		// インデックスバッファをバックバッファに描画
		di->m_context->DrawIndexed(6, 0, 0);

		// フレームを最終出力
		di->m_swapChain->Present((wait)?1:0, 0);
	}
}

void D3DDraw_Resize(D3DDRAWHDL hdi, UINT32 w, UINT32 h)
{
	// バックバッファのサイズ変更（描画サイズ変更時）
	INFO_D3DDRAW* di = (INFO_D3DDRAW*)hdi;
	if ( di ) {
		if ( w!=di->m_disp_w || h!=di->m_disp_h ) {
			di->m_disp_w = w;
			di->m_disp_h = h;
			D3D_Resize(di);
			D3D_UpdateVertex(di);
		}
	}
}

void D3DDraw_SetAspect(D3DDRAWHDL hdi, D3DDRAW_ASPECT n)
{
	// アスペクト設定変更時
	INFO_D3DDRAW* di = (INFO_D3DDRAW*)hdi;
	if ( di ) {
		if ( di->m_aspect != n ) {
			di->m_aspect = n;
			D3D_UpdateVertex(di);
		}
	}
}

void D3DDraw_SetStatArea(D3DDRAWHDL hdi, UINT32 n)
{
	// ステータスバーはクライアント領域に含まれるので、それを除外して計算するための値を設定する
	INFO_D3DDRAW* di = (INFO_D3DDRAW*)hdi;
	if ( di ) {
		if ( di->m_stat_h != n ) {
			di->m_stat_h = n;
			D3D_Resize(di);
			D3D_UpdateVertex(di);
		}
	}
}

void D3DDraw_UpdateWindowSize(D3DDRAWHDL hdi, UINT32 w, UINT32 h)
{
	// ウィンドウサイズ変化（WM_SIZE）時
	// メニューやステータスバーの表示に変化があった時など
	INFO_D3DDRAW* di = (INFO_D3DDRAW*)hdi;
	if ( di ) {
		if ( ( w!=di->m_win_w || h!=di->m_win_h ) && ( w!=0 ) && (h!=0) ) {
			di->m_win_w = w;
			di->m_win_h = h;
			D3D_UpdateVertex(di);
		}
	}
}

void D3DDraw_SetFilter(D3DDRAWHDL hdi, D3DDRAW_FILTER n)
{
	// 補間の有無の設定 サンプラの切り替えで対応している
	INFO_D3DDRAW* di = (INFO_D3DDRAW*)hdi;
	if ( di ) {
		di->m_filter = n;
	}
}
