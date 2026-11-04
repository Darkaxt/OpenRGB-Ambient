#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <roapi.h>
#include <dwmapi.h>
#include <commctrl.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include "OfficeAmbience.h"
#include "ColorPostProcessor.h"
#include <OpenRGBPluginInterface.h>
#include <QTimer>
#include <QSaveFile>
#include <QFile>
#include <QDateTime>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <map>
#include <mutex>
#include <numeric>
#include <set>
#include <cstring>

namespace {
using namespace winrt;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;
using Clock=std::chrono::steady_clock;
using Json=nlohmann::json;
constexpr unsigned maxSamples=384;
struct Apartment {
    HRESULT result=RoInitialize(RO_INIT_MULTITHREADED);
    Apartment() { if(FAILED(result) && result!=RPC_E_CHANGED_MODE) check_hresult(result); }
    ~Apartment() { if(SUCCEEDED(result)) RoUninitialize(); }
};
struct DpiContext {
    DPI_AWARENESS_CONTEXT previous=SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    DpiContext() { check_bool(previous!=nullptr); }
    ~DpiContext() { SetThreadDpiAwarenessContext(previous); }
};
struct Sample { float left,top,right,bottom; };
struct Monitor { HMONITOR handle{}; RECT bounds{}; };

std::wstring windowClass(HWND window) {
    wchar_t value[256]{}; GetClassNameW(window,value,256); return value;
}
std::wstring ownerName(HWND window) {
    DWORD pid{}; GetWindowThreadProcessId(window,&pid);
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
    if(!process) return {};
    wchar_t path[32768]{}; DWORD length=32768;
    bool ok=QueryFullProcessImageNameW(process,0,path,&length); CloseHandle(process);
    if(!ok) return {};
    std::wstring name(path,length); return name.substr(name.find_last_of(L"\\/")+1);
}
bool isRenderer(HWND window) {
    auto name=ownerName(window);
    return name==L"wallpaper64.exe" || name==L"wallpaper32.exe" ||
        name==L"webwallpaper32.exe" || name==L"webwallpaper64.exe" || name==L"edgewallpaper64.exe";
}
Monitor monitorByName(const std::wstring& name) {
    struct Search { const std::wstring& name; Monitor result; } search{name,{}};
    EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR handle,HDC,LPRECT,LPARAM data)->BOOL {
        auto& s=*reinterpret_cast<Search*>(data);
        MONITORINFOEXW info{}; info.cbSize=sizeof(info);
        if(GetMonitorInfoW(handle,&info) && s.name==info.szDevice) s.result={handle,info.rcMonitor};
        return TRUE;
    },reinterpret_cast<LPARAM>(&search));
    return search.result;
}
bool overlaps(RECT a,RECT b) { RECT intersection{}; return IntersectRect(&intersection,&a,&b); }
HWND wallpaperHost(const Monitor& monitor) {
    struct Search { const Monitor& monitor; HWND host{}; } search{monitor};
    EnumWindows([](HWND root,LPARAM data)->BOOL {
        auto& s=*reinterpret_cast<Search*>(data);
        auto cls=windowClass(root);
        if((cls!=L"Progman" && cls!=L"WorkerW") || !IsWindowVisible(root) || ownerName(root)!=L"explorer.exe") return TRUE;
        struct Children { const Monitor& monitor; bool renderer=false,icons=false; } children{s.monitor};
        EnumChildWindows(root,[](HWND child,LPARAM data)->BOOL {
            auto& c=*reinterpret_cast<Children*>(data);
            // An empty desktop list does not add pixels to the capture. Refuse
            // an actual icon list rather than disabling capture on this setup.
            if(windowClass(child)==L"SysListView32" && IsWindowVisible(child) &&
               SendMessageW(child,LVM_GETITEMCOUNT,0,0)>0) c.icons=true;
            RECT bounds{};
            if(IsWindowVisible(child) && GetWindowRect(child,&bounds) && overlaps(bounds,c.monitor.bounds) && isRenderer(child)) c.renderer=true;
            return TRUE;
        },reinterpret_cast<LPARAM>(&children));
        if(children.renderer && !children.icons) { s.host=root; return FALSE; }
        return TRUE;
    },reinterpret_cast<LPARAM>(&search));
    return search.host;
}
HWND maximizedApp(const Monitor& monitor) {
    struct Search { const Monitor& monitor; HWND app{}; } search{monitor};
    EnumWindows([](HWND window,LPARAM data)->BOOL {
        auto& s=*reinterpret_cast<Search*>(data);
        if(!IsWindowVisible(window) || IsIconic(window) || MonitorFromWindow(window,MONITOR_DEFAULTTONULL)!=s.monitor.handle) return TRUE;
        auto cls=windowClass(window);
        if(cls==L"Progman" || cls==L"WorkerW" || cls==L"Shell_TrayWnd" || cls==L"Shell_SecondaryTrayWnd") return TRUE;
        if(GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_TOOLWINDOW) return TRUE;
        DWORD cloaked{}; DwmGetWindowAttribute(window,DWMWA_CLOAKED,&cloaked,sizeof(cloaked));
        if(cloaked || isRenderer(window)) return TRUE;
        RECT r{};
        if(FAILED(DwmGetWindowAttribute(window,DWMWA_EXTENDED_FRAME_BOUNDS,&r,sizeof(r)))) GetWindowRect(window,&r);
        const auto m=s.monitor.bounds;
        bool fullscreen=r.left<=m.left+2 && r.top<=m.top+2 && r.right>=m.right-2 && r.bottom>=m.bottom-2;
        if(IsZoomed(window) || fullscreen) { s.app=window; return FALSE; }
        return TRUE;
    },reinterpret_cast<LPARAM>(&search));
    return search.app;
}

struct Device {
    RGBControllerInterface* controller;
    size_t start,count;
    std::vector<RGBColor> colors;
};
Sample around(float x,float y,float width,float height) {
    return {std::clamp(x-width/2,0.f,1.f),std::clamp(y-height/2,0.f,1.f),
            std::clamp(x+width/2,0.f,1.f),std::clamp(y+height/2,0.f,1.f)};
}
std::vector<Sample> deviceSamples(RGBControllerInterface* controller) {
    auto name=controller->GetName();
    const auto n=controller->GetLEDCount();
    std::vector<Sample> points(n);
    auto line=[&](unsigned from,unsigned count,float x,float y,float dx,float dy,float w,float h) {
        for(unsigned i=0;i<count;++i) {
            float t=(i+.5f)/count; points.at(from+i)=around(x+t*dx,y+t*dy,w,h);
        }
    };
    if(name.find("H6609")!=std::string::npos && n==20) {
        line(0,3,.04f,1,0,-1,.08f,1.f/3);
        line(3,7,0,.04f,1,0,1.f/7,.08f);
        line(10,4,.96f,0,0,1,.08f,.25f);
        line(14,6,1,.96f,-1,0,1.f/6,.08f);
    } else if(name.find("H8048")!=std::string::npos && n==20) {
        line(0,10,.04f,1,0,-1,.08f,.1f);
        line(10,10,.96f,1,0,-1,.08f,.1f);
    } else if(controller->GetDescription().find("NL72S2")!=std::string::npos && n==48) {
        line(0,48,0,.96f,1,0,1.f/48,.08f);
    } else {
        bool laptop=name.find("Lenovo Legion 7 Gen 8")!=std::string::npos && n==109;
        bool keyboard=name.find("Q6 HE ISO")!=std::string::npos && n==109;
        bool mouse=(name.find("G502 X PLUS")!=std::string::npos || name.find("G502 X Plus")!=std::string::npos) && n==8;
        if(!laptop && !keyboard && !mouse) throw std::runtime_error("Unexpected office device or LED count: "+name);
        float left=laptop?0.f:mouse?.80f:.25f;
        float top=laptop?.55f:mouse?.75f:.70f;
        float width=laptop?.25f:mouse?.20f:.50f;
        float height=1-top;
        line(0,n,left,top+height/2,width,0,width/n,height);
        for(unsigned zone=0;zone<controller->GetZoneCount();++zone) {
            auto columns=controller->GetZoneMatrixMapWidth(zone),rows=controller->GetZoneMatrixMapHeight(zone);
            const auto* map=controller->GetZoneMatrixMapData(zone);
            if(!map || !columns || !rows) continue;
            std::vector<float> sx(n),sy(n),counts(n);
            for(unsigned y=0;y<rows;++y) for(unsigned x=0;x<columns;++x) {
                unsigned index=map[y*columns+x];
                if(index>=n) continue;
                sx[index]+=(x+.5f)/columns; sy[index]+=(y+.5f)/rows; counts[index]+=1;
            }
            for(unsigned index=0;index<n;++index) if(counts[index])
                points[index]=around(left+width*sx[index]/counts[index],top+height*sy[index]/counts[index],width/columns,height/rows);
        }
        if(laptop) { line(102,6,left,.98f,width,0,width/6,.04f); points[108]=around(left+width/2,.98f,.04f,.04f); }
    }
    return points;
}

// Input textures stay on the GPU. One output pixel represents one physical LED;
// the only CPU readback is this small row of final region-average RGB values.
constexpr const char* shader=R"(
Texture2D image : register(t0);
SamplerState linearSampler : register(s0);
cbuffer Regions : register(b0) { float4 region[384]; };
struct Vertex { float4 position : SV_POSITION; };
Vertex vs(uint index : SV_VertexID) {
    Vertex o; float2 uv=float2((index<<1)&2,index&2);
    o.position=float4(uv*float2(2,-2)+float2(-1,1),0,1); return o;
}
float4 ps(Vertex input) : SV_TARGET {
    float4 r=region[(uint)input.position.x]; float3 color=0;
    [unroll] for(uint y=0;y<4;++y) [unroll] for(uint x=0;x<4;++x) {
        float2 uv=lerp(r.xy,r.zw,(float2(x,y)+.5)/4);
        uint width,height; image.GetDimensions(width,height);
        float lod=max(0,log2(max((r.z-r.x)*width,(r.w-r.y)*height)/4));
        color+=image.SampleLevel(linearSampler,uv,lod).rgb;
    }
    return float4(color/16,1);
})";

struct CaptureState {
    std::mutex mutex;
    bool active=true,closed=false;
    std::string error;
    std::vector<RGBColor> colors;
    uint64_t frames=0;
    double lastProcessingMs=0;
    Clock::time_point received{};
    Clock::time_point firstReceived{};
    com_ptr<ID3D11Device> device;
    com_ptr<ID3D11DeviceContext> context;
    com_ptr<ID3D11Texture2D> input,output,readback;
    com_ptr<ID3D11ShaderResourceView> inputView;
    com_ptr<ID3D11RenderTargetView> target;
    com_ptr<ID3D11VertexShader> vertex;
    com_ptr<ID3D11PixelShader> pixel;
    com_ptr<ID3D11Buffer> regions;
    com_ptr<ID3D11SamplerState> sampler;
    unsigned sourceW=0,sourceH=0;
    RECT host{},monitor{};

    CaptureState(const std::vector<Sample>& samples,RECT windowBounds,RECT monitorBounds,D3D_DRIVER_TYPE driver=D3D_DRIVER_TYPE_HARDWARE):colors(samples.size()),host(windowBounds),monitor(monitorBounds) {
        check_hresult(D3D11CreateDevice(nullptr,driver,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            nullptr,0,D3D11_SDK_VERSION,device.put(),nullptr,context.put()));
        com_ptr<ID3DBlob> vs,ps,errors;
        check_hresult(D3DCompile(shader,strlen(shader),nullptr,nullptr,nullptr,"vs","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,vs.put(),errors.put()));
        errors=nullptr;
        check_hresult(D3DCompile(shader,strlen(shader),nullptr,nullptr,nullptr,"ps","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,ps.put(),errors.put()));
        check_hresult(device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,vertex.put()));
        check_hresult(device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,pixel.put()));
        std::array<Sample,maxSamples> data{}; std::copy(samples.begin(),samples.end(),data.begin());
        D3D11_BUFFER_DESC buffer{}; buffer.ByteWidth=sizeof(data); buffer.Usage=D3D11_USAGE_IMMUTABLE; buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA initial{}; initial.pSysMem=data.data();
        check_hresult(device->CreateBuffer(&buffer,&initial,regions.put()));
        D3D11_SAMPLER_DESC sd{}; sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP; sd.MaxLOD=D3D11_FLOAT32_MAX;
        check_hresult(device->CreateSamplerState(&sd,sampler.put()));
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width=static_cast<UINT>(samples.size()); desc.Height=1; desc.MipLevels=1; desc.ArraySize=1;
        desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count=1;
        desc.Usage=D3D11_USAGE_DEFAULT; desc.BindFlags=D3D11_BIND_RENDER_TARGET;
        check_hresult(device->CreateTexture2D(&desc,nullptr,output.put()));
        check_hresult(device->CreateRenderTargetView(output.get(),nullptr,target.put()));
        desc.Usage=D3D11_USAGE_STAGING; desc.BindFlags=0; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        check_hresult(device->CreateTexture2D(&desc,nullptr,readback.put()));
    }
    void process(const Direct3D11CaptureFrame& frame) {
        auto access=frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        com_ptr<ID3D11Texture2D> source;
        check_hresult(access->GetInterface(__uuidof(ID3D11Texture2D),source.put_void()));
        processTexture(source.get());
    }
    void processTexture(ID3D11Texture2D* source) {
        auto began=Clock::now();
        D3D11_TEXTURE2D_DESC desc{}; source->GetDesc(&desc);
        if(!input || sourceW!=desc.Width || sourceH!=desc.Height) {
            sourceW=desc.Width; sourceH=desc.Height;
            auto sx=[&](LONG v) { return static_cast<UINT>(std::clamp((double)(v-host.left)/(host.right-host.left)*sourceW,0.0,(double)sourceW)); };
            auto sy=[&](LONG v) { return static_cast<UINT>(std::clamp((double)(v-host.top)/(host.bottom-host.top)*sourceH,0.0,(double)sourceH)); };
            unsigned left=sx(monitor.left),top=sy(monitor.top),right=sx(monitor.right),bottom=sy(monitor.bottom);
            if(right<=left || bottom<=top) throw std::runtime_error("Selected monitor outside WGC window");
            desc.Width=right-left; desc.Height=bottom-top; desc.MipLevels=0;
            desc.ArraySize=1; desc.SampleDesc.Count=1; desc.Usage=D3D11_USAGE_DEFAULT;
            desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
            desc.CPUAccessFlags=0; desc.MiscFlags=D3D11_RESOURCE_MISC_GENERATE_MIPS;
            input=nullptr; inputView=nullptr;
            check_hresult(device->CreateTexture2D(&desc,nullptr,input.put()));
            check_hresult(device->CreateShaderResourceView(input.get(),nullptr,inputView.put()));
        }
        auto sx=[&](LONG v) { return static_cast<UINT>(std::clamp((double)(v-host.left)/(host.right-host.left)*sourceW,0.0,(double)sourceW)); };
        auto sy=[&](LONG v) { return static_cast<UINT>(std::clamp((double)(v-host.top)/(host.bottom-host.top)*sourceH,0.0,(double)sourceH)); };
        D3D11_BOX crop{sx(monitor.left),sy(monitor.top),0,sx(monitor.right),sy(monitor.bottom),1};
        context->CopySubresourceRegion(input.get(),0,0,0,0,source,0,&crop);
        context->GenerateMips(inputView.get());
        D3D11_VIEWPORT viewport{0,0,static_cast<float>(colors.size()),1,0,1};
        context->RSSetViewports(1,&viewport);
        auto* rt=target.get(); context->OMSetRenderTargets(1,&rt,nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vertex.get(),nullptr,0); context->PSSetShader(pixel.get(),nullptr,0);
        auto* constants=regions.get(); context->PSSetConstantBuffers(0,1,&constants);
        auto* view=inputView.get(); context->PSSetShaderResources(0,1,&view);
        auto* filter=sampler.get(); context->PSSetSamplers(0,1,&filter);
        context->Draw(3,0);
        ID3D11ShaderResourceView* none=nullptr; context->PSSetShaderResources(0,1,&none);
        context->CopyResource(readback.get(),output.get());
        D3D11_MAPPED_SUBRESOURCE mapped{}; check_hresult(context->Map(readback.get(),0,D3D11_MAP_READ,0,&mapped));
        auto* bytes=static_cast<const unsigned char*>(mapped.pData);
        for(size_t i=0;i<colors.size();++i) colors[i]=ToRGBColor(bytes[i*4],bytes[i*4+1],bytes[i*4+2]);
        context->Unmap(readback.get(),0);
        received=Clock::now(); ++frames;
        if(frames==1) firstReceived=received;
        lastProcessingMs=std::chrono::duration<double,std::milli>(received-began).count();
    }
};

class Capture {
    GraphicsCaptureItem item{nullptr};
    Direct3D11CaptureFramePool pool{nullptr};
    GraphicsCaptureSession session{nullptr};
    event_token arrived{},closed{};
public:
    std::shared_ptr<CaptureState> state;
    Capture(HWND window,RECT monitor,const std::vector<Sample>& samples) {
        RECT bounds{}; check_bool(GetWindowRect(window,&bounds));
        state=std::make_shared<CaptureState>(samples,bounds,monitor);
        auto factory=get_activation_factory<GraphicsCaptureItem,IGraphicsCaptureItemInterop>();
        check_hresult(factory->CreateForWindow(window,guid_of<GraphicsCaptureItem>(),put_abi(item)));
        auto dxgi=state->device.as<IDXGIDevice>(); com_ptr<IInspectable> wrapper;
        check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.get(),wrapper.put()));
        auto winDevice=wrapper.as<IDirect3DDevice>();
        pool=Direct3D11CaptureFramePool::CreateFreeThreaded(winDevice,DirectXPixelFormat::B8G8R8A8UIntNormalized,2,item.Size());
        session=pool.CreateCaptureSession(item); session.IsCursorCaptureEnabled(false);
        try { session.IsBorderRequired(false); } catch(hresult_error const&) {}
        auto owned=state;
        closed=item.Closed([owned](auto const&,auto const&) { std::lock_guard lock(owned->mutex); owned->closed=true; });
        auto size=item.Size();
        arrived=pool.FrameArrived([owned,winDevice,size](auto const& sender,auto const&) mutable {
            std::lock_guard lock(owned->mutex);
            if(!owned->active) return;
            auto frame=sender.TryGetNextFrame(); if(!frame) return;
            auto content=frame.ContentSize();
            try {
                if(content.Width==size.Width && content.Height==size.Height) owned->process(frame);
            }
            catch(hresult_error const& error) { owned->error=to_string(error.message()); }
            catch(std::exception const& error) { owned->error=error.what(); }
            frame.Close();
            if(content.Width>0 && content.Height>0 && (content.Width!=size.Width || content.Height!=size.Height)) {
                size=content;
                sender.Recreate(winDevice,DirectXPixelFormat::B8G8R8A8UIntNormalized,2,size);
            }
        });
        session.StartCapture();
    }
    ~Capture() {
        { std::lock_guard lock(state->mutex); state->active=false; }
        if(pool) pool.FrameArrived(arrived);
        if(item) item.Closed(closed);
        if(session) session.Close();
        if(pool) pool.Close();
    }
};
} // namespace

struct OfficeAmbience::Impl {
    QTimer output;
    OpenRGBPluginAPIInterface* api;
    std::filesystem::path configPath,statusPath;
    std::wstring display;
    std::vector<Device> devices;
    std::vector<Sample> samples;
    std::vector<RGBColor> latest;
    std::unique_ptr<Capture> capture;
    HWND source{};
    RECT sourceBounds{},monitorBounds{};
    std::string mode="waiting",error;
    Clock::time_point lastOutput=Clock::now(),switched=lastOutput,lastStatus=lastOutput;
    Clock::time_point received{};
    uint64_t outputFrames=0,inputFrames=0;
    double smoothingMs=140,processingMs=0,firstFrameDelayMs=0;
    double callbackToSubmitMs=0,submitMs=0;
    bool initialized=false;
    std::atomic_bool dirty{true};
    HWINEVENTHOOK systemHook{},objectHook{};
    static inline std::map<HWINEVENTHOOK,Impl*> hooks;
    Apartment apartment;
    DpiContext dpi;

    static void CALLBACK winEvent(HWINEVENTHOOK hook,DWORD,HWND,LONG object,LONG,DWORD,DWORD) {
        if(object!=OBJID_WINDOW) return;
        auto found=hooks.find(hook); if(found!=hooks.end()) found->second->dirty=true;
    }
    Impl(OpenRGBPluginAPIInterface* api,const std::filesystem::path& path):api(api),configPath(path) {
        std::ifstream file(path); Json config; file>>config;
        auto wide=to_hstring(config.value("monitor",std::string("\\\\.\\DISPLAY2")));
        display.assign(wide.c_str(),wide.size());
        int fps=config.value("fps",15);
        if(fps<1 || fps>30) throw std::runtime_error("Office FPS must be in 1..30");
        smoothingMs=config.value("smoothing_ms",140.0);
        statusPath=path.parent_path()/"office-ambience-status.json";
        std::set<std::string> names;
        for(auto* controller:api->GetRGBControllers()) {
            auto points=deviceSamples(controller);
            if(!names.insert(controller->GetName()).second) throw std::runtime_error("Duplicate office controller");
            devices.push_back({controller,samples.size(),points.size(),std::vector<RGBColor>(points.size())});
            samples.insert(samples.end(),points.begin(),points.end());
        }
        if(devices.size()!=6 || samples.size()!=314) throw std::runtime_error("Office ambience requires the verified six-device 314-LED inventory");
        for(auto& device:devices) device.controller->SetCustomMode();
        systemHook=SetWinEventHook(EVENT_SYSTEM_FOREGROUND,EVENT_SYSTEM_MINIMIZEEND,nullptr,winEvent,0,0,WINEVENT_OUTOFCONTEXT);
        objectHook=SetWinEventHook(EVENT_OBJECT_CREATE,EVENT_OBJECT_LOCATIONCHANGE,nullptr,winEvent,0,0,WINEVENT_OUTOFCONTEXT);
        if(!systemHook || !objectHook) {
            if(systemHook) UnhookWinEvent(systemHook);
            if(objectHook) UnhookWinEvent(objectHook);
            throw std::runtime_error("Window-state event registration failed");
        }
        hooks[systemHook]=this; hooks[objectHook]=this;
        QObject::connect(&output,&QTimer::timeout,[this] { tick(); });
        output.setTimerType(Qt::PreciseTimer); output.start(std::max(1,static_cast<int>(std::round(1000.0/fps))));
        status();
    }
    ~Impl() {
        output.stop();
        for(auto hook:{systemHook,objectHook}) if(hook) { hooks.erase(hook); UnhookWinEvent(hook); }
        capture.reset();
        mode="stopped"; status();
    }
    void chooseSource() {
        auto monitor=monitorByName(display);
        HWND candidate=monitor.handle?maximizedApp(monitor):nullptr;
        std::string selected=candidate?"app":"wallpaper";
        if(!candidate && monitor.handle) candidate=wallpaperHost(monitor);
        RECT bounds{}; if(candidate) GetWindowRect(candidate,&bounds);
        if(candidate==source && capture && EqualRect(&bounds,&sourceBounds) && EqualRect(&monitor.bounds,&monitorBounds)) return;
        capture.reset(); source=candidate; mode=candidate?selected:"waiting";
        sourceBounds=bounds; monitorBounds=monitor.bounds;
        switched=Clock::now(); firstFrameDelayMs=0;
        error.clear();
        if(candidate) try { capture=std::make_unique<Capture>(candidate,monitor.bounds,samples); }
        catch(hresult_error const& failure) { error=to_string(failure.message()); }
        catch(std::exception const& failure) { error=failure.what(); }
        status();
    }
    void tick() {
        if(dirty.exchange(false)) chooseSource();
        auto now=Clock::now();
        if(capture) {
            auto state=capture->state;
            bool closed=false;
            {
                std::lock_guard lock(state->mutex);
                closed=state->closed;
                if(!closed && state->frames) {
                    latest=state->colors; inputFrames=state->frames; processingMs=state->lastProcessingMs;
                    received=state->received;
                    if(firstFrameDelayMs==0) firstFrameDelayMs=std::chrono::duration<double,std::milli>(state->firstReceived-switched).count();
                }
                if(!state->error.empty()) error=state->error;
            }
            if(closed) { capture.reset(); source=nullptr; dirty=true; }
        }
        if(!latest.empty()) {
            auto submitBegan=Clock::now();
            float weight=smoothingMs<=0?1.f:static_cast<float>(1-std::exp(-std::chrono::duration<double,std::milli>(now-lastOutput).count()/smoothingMs));
            SmoothingColorPostProcessor smooth{weight};
            for(auto& device:devices) {
                for(size_t index=0;index<device.count;++index) {
                    auto target=latest[device.start+index];
                    auto color=initialized?smooth.process(RGBGetRValue(target),RGBGetGValue(target),RGBGetBValue(target),device.colors[index]):target;
                    device.colors[index]=color;
                    device.controller->SetColor(static_cast<unsigned>(index),color);
                }
                device.controller->UpdateLEDs();
            }
            initialized=true; ++outputFrames;
            auto submitted=Clock::now();
            submitMs=std::chrono::duration<double,std::milli>(submitted-submitBegan).count();
            callbackToSubmitMs=std::chrono::duration<double,std::milli>(submitted-received).count();
        }
        lastOutput=now;
        // Diagnostic reporting cadence never classifies a silent/static frame as
        // failure and never cancels or restarts a valid capture session.
        if(now-lastStatus>=std::chrono::seconds(1)) status();
    }
    void status() {
        Json report={{"mode",mode},{"source_hwnd",reinterpret_cast<uintptr_t>(source)},
            {"devices",devices.size()},{"leds",samples.size()},{"output_frames",outputFrames},
            {"input_frames",inputFrames},{"gpu_processing_ms",processingMs},
            {"callback_to_native_submit_ms",callbackToSubmitMs},{"native_submit_ms",submitMs},
            {"first_frame_after_switch_ms",firstFrameDelayMs},{"has_valid_frame",initialized},
            {"error",error},{"updated_utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toStdString()}};
        QSaveFile file(QString::fromStdWString(statusPath.wstring()));
        if(file.open(QIODevice::WriteOnly)) { auto data=report.dump(2); file.write(data.data(),data.size()); file.commit(); }
        lastStatus=Clock::now();
    }
};

OfficeAmbience::OfficeAmbience(OpenRGBPluginAPIInterface* api,const std::filesystem::path& config):impl(std::make_unique<Impl>(api,config)) {}
OfficeAmbience::~OfficeAmbience()=default;
void OfficeAmbience::stop() { impl.reset(); }
