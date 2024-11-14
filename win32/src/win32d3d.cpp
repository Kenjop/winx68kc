/* -----------------------------------------------------------------------------------
  �X�N���[���Ǘ��w�iDirect3D11�j
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
	// ��b���
	HWND                       m_hwnd;                // �E�B���h�E�n���h��
	INFO_SCRNBUF*              m_screen;              // �G�~���o�̓X�N���[���o�b�t�@
	UINT32                     m_disp_w;              // �`��T�C�Y
	UINT32                     m_disp_h;
	UINT32                     m_win_w;               // �E�B���h�E�T�C�Y
	UINT32                     m_win_h;
	ST_DISPAREA                m_area;                // CRTC�\���̈���
	UINT32                     m_stat_h;              // �X�e�[�^�X�o�[�̍���
	D3DDRAW_ASPECT             m_aspect;              // �A�X�y�N�g��ݒ�
	D3DDRAW_FILTER             m_filter;              // ��Ԃ̗L��

	// D3D�I�u�W�F�N�g
	ID3D11Device*              m_device;              // �f�o�C�X�C���^�[�t�F�C�X
	ID3D11DeviceContext*       m_context;             // �R���e�L�X�g
	IDXGISwapChain*            m_swapChain;           // �X���b�v�`�F�C���C���^�[�t�F�C�X
	ID3D11RenderTargetView*    m_renderTargetView;    // �����_�[�^�[�Q�b�g�r���[
	ID3D11InputLayout*         m_layout;              // �C���v�b�g���C�A�E�g
	ID3D11VertexShader*        m_vertexShader;        // ���_�V�F�[�_
	ID3D11PixelShader*         m_pixelShader;         // �s�N�Z���V�F�[�_
	ID3D11Buffer*              m_vertexBuffer;        // ���_�o�b�t�@
	ID3D11Buffer*              m_indexBuffer;         // �C���f�b�N�X�o�b�t�@
	ID3D11Buffer*              m_constantBuffer;      // �萔�o�b�t�@
	ID3D11SamplerState*        m_sampler[2];          // �e�N�X�`���T���v��
	ID3D11Texture2D*           m_texture;             // �e�N�X�`��
	ID3D11ShaderResourceView*  m_shaderResourceView;  // �V�F�[�_���\�[�X�r���[
} INFO_D3DDRAW;


#define BASE_SCREEN_SIZE_W   768
#define BASE_SCREEN_SIZE_H   512

#define BASE_TEXTURE_SIZE_W  1024
#define BASE_TEXTURE_SIZE_H  512

#define SAFE_RELEASE(p)  if ( p ) { (p)->Release(); (p)=NULL; }


// --------------------------------------------------------------------------
//   �����֐�
// --------------------------------------------------------------------------
// �f�o�C�X�֘A������
static BOOL InitDevice(INFO_D3DDRAW* di)
{
	static const D3D_DRIVER_TYPE types[] = {
		D3D_DRIVER_TYPE_HARDWARE,
		D3D_DRIVER_TYPE_WARP,
		D3D_DRIVER_TYPE_REFERENCE,
		D3D_DRIVER_TYPE_SOFTWARE,
	};

	// �X���b�v�`�F�C���̍쐬
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

	// D3D_DRIVER_TYPE_HARDWARE ���珇�Ɏg������̂�T���i�����́j
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

	// D3D�������L��������t���X�N���[����t���֎~
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
	// �\���̈���쐬
	D3D11_VIEWPORT viewport;
	ZeroMemory(&viewport, sizeof(viewport));
	viewport.Width  = (FLOAT)di->m_disp_w;
	viewport.Height = (FLOAT)di->m_disp_h;
	viewport.MinDepth = D3D11_MIN_DEPTH;    // 0.0f
	viewport.MaxDepth = D3D11_MAX_DEPTH;    // 1.0f
	di->m_context->RSSetViewports(1, &viewport);

	// �o�b�N�o�b�t�@���쐬
	ID3D11Texture2D* backBuffer;
	di->m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuffer));
	di->m_device->CreateRenderTargetView(backBuffer, nullptr, &di->m_renderTargetView);
	SAFE_RELEASE(backBuffer);

	// �����_�[�^�[�Q�b�g���o�b�N�o�b�t�@�ɐݒ�
	di->m_context->OMSetRenderTargets(1, &di->m_renderTargetView, nullptr);

	return TRUE;
}

// �V�F�[�_�֘A������
static BOOL InitShader(INFO_D3DDRAW* di)
{
	di->m_device->CreateVertexShader(&g_vs_0, sizeof(g_vs_0), NULL, &di->m_vertexShader);
	di->m_device->CreatePixelShader(&g_ps_0, sizeof(g_ps_0), NULL, &di->m_pixelShader);

	// ���_�C���v�b�g���C�A�E�g���`
	D3D11_INPUT_ELEMENT_DESC inputElementDescs[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};

	// �C���v�b�g���C�A�E�g�̃T�C�Y
	UINT numElements = sizeof(inputElementDescs) / sizeof(inputElementDescs[0]);

	// ���_�C���v�b�g���C�A�E�g���쐬
	if ( FAILED(di->m_device->CreateInputLayout(inputElementDescs, numElements, &g_vs_0, sizeof(g_vs_0), &di->m_layout)) )
	{
		LOG(("### D3DDraw : error in CreateInputLayout()."));
		return FALSE;
	}

	// ���_�C���v�b�g���C�A�E�g���Z�b�g
	di->m_context->IASetInputLayout(di->m_layout);

	return TRUE;
}

// �o�b�t�@�֘A������
static BOOL InitBuffer(INFO_D3DDRAW* di)
{
	// �l�p�`�̃W�I���g�����`�i�����_�~�[�j
	static const Vertex vertices[] = {
		{ { 0.0f,   0.0f,   0.0f }, { 0.0f,                                                 0.0f                                                 } },
		{ { 768.0f, 0.0f,   0.0f }, { (float)BASE_SCREEN_SIZE_W/(float)BASE_TEXTURE_SIZE_W, 0.0f                                                 } },
		{ { 0.0f,   512.0f, 0.0f }, { 0.0f,                                                 (float)BASE_SCREEN_SIZE_H/(float)BASE_TEXTURE_SIZE_H } },
		{ { 768.0f, 512.0f, 0.0f }, { (float)BASE_SCREEN_SIZE_W/(float)BASE_TEXTURE_SIZE_W, (float)BASE_SCREEN_SIZE_H/(float)BASE_TEXTURE_SIZE_H } },
	};

	// �o�b�t�@���쐬
	D3D11_BUFFER_DESC bufferDesc;
	ZeroMemory(&bufferDesc, sizeof(bufferDesc));

	// ���_�o�b�t�@�̐ݒ�
	bufferDesc.Usage = D3D11_USAGE_DEFAULT;
	bufferDesc.ByteWidth = sizeof(Vertex) * 4;
	bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	bufferDesc.CPUAccessFlags = 0;

	// ���\�[�X�̐ݒ�
	D3D11_SUBRESOURCE_DATA initData;
	ZeroMemory(&initData, sizeof(initData));
	initData.pSysMem = vertices;

	// ���_�o�b�t�@���쐬
	if ( FAILED(di->m_device->CreateBuffer(&bufferDesc, &initData, &di->m_vertexBuffer)) )
	{
		LOG(("### D3DDraw : error in create vertex buffer."));
		return FALSE;
	}

	// �\�����钸�_�o�b�t�@��I��
	UINT stride = sizeof(Vertex);
	UINT offset = 0;
	di->m_context->IASetVertexBuffers(0, 1, &di->m_vertexBuffer, &stride, &offset);

	// �l�p�`�̃C���f�b�N�X���`
	static const WORD index[] = { /*Triangle1*/ 0, 1, 2,  /*Triangle2*/ 2, 1, 3 };

	// �C���f�b�N�X���̒ǉ�
	bufferDesc.Usage = D3D11_USAGE_DEFAULT;
	bufferDesc.ByteWidth = sizeof(WORD) * 6;
	bufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	bufferDesc.CPUAccessFlags = 0;
	initData.pSysMem = index;

	// �C���f�b�N�X�o�b�t�@���쐬
	if ( FAILED(di->m_device->CreateBuffer(&bufferDesc, &initData, &di->m_indexBuffer)) )
	{
		LOG(("### D3DDraw : error in create index buffer."));
		return FALSE;
	}

	// �\������C���f�b�N�X�o�b�t�@��I��
	di->m_context->IASetIndexBuffer(di->m_indexBuffer, DXGI_FORMAT_R16_UINT, 0);

	// �g�p����v���~�e�B�u�^�C�v��ݒ�
	di->m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// �萔���̒ǉ�
	bufferDesc.Usage = D3D11_USAGE_DEFAULT;
	bufferDesc.ByteWidth = sizeof(ConstantBuffer);
	bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bufferDesc.CPUAccessFlags = 0;

	// �萔�o�b�t�@���쐬
	if ( FAILED(di->m_device->CreateBuffer(&bufferDesc, nullptr, &di->m_constantBuffer)) )
	{
		LOG(("### D3DDraw : error in create constant buffer."));
		return FALSE;
	}

	return TRUE;
}

// �e�N�X�`���֘A������
static BOOL InitTexture(INFO_D3DDRAW* di)
{
	// �T���v���̐ݒ�
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
	// ��Ԃ���T���v��
	samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	if ( FAILED(di->m_device->CreateSamplerState(&samplerDesc, &di->m_sampler[1])) )
	{
		LOG(("### D3DDraw : error in create sampler[1] state."));
		return FALSE;
	}


	// �e�N�X�`���̍쐬
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

// �}�g���b�N�X�֘A������
// VertexShader �����[���h�ϊ��}�g���N�X�𔽉f���Ă�̂ňꉞ�c���Ă邪�A���������V�F�[�_���̏����v���C������
static BOOL InitMatrix(INFO_D3DDRAW* di)
{
	// ���[���h�}�g���b�N�X�̏�����
	XMMATRIX world = XMMatrixIdentity();  // �����Ȃ��őS�Ă̎���0.0f�i�ړ��Ȃ��j

	// �v���W�F�N�V�����}�g���b�N�X�̏������i�ˉe�s��ϊ��j
	XMMATRIX projection = XMMatrixOrthographicOffCenterLH(0.0f, BASE_SCREEN_SIZE_W, BASE_SCREEN_SIZE_H, 0.0f, 0.0f, 1.0f);

	// �ϐ����
	ConstantBuffer cBuffer;
	cBuffer.m_WP = XMMatrixTranspose(world * projection);

	// GPU�i�V�F�[�_���j�֓]��
	di->m_context->UpdateSubresource(di->m_constantBuffer, 0, nullptr, &cBuffer, 0, 0);

	return TRUE;
}

// D3D������
static BOOL D3D_Init(INFO_D3DDRAW* di)
{
	do {
		// �f�o�C�X������
		if ( !InitDevice(di) ) break;

		// �r���[�|�[�g������
		if ( !InitViewport(di) ) break;

		// �V�F�[�_������
		if ( !InitShader(di) ) break;

		// �o�b�t�@������
		if ( !InitBuffer(di) ) break;

		// �e�N�X�`��������
		if ( !InitTexture(di) ) break;

		// �}�g���b�N�X������
		if ( !InitMatrix(di) ) break;

		return TRUE;
	} while ( 0 );

	return FALSE;
}

// D3D�j��
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

// �f�o�C�X���Z�b�g
static BOOL D3D_Reset(INFO_D3DDRAW* di)
{
	return TRUE;
}

// �E�B���h�E�T�C�Y�ύX
static BOOL D3D_Resize(INFO_D3DDRAW* di)
{
	// �X���b�v�`�F�C���ւ̃��t�@�����X������
	ID3D11RenderTargetView* nullViews [] = { nullptr };
	di->m_context->OMSetRenderTargets(ARRAYSIZE(nullViews), nullViews, nullptr);
	SAFE_RELEASE(di->m_renderTargetView);  // XXX ����ł����̂��H
	di->m_context->Flush();

	// ���T�C�Y���s
	HRESULT hr = di->m_swapChain->ResizeBuffers(1, di->m_disp_w, di->m_disp_h + di->m_stat_h, DXGI_FORMAT_B8G8R8A8_UNORM, 0);
	if ( FAILED(hr) )
	{
		// XXX �f�o�C�X���Z�b�g���K�v
		// ���݂�D3D�ł̓f�o�C�X���X�g�͌���������ł����N����Ȃ��̂ŁA���󖳎���������Ă������ȁc
		LOG(("### D3DDraw : error in resizing swapchain."));
		return FALSE;
	}

	// �r���[�|�[�g�č쐬
	if ( !InitViewport(di) ) return FALSE;

//LOG(("D3DDraw:Resize() : changed window size -> %d,%d", di->m_disp_w, di->m_disp_h));

	return TRUE;
}

// Vertex�o�b�t�@�̍X�V�iX68000�T�C�h�̉𑜓x/CRTC�ݒ�ύX�j
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

	// �܂��A�X�y�N�g��ݒ�ɏ]���āA�g�嗦�����߂�
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
	case D3DDRAW_ASPECT_FREE:  // �g���ĂȂ��B�t���X�N���[���ŉ�ʈ�t�ɕ`�������Ƃ��Ƃ��p
	default:
		w = win_w;
		h = win_h;
		break;
	}

	// ���_�͈͊m��
	w = ( w * BASE_SCREEN_SIZE_W ) / win_w;
	h = ( h * BASE_SCREEN_SIZE_H ) / win_h;
	padx = ( (float)BASE_SCREEN_SIZE_W - w ) / 2.0f;
	pady = ( (float)BASE_SCREEN_SIZE_H - h ) / 2.0f;

	// 0�`t_w/h ����ɂ����e���_�� 0�`w/h �͈̔͂ɗ��Ƃ�����
	x0 = (x0*w) / t_w;
	x1 = (x1*w) / t_w;
	y0 = (y0*h) / t_h;
	y1 = (y1*h) / t_h;

	// ���_���X�g���X�V
	Vertex vertices[] = {
		{ { x0+padx, y0+pady, 0.0f }, { 0.0f,                             0.0f                             } },
		{ { x1+padx, y0+pady, 0.0f }, { emu_w/(float)BASE_TEXTURE_SIZE_W, 0.0f                             } },
		{ { x0+padx, y1+pady, 0.0f }, { 0.0f,                             emu_h/(float)BASE_TEXTURE_SIZE_H } },
		{ { x1+padx, y1+pady, 0.0f }, { emu_w/(float)BASE_TEXTURE_SIZE_W, emu_h/(float)BASE_TEXTURE_SIZE_H } },
	};
	di->m_context->UpdateSubresource(di->m_vertexBuffer, 0, NULL, vertices, 0, 0);
}


// --------------------------------------------------------------------------
//   ���J�֐�
// --------------------------------------------------------------------------
D3DDRAWHDL D3DDraw_Create(HWND hwnd, INFO_SCRNBUF* info)
{
	INFO_D3DDRAW* di = NULL;

	do {
		// DRAWMGRINFO�\���̂��m��
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

		// D3D�֘A������
		if ( !D3D_Init(di) ) break;

		// �����܂ł��ǂ蒅�����琬���BDRAWINFO�\���̂̃|�C���^���n���h���Ƃ��ĕԂ�
		LOG(("D3DDraw : Initialized."));
		return ((D3DDRAWHDL)di);
	} while ( 0 );

	// �ǂ����Ŏ��s�����炱���ɗ���
	D3DDraw_Dispose((D3DDRAWHDL)di);
	LOG(("D3DDraw : Initialization failed."));
	return NULL;
}

void D3DDraw_Dispose(D3DDRAWHDL hdi)
{
	INFO_D3DDRAW* di = (INFO_D3DDRAW*)hdi;

	// DRAWMGRINFO�\���̂�����Δj��
	if ( di ) {
		// D3D�j��
		D3D_Destroy(di);

		// DRAWINFO�\���̂�j��
		_MFREE(di);
	}
}

void D3DDraw_Draw(D3DDRAWHDL hdi, BOOL wait, const ST_DISPAREA* area, BOOL enable)
{
	INFO_D3DDRAW* di = (INFO_D3DDRAW*)hdi;

	if ( di )
	{
		// �`�悪���L���łȂ��t���[���iCRTC�l���傫���ϓ���������̃t���[���j�̏ꍇ�A�e�N�X�`���X�V���Ȃ�
		if ( enable )
		{
			// X68000���̉�ʉ𑜓x���ς���Ă�����A���_���X�g���X�V
			if ( memcmp(&di->m_area, area, sizeof(ST_DISPAREA)) ) {
				memcpy(&di->m_area, area, sizeof(ST_DISPAREA));
				D3D_UpdateVertex(di);
			}

			// �}�b�v���ăX�N���[���o�b�t�@���e���e�N�X�`���ɃR�s�[
			D3D11_MAPPED_SUBRESOURCE mappedResource;
			if ( SUCCEEDED(di->m_context->Map(di->m_texture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource)) )
			{
				const UINT32 sbpl = di->m_screen->bpl;
				const UINT32 dbpl = BASE_TEXTURE_SIZE_W*4;
				const UINT8* src = di->m_screen->ptr;
				const UINT32 w = di->m_screen->w;
				UINT32 h = di->m_screen->h;
				UINT8* dst = (UINT8*)mappedResource.pData;

				// w �� 1024 �ɂȂ�ݒ�g�����Ƃ͂قڂȂ��c�͂��������݂͂���̂ňꉞ�`�F�b�N
				if ( di->m_screen->w < BASE_TEXTURE_SIZE_W ) {
					// ��Ԏ��ɕ\���͈͊O�s�N�Z�����������ނ̂ŁA1�s�N�Z���O�𖄂߂Ă���
					while ( h-- ) {
						memcpy(dst, src, w*4);
						memset(dst+w*4, 0, 4);
						src += sbpl;
						dst += dbpl;
					}
					// ��Ԏ��ɕ\���͈͊O�s�N�Z�����������ނ̂ŁA1�s�N�Z���O�𖄂߂Ă���
					if ( di->m_screen->h < BASE_TEXTURE_SIZE_H ) {
						memset(dst, 0, w*4+4);
					}
				} else {
					// 1024���̏ꍇ�͖��߂�K�v�Ȃ�
					while ( h-- ) {
						memcpy(dst, src, BASE_TEXTURE_SIZE_W*4);
						src += sbpl;
						dst += dbpl;
					}
					// ���������̓`�F�b�N���ĕK�v�Ȃ疄�߂�
					if ( di->m_screen->h < BASE_TEXTURE_SIZE_H ) {
						memset(dst, 0, BASE_TEXTURE_SIZE_W*4);
					}
				}

				di->m_context->Unmap(di->m_texture, 0);
			}
		}

		// �����_�[�^�[�Q�b�g�r���[���N���A
		static const FLOAT clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
		di->m_context->ClearRenderTargetView(di->m_renderTargetView, clearColor);

		// GPU�o�b�t�@���Z�b�g
		di->m_context->VSSetShader(di->m_vertexShader, nullptr, 0);
		di->m_context->PSSetShader(di->m_pixelShader, nullptr, 0);
		di->m_context->VSSetConstantBuffers(0, 1, &di->m_constantBuffer);

		// �e�N�X�`�����V�F�[�_�ɓo�^
		di->m_context->PSSetSamplers(0, 1, &di->m_sampler[di->m_filter]);
		di->m_context->PSSetShaderResources(0, 1, &di->m_shaderResourceView);

		// �C���f�b�N�X�o�b�t�@���o�b�N�o�b�t�@�ɕ`��
		di->m_context->DrawIndexed(6, 0, 0);

		// �t���[�����ŏI�o��
		di->m_swapChain->Present((wait)?1:0, 0);
	}
}

void D3DDraw_Resize(D3DDRAWHDL hdi, UINT32 w, UINT32 h)
{
	// �o�b�N�o�b�t�@�̃T�C�Y�ύX�i�`��T�C�Y�ύX���j
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
	// �A�X�y�N�g�ݒ�ύX��
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
	// �X�e�[�^�X�o�[�̓N���C�A���g�̈�Ɋ܂܂��̂ŁA��������O���Čv�Z���邽�߂̒l��ݒ肷��
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
	// �E�B���h�E�T�C�Y�ω��iWM_SIZE�j��
	// ���j���[��X�e�[�^�X�o�[�̕\���ɕω������������Ȃ�
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
	// ��Ԃ̗L���̐ݒ� �T���v���̐؂�ւ��őΉ����Ă���
	INFO_D3DDRAW* di = (INFO_D3DDRAW*)hdi;
	if ( di ) {
		di->m_filter = n;
	}
}
