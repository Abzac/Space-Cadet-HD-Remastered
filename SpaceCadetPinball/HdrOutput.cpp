#include "pch.h"
#include "HdrOutput.h"

#if defined(_WIN32) && defined(_MSC_VER)
#include "render.h"
#include "winmain.h"
#include "fullscrn.h"
#include "pb.h"
#include "TPinballTable.h"
#include "TTextBox.h"
#include <SDL_syswm.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <dcomp.h>
#include <wrl/client.h>
#include <memory>

using Microsoft::WRL::ComPtr;
namespace
{
	constexpr unsigned MaxRegions = 256;
	constexpr unsigned MatrixCount = 5;
	HdrOutput::MatrixMeasurement matrixReadback[MatrixCount]{};
	float lastPeak = 0.f;
	const char* status = "HDR has not been requested";
	char failure[180]{};
	bool succeeded(HRESULT hr, const char* stage)
	{
		if (SUCCEEDED(hr)) return true;
		SDL_snprintf(failure,sizeof(failure),"%s (0x%08x)",stage,unsigned(hr)); status=failure;
		return false;
	}
	struct FrameConstants
	{
		struct Rect {float x,y,rx,ry;};
		Rect regions[MaxRegions];
		Rect displays[MatrixCount];
		float white, peak, pad[2];
		uint32_t covered[MaxRegions][8];
	};
	// DAT owns the apertures. The two message panels are followed by score, ball and player.
	void displayRects(SDL_Rect (&rects)[MatrixCount])
	{
		unsigned index=0;
		for (auto box : {pb::InfoTextBox,pb::MissTextBox})
		{
			if (box) rects[index]=fullscrn::GetScreenRectFromPinballRect({box->OffsetX,box->OffsetY,box->Width,box->Height});
			++index;
		}
		auto table=pb::MainTable;
		if (table) for (auto counter : {table->CurScoreStruct,table->ScoreBallcount,table->ScorePlayerNumber1})
		{
			if (counter) rects[index]=fullscrn::GetScreenRectFromPinballRect({counter->OffsetX,counter->OffsetY,counter->Width,counter->Height});
			++index;
		}
	}
	float halfValue(uint16_t bits)
	{
		int exponent=(bits>>10)&31;
		if (!exponent) return std::ldexp(float(bits&1023),-24);
		return std::ldexp(1.f+(bits&1023)/1024.f,exponent-15);
	}

	// The first draw converts the whole SDR frame to linear scRGB. The second
	// draws only the small ellipses corresponding to live lamps and the ball.
	const char shader[] = R"(
cbuffer Frame : register(b0) { float4 regions[256]; float4 displays[5]; float white; float peak; float2 pad; uint4 covered[512]; };
Texture2D scene : register(t0);
struct Vertex { float4 pos : SV_Position; float2 local : TEXCOORD0; nointerpolation uint instance : TEXCOORD1; };
Vertex baseVS(uint i : SV_VertexID) {
    Vertex v; float2 uv = float2((i << 1) & 2, i & 2);
    v.pos = float4(uv * float2(2,-2) + float2(-1,1),0,1); v.local = 0; v.instance=0; return v;
}
Vertex quad(uint i, float4 r) {
    Vertex v; float2 corner = float2(i & 1, i >> 1) * 2 - 1;
    float2 uv = r.xy + corner * r.zw;
    v.pos = float4(uv * float2(2,-2) + float2(-1,1),0,1); v.local = corner; v.instance=0; return v;
}
Vertex lampVS(uint i : SV_VertexID, uint instance : SV_InstanceID) { Vertex v=quad(i,regions[instance]); v.instance=instance;return v; }
Vertex matrixVS(uint i : SV_VertexID, uint instance : SV_InstanceID) { return quad(i,displays[instance]); }
float3 linearAt(float2 pos) {
    float3 rgb = saturate(scene.Load(int3(int2(pos),0)).rgb);
    return lerp(rgb/12.92, pow((rgb+0.055)/1.055,2.4), step(0.04045,rgb));
}
float4 basePS(Vertex v) : SV_Target { return float4(linearAt(v.pos.xy)*white,1); }
float4 matrixPS(Vertex v) : SV_Target {
    float3 c=linearAt(v.pos.xy);
    // The two-tone violet/cyan diodes are the only emitters in these apertures.
    // Their fractional pixel coverage comes from the actual anti-aliased SDR glyph.
    float diode=smoothstep(0.006,0.095,c.b-min(c.r,c.g));
    return float4(c*lerp(white,peak,diode),1);
}
float visibleSite(int2 samplePos,uint instance) {
    uint2 q=uint2(clamp(samplePos,int2(0,0),int2(15,15)));
    uint word=q.y>>1;
    uint flags=covered[2*instance+(word>>2)][word&3];
    return 1.0-float((flags>>(q.x+16*(q.y&1)))&1);
}
float4 lampPS(Vertex v) : SV_Target {
    float3 c = linearAt(v.pos.xy); float value = max(c.r,max(c.g,c.b));
    float lens = saturate(1-dot(v.local,v.local));
    float2 pos=(v.local+1)*8-.5; int2 q=int2(floor(pos)); float2 f=frac(pos);
    float coverage=lerp(lerp(visibleSite(q,v.instance),visibleSite(q+int2(1,0),v.instance),f.x),
                        lerp(visibleSite(q+int2(0,1),v.instance),visibleSite(q+int2(1,1),v.instance),f.x),f.y);
    float emission = smoothstep(0.14,0.90,value)*lens*coverage;
    return float4(c*lerp(white,peak,emission),1);
})";

	struct Output
	{
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> immediate, commands;
		ComPtr<ID3D11DeviceContext1> context1;
		ComPtr<ID3DDeviceContextState> drawingState;
		ComPtr<IDCompositionDevice> composition;
		ComPtr<IDCompositionTarget> target;
		ComPtr<IDCompositionVisual> visual;
		ComPtr<IDXGISwapChain1> swap;
		ComPtr<ID3D11Texture2D> source;
		ComPtr<ID3D11ShaderResourceView> srv;
		ComPtr<ID3D11RenderTargetView> targetView;
		ComPtr<ID3D11VertexShader> baseVS, lampVS, matrixVS;
		ComPtr<ID3D11PixelShader> basePS, lampPS, matrixPS;
		ComPtr<ID3D11Buffer> constants;
		ComPtr<ID3D11BlendState> brightest;
		ComPtr<ID3D11RasterizerState> rasterState;
		UINT width{}, height{};
		float peakNits = 500;

		~Output()
		{
			if (target && composition) { target->SetRoot(nullptr); composition->Commit(); }
		}
		bool Shaders()
		{
			auto compile = [](const char* name, const char* model, ID3DBlob** blob)
			{
				return D3DCompile(shader, sizeof(shader)-1, nullptr, nullptr, nullptr, name, model,
					D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, blob, nullptr);
			};
			ComPtr<ID3DBlob> a,b,c,d,e,f;
			if (FAILED(compile("baseVS","vs_4_0",&a)) || FAILED(compile("lampVS","vs_4_0",&b)) ||
				FAILED(compile("basePS","ps_4_0",&c)) || FAILED(compile("lampPS","ps_4_0",&d)) ||
				FAILED(compile("matrixVS","vs_4_0",&e)) || FAILED(compile("matrixPS","ps_4_0",&f))) return false;
			D3D11_BUFFER_DESC cb{}; cb.ByteWidth = sizeof(FrameConstants); cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			D3D11_BLEND_DESC blend{};
			auto& rt = blend.RenderTarget[0];
			rt.BlendEnable = TRUE; rt.SrcBlend = rt.DestBlend = rt.SrcBlendAlpha = rt.DestBlendAlpha = D3D11_BLEND_ONE;
			rt.BlendOp = rt.BlendOpAlpha = D3D11_BLEND_OP_MAX; rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
			D3D11_RASTERIZER_DESC raster{}; raster.FillMode = D3D11_FILL_SOLID; raster.CullMode = D3D11_CULL_NONE;
			raster.DepthClipEnable = TRUE;
			return SUCCEEDED(device->CreateVertexShader(a->GetBufferPointer(),a->GetBufferSize(),nullptr,&baseVS))
				&& SUCCEEDED(device->CreateVertexShader(b->GetBufferPointer(),b->GetBufferSize(),nullptr,&lampVS))
				&& SUCCEEDED(device->CreatePixelShader(c->GetBufferPointer(),c->GetBufferSize(),nullptr,&basePS))
				&& SUCCEEDED(device->CreatePixelShader(d->GetBufferPointer(),d->GetBufferSize(),nullptr,&lampPS))
				&& SUCCEEDED(device->CreateVertexShader(e->GetBufferPointer(),e->GetBufferSize(),nullptr,&matrixVS))
				&& SUCCEEDED(device->CreatePixelShader(f->GetBufferPointer(),f->GetBufferSize(),nullptr,&matrixPS))
				&& SUCCEEDED(device->CreateBuffer(&cb,nullptr,&constants))
				&& SUCCEEDED(device->CreateRasterizerState(&raster,&rasterState))
				&& SUCCEEDED(device->CreateBlendState(&blend,&brightest));
		}
		bool Init(HWND hwnd, SDL_Renderer* renderer)
		{
			device.Attach(SDL_RenderGetD3D11Device(renderer)); // SDL returns an AddRef'd device
			if (!device) { status="SDL is not using Direct3D 11"; return false; }
			device->GetImmediateContext(&immediate);
			// SDL can create a single-threaded D3D device. Swap context state
			// around our pass so SDL retains all state cached by its backend.
			commands=immediate;
			ComPtr<IDXGIDevice> dxgi; ComPtr<ID3D11Device1> device1;
			auto level=device->GetFeatureLevel();
			if (!succeeded(device.As(&dxgi),"DXGI device") || !succeeded(device.As(&device1),"D3D11.1 device")
				|| !succeeded(immediate.As(&context1),"D3D11.1 context")
				|| !succeeded(device1->CreateDeviceContextState(0,&level,1,D3D11_SDK_VERSION,
					__uuidof(ID3D11Device),nullptr,&drawingState),"D3D context state")
				|| !succeeded(DCompositionCreateDevice(dxgi.Get(),IID_PPV_ARGS(&composition)),"DirectComposition device")
				|| !succeeded(composition->CreateTargetForHwnd(hwnd, TRUE, &target),"DirectComposition window target")
				|| !succeeded(composition->CreateVisual(&visual),"DirectComposition visual")) return false;
			if (!Shaders()) { status="HDR shader or pipeline state creation failed"; return false; }
			return true;
		}
		bool Resize(const D3D11_TEXTURE2D_DESC& src)
		{
			if (src.Format != DXGI_FORMAT_B8G8R8A8_UNORM && src.Format != DXGI_FORMAT_R8G8B8A8_UNORM) return false;
			targetView.Reset(); source.Reset(); srv.Reset();
			if (!swap)
			{
				ComPtr<IDXGIDevice> dxgi; ComPtr<IDXGIAdapter> adapter; ComPtr<IDXGIFactory2> factory;
				if (FAILED(device.As(&dxgi)) || FAILED(dxgi->GetAdapter(&adapter)) ||
					FAILED(adapter->GetParent(IID_PPV_ARGS(&factory)))) return false;
				DXGI_SWAP_CHAIN_DESC1 desc{};
				desc.Width = src.Width; desc.Height = src.Height; desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
				desc.SampleDesc.Count = 1; desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.BufferCount = 2;
				desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL; desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
				ComPtr<IDXGISwapChain3> chain3;
				if (FAILED(factory->CreateSwapChainForComposition(device.Get(),&desc,nullptr,&swap))
					|| FAILED(swap.As(&chain3))
					|| FAILED(chain3->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709))
					|| FAILED(visual->SetContent(swap.Get())) || FAILED(target->SetRoot(visual.Get()))
					|| FAILED(composition->Commit())) return false;
			}
			else if (FAILED(swap->ResizeBuffers(0,src.Width,src.Height,DXGI_FORMAT_UNKNOWN,0))) return false;
			ComPtr<ID3D11Texture2D> back;
			D3D11_TEXTURE2D_DESC copy{};
			copy.Width=src.Width; copy.Height=src.Height; copy.MipLevels=copy.ArraySize=copy.SampleDesc.Count=1;
			copy.Format=src.Format; copy.BindFlags=D3D11_BIND_SHADER_RESOURCE;
			width=src.Width; height=src.Height;
			return SUCCEEDED(swap->GetBuffer(0,IID_PPV_ARGS(&back)))
				&& SUCCEEDED(device->CreateRenderTargetView(back.Get(),nullptr,&targetView))
				&& SUCCEEDED(device->CreateTexture2D(&copy,nullptr,&source))
				&& SUCCEEDED(device->CreateShaderResourceView(source.Get(),nullptr,&srv));
		}
		bool Draw(SDL_Renderer* renderer, bool measurePeak, HdrOutput::LuminanceSite* sites, unsigned siteCount)
		{
			if (SDL_RenderFlush(renderer) != 0) return false;
			ComPtr<ID3D11RenderTargetView> scene;
			immediate->OMGetRenderTargets(1,&scene,nullptr);
			if (!scene) return false;
			ComPtr<ID3D11Resource> resource; ComPtr<ID3D11Texture2D> tex;
			scene->GetResource(&resource);
			if (FAILED(resource.As(&tex))) return false;
			D3D11_TEXTURE2D_DESC desc; tex->GetDesc(&desc);
			if ((width!=desc.Width || height!=desc.Height) && !Resize(desc)) return false;
			FrameConstants frame{};
			UINT count = static_cast<UINT>(std::min<size_t>(MaxRegions,render::EmissiveRegions.size()));
			// Keep game menus at diffuse white if they overlap a lit insert.
			const bool unobstructed=!ImGui::GetCurrentContext()->OpenPopupStack.Size && !winmain::CabinetMenuOpen();
			if (!unobstructed) count=0;
			SDL_Rect rects[MatrixCount]{};
			displayRects(rects);
			for (unsigned i=0;i<MatrixCount;++i)
			{
				auto r=rects[i];
				frame.displays[i]={(r.x+r.w*.5f)/width,(r.y+r.h*.5f)/height,r.w*.5f/width,r.h*.5f/height};
			}
			for (unsigned i=0;i<count;++i)
			{
				const auto& r=render::EmissiveRegions[i];
				frame.regions[i]={r.x,r.y,r.rx,r.ry};
				std::copy_n(r.covered,8,frame.covered[i]);
			}
			frame.white = 160.f / 80.f; frame.peak = peakNits / 80.f;
			ComPtr<ID3DDeviceContextState> sdlState;
			context1->SwapDeviceContextState(drawingState.Get(),&sdlState);
			commands->CopyResource(source.Get(),tex.Get());
			commands->UpdateSubresource(constants.Get(),0,nullptr,&frame,0,0);
			commands->VSSetConstantBuffers(0,1,constants.GetAddressOf());
			commands->PSSetConstantBuffers(0,1,constants.GetAddressOf());
			commands->PSSetShaderResources(0,1,srv.GetAddressOf());
			commands->OMSetRenderTargets(1,targetView.GetAddressOf(),nullptr);
			D3D11_VIEWPORT viewport{0,0,static_cast<float>(width),static_cast<float>(height),0,1};
			commands->RSSetViewports(1,&viewport); commands->RSSetState(rasterState.Get());
			commands->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			commands->VSSetShader(baseVS.Get(),nullptr,0); commands->PSSetShader(basePS.Get(),nullptr,0);
			commands->Draw(3,0);
			if (count)
			{
				commands->OMSetBlendState(brightest.Get(),nullptr,0xffffffff);
				commands->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
				commands->VSSetShader(lampVS.Get(),nullptr,0); commands->PSSetShader(lampPS.Get(),nullptr,0);
				commands->DrawInstanced(4,count,0,0);
			}
			if (unobstructed)
			{
				commands->OMSetBlendState(brightest.Get(),nullptr,0xffffffff);
				commands->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
				commands->VSSetShader(matrixVS.Get(),nullptr,0); commands->PSSetShader(matrixPS.Get(),nullptr,0);
				commands->DrawInstanced(4,MatrixCount,0,0);
			}
			commands->ClearState();
			context1->SwapDeviceContextState(sdlState.Get(),nullptr);
			if (measurePeak)
			{
				ComPtr<ID3D11Texture2D> back, staging;
				lastPeak = 0;
				for (auto& sample : matrixReadback) sample={0.f,0.f};
				if (SUCCEEDED(swap->GetBuffer(0,IID_PPV_ARGS(&back))))
				{
					D3D11_TEXTURE2D_DESC readDesc{}; back->GetDesc(&readDesc);
					readDesc.BindFlags=0; readDesc.Usage=D3D11_USAGE_STAGING;
					readDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ; readDesc.MiscFlags=0;
					if (SUCCEEDED(device->CreateTexture2D(&readDesc,nullptr,&staging)))
					{
						immediate->CopyResource(staging.Get(),back.Get());
						D3D11_MAPPED_SUBRESOURCE mapped{};
						if (SUCCEEDED(immediate->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)))
						{
							uint16_t maximum=0;
							for (unsigned i=0;i<siteCount;++i)
							{
								auto& site=sites[i];
								if (!(site.u>=0.f && site.u<1.f && site.v>=0.f && site.v<1.f)) continue;
								const UINT x=std::min(width-1,UINT(site.u*width));
								const UINT y=std::min(height-1,UINT(site.v*height));
								const auto row=reinterpret_cast<const uint16_t*>(static_cast<const char*>(mapped.pData)+y*mapped.RowPitch);
								const auto p=row+4*x;
								site.nits=80.f*(.2126f*halfValue(p[0])+.7152f*halfValue(p[1])+.0722f*halfValue(p[2]));
							}
							for (UINT y=0;y<height;++y)
							{
								auto row=reinterpret_cast<const uint16_t*>(static_cast<const char*>(mapped.pData)+y*mapped.RowPitch);
								for (UINT x=0;x<width;++x)
									maximum=std::max(maximum,std::max(row[4*x],std::max(row[4*x+1],row[4*x+2])));
							}
							for (unsigned i=0;i<MatrixCount;++i)
							{
								SDL_Rect bounds{0,0,int(width),int(height)}, clipped{};
								if (!SDL_IntersectRect(&bounds,&rects[i],&clipped)) continue;
								auto& m=matrixReadback[i];
								m.darkest=1e6f;
								double red=0,green=0,blue=0,luminance=0;
								for (int y=clipped.y;y<clipped.y+clipped.h;++y)
								{
									auto row=reinterpret_cast<const uint16_t*>(static_cast<const char*>(mapped.pData)+y*mapped.RowPitch);
									for (int x=clipped.x;x<clipped.x+clipped.w;++x)
									{
										auto p=row+x*4;
										const float r=halfValue(p[0]),g=halfValue(p[1]),b=halfValue(p[2]);
										float nits=80*(.2126f*r+.7152f*g+.0722f*b);
										m.brightest=std::max(m.brightest,nits); m.darkest=std::min(m.darkest,nits);
										if (b>.10f && b>1.08f*std::min(r,g))
										{
											red+=r; green+=g; blue+=b;
											luminance+=nits; ++m.litPixels;
										}
									}
								}
								if (m.litPixels)
								{
									m.litMean=float(luminance/m.litPixels);
									m.redOverBlue=float(red/blue); m.greenOverBlue=float(green/blue);
								}
							}
							immediate->Unmap(staging.Get(),0);
							lastPeak=halfValue(maximum);
						}
					}
				}
			}
			return SUCCEEDED(swap->Present(0,0));
		}
	};

	std::unique_ptr<Output> output;
	Uint64 nextProbe = 0;
	HMONITOR lastMonitor = nullptr;
	bool MonitorPeak(HMONITOR monitor, float& peak)
	{
		ComPtr<IDXGIFactory1> factory;
		if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return false;
		for (UINT i=0;;i++)
		{
			ComPtr<IDXGIAdapter1> adapter;
			if (factory->EnumAdapters1(i,&adapter)!=S_OK) break;
			for (UINT j=0;;j++)
			{
				ComPtr<IDXGIOutput> found; ComPtr<IDXGIOutput6> modern; DXGI_OUTPUT_DESC1 desc{};
				if (adapter->EnumOutputs(j,&found)!=S_OK) break;
				if (SUCCEEDED(found.As(&modern)) && SUCCEEDED(modern->GetDesc1(&desc)) && desc.Monitor==monitor)
				{
					peak=std::max(160.f,std::min(1000.f,desc.MaxLuminance>0 ? desc.MaxLuminance : 500.f));
					return desc.ColorSpace==DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
				}
			}
		}
		return false;
	}
}
bool HdrOutput::Present(SDL_Window* window, SDL_Renderer* renderer, bool enabled, bool measurePeak, bool testFloatBufferOnSDR,
                        LuminanceSite* sites, unsigned siteCount)
{
	if (!enabled) { if (output) Close(); return false; }
	SDL_SysWMinfo info{}; SDL_VERSION(&info.version);
	if (!SDL_GetWindowWMInfo(window,&info) || info.subsystem!=SDL_SYSWM_WINDOWS) { status="No native Windows display"; return false; }
	const auto monitor=MonitorFromWindow(info.info.win.window,MONITOR_DEFAULTTONEAREST);
	const auto now=SDL_GetTicks64();
	if (now>=nextProbe || monitor!=lastMonitor || testFloatBufferOnSDR)
	{
		nextProbe=now+3000; lastMonitor=monitor;
		float peak{};
		if (!MonitorPeak(monitor,peak))
		{
			status="Windows did not report an active HDR output for this window";
			if (!testFloatBufferOnSDR) { output.reset(); return false; }
			peak=600.f;
		}
		if (!output)
		{
			auto candidate=std::unique_ptr<Output>(new Output);
			if (!candidate->Init(info.info.win.window,renderer)) return false;
			output=std::move(candidate);
		}
		output->peakNits=peak;
	}
	if (!output) return false;
	if (output->Draw(renderer,measurePeak,sites,sites ? siteCount : 0))
	{
		status=testFloatBufferOnSDR ? "FP16 diagnostic frame" : "Active Windows HDR output";
		return true;
	}
	status="Could not present the scRGB frame";
	output.reset();
	return false;
}
bool HdrOutput::Active() { return !!output; }
float HdrOutput::LastPeak() { return lastPeak; }
HdrOutput::MatrixMeasurement HdrOutput::LastMatrix(unsigned index)
{ return index<MatrixCount ? matrixReadback[index] : MatrixMeasurement{}; }
const char* HdrOutput::Status() { return status; }
void HdrOutput::Close() { output.reset(); nextProbe=0; lastMonitor=nullptr; }
#else
bool HdrOutput::Present(SDL_Window*, SDL_Renderer*, bool, bool, bool, LuminanceSite*, unsigned) { return false; }
float HdrOutput::LastPeak() { return 0; }
HdrOutput::MatrixMeasurement HdrOutput::LastMatrix(unsigned) { return {}; }
const char* HdrOutput::Status() { return "HDR presenter unavailable on this platform"; }
bool HdrOutput::Active() { return false; }
void HdrOutput::Close() {}
#endif
