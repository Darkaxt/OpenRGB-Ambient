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
#include "OfficeControlServer.h"
#include "OfficeProcessing.h"
#include "OfficeSamplingShader.h"
#include <OpenRGBPluginInterface.h>
#include <ResourceManagerCallback.h>
#include <QTimer>
#include <QSaveFile>
#include <QFile>
#include <QDateTime>
#include <QImage>
#include <QBuffer>
#include <QCoreApplication>
#include <wtsapi32.h>
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
using Sample=office::Region;
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
    std::string key;
    std::string name;
    Json ledNames;
};
std::string deviceKey(RGBControllerInterface* controller) {
    auto name=controller->GetName();
    return name.find("H6609")!=std::string::npos?"H6609":name.find("H8048")!=std::string::npos?"H8048":
        name.find("Q6 HE ISO")!=std::string::npos?"Keychron":name.find("Lenovo Legion")!=std::string::npos?"Legion":
        controller->GetDescription().find("NL72S2")!=std::string::npos?"Secretlab":
        (name.find("G502 X PLUS")!=std::string::npos || name.find("G502 X Plus")!=std::string::npos)?"Logitech":"";
}
unsigned expectedCount(const std::string& key) {
    static const std::map<std::string,unsigned> counts={{"H6609",20},{"H8048",20},{"Keychron",109},{"Legion",109},{"Secretlab",48},{"Logitech",8}};
    auto found=counts.find(key);if(found==counts.end()) throw std::runtime_error("Unknown office controller identity");return found->second;
}
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

struct CaptureState {
    std::mutex mutex;
    bool active=true,closed=false;
    std::string error;
    std::vector<RGBColor> colors;
    std::vector<Sample> effectiveRegions;
    uint64_t frames=0;
    double lastProcessingMs=0;
    Clock::time_point received{};
    Clock::time_point firstReceived{};
    com_ptr<ID3D11Device> device;
    com_ptr<ID3D11DeviceContext> context;
    com_ptr<ID3D11Texture2D> input,output,readback;
    com_ptr<ID3D11ShaderResourceView> inputView;
    com_ptr<ID3D11RenderTargetView> target;
    com_ptr<ID3D11Texture2D> boundsOutput,boundsReadback;
    com_ptr<ID3D11RenderTargetView> boundsTarget;
    com_ptr<ID3D11VertexShader> vertex;
    com_ptr<ID3D11PixelShader> pixel;
    com_ptr<ID3D11PixelShader> previewPixel;
    com_ptr<ID3D11Buffer> options;
    com_ptr<ID3D11Buffer> regions;
    com_ptr<ID3D11SamplerState> sampler;
    unsigned sourceW=0,sourceH=0;
    RECT host{},monitor{};
    bool wantPreview=false;
    QByteArray previewPng;
    std::string previewError;
    com_ptr<ID3D11Texture2D> preview;
    com_ptr<ID3D11Texture2D> previewTarget;
    com_ptr<ID3D11RenderTargetView> previewView;

    com_ptr<ID3D11Buffer> makeOptions(const Json& config) {
        auto values=office::shaderOptions(config);
        D3D11_BUFFER_DESC desc{};desc.ByteWidth=sizeof(values);desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA data{};data.pSysMem=values.data();
        com_ptr<ID3D11Buffer> next;check_hresult(device->CreateBuffer(&desc,&data,next.put()));return next;
    }
    void configure(const Json& config) {options=makeOptions(config);}

    com_ptr<ID3D11Buffer> makeRegions(const std::vector<Sample>& samples) {
        std::array<Sample,maxSamples> data{};std::copy(samples.begin(),samples.end(),data.begin());
        D3D11_BUFFER_DESC desc{};desc.ByteWidth=sizeof(data);desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA initial{};initial.pSysMem=data.data();
        com_ptr<ID3D11Buffer> result;check_hresult(device->CreateBuffer(&desc,&initial,result.put()));return result;
    }

    CaptureState(const std::vector<Sample>& samples,RECT windowBounds,RECT monitorBounds,D3D_DRIVER_TYPE driver=D3D_DRIVER_TYPE_HARDWARE):colors(samples.size()),host(windowBounds),monitor(monitorBounds) {
        check_hresult(D3D11CreateDevice(nullptr,driver,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            nullptr,0,D3D11_SDK_VERSION,device.put(),nullptr,context.put()));
        com_ptr<ID3DBlob> vs,ps,errors;
        auto compile=[&](const char* entry,const char* profile,com_ptr<ID3DBlob>& result) {
            errors=nullptr;auto hr=D3DCompile(shader,strlen(shader),nullptr,nullptr,nullptr,entry,profile,D3DCOMPILE_OPTIMIZATION_LEVEL3,0,result.put(),errors.put());
            if(FAILED(hr)) throw std::runtime_error(errors?std::string(static_cast<const char*>(errors->GetBufferPointer()),errors->GetBufferSize()):"GPU shader compilation failed");
        };
        compile("vs","vs_5_0",vs);
        errors=nullptr;
        compile("ps","ps_5_0",ps);
        check_hresult(device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,vertex.put()));
        check_hresult(device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,pixel.put()));
        ps=nullptr;errors=nullptr;
        compile("previewPs","ps_5_0",ps);
        check_hresult(device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,previewPixel.put()));
        configure(office::processingDefaults());
        regions=makeRegions(samples);
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
        desc.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_RENDER_TARGET;desc.CPUAccessFlags=0;
        check_hresult(device->CreateTexture2D(&desc,nullptr,boundsOutput.put()));
        check_hresult(device->CreateRenderTargetView(boundsOutput.get(),nullptr,boundsTarget.put()));
        desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        check_hresult(device->CreateTexture2D(&desc,nullptr,boundsReadback.put()));effectiveRegions.resize(samples.size());
    }
    void process(const Direct3D11CaptureFrame& frame) {
        auto access=frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        com_ptr<ID3D11Texture2D> source;
        check_hresult(access->GetInterface(__uuidof(ID3D11Texture2D),source.put_void()));
        processTexture(source.get());
    }
    void capturePreview() {
        if(!input) return; // Keep the request until the first real frame arrives.
        try {
            D3D11_TEXTURE2D_DESC previewDesc{};input->GetDesc(&previewDesc);
            previewDesc.Height=std::max(1u,static_cast<unsigned>(640.0*previewDesc.Height/previewDesc.Width));previewDesc.Width=640;
            previewDesc.MipLevels=1;previewDesc.MiscFlags=0;
            if(!preview) {
                previewDesc.Usage=D3D11_USAGE_DEFAULT;previewDesc.BindFlags=D3D11_BIND_RENDER_TARGET;previewDesc.CPUAccessFlags=0;
                check_hresult(device->CreateTexture2D(&previewDesc,nullptr,previewTarget.put()));
                check_hresult(device->CreateRenderTargetView(previewTarget.get(),nullptr,previewView.put()));
                previewDesc.Usage=D3D11_USAGE_STAGING;previewDesc.BindFlags=0;previewDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
                check_hresult(device->CreateTexture2D(&previewDesc,nullptr,preview.put()));
            }
            D3D11_VIEWPORT viewport{0,0,640.f,static_cast<float>(previewDesc.Height),0,1};context->RSSetViewports(1,&viewport);
            auto* rt=previewView.get();context->OMSetRenderTargets(1,&rt,nullptr);
            context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context->VSSetShader(vertex.get(),nullptr,0);context->PSSetShader(previewPixel.get(),nullptr,0);
            auto* constants=options.get();context->PSSetConstantBuffers(1,1,&constants);
            auto* view=inputView.get();context->PSSetShaderResources(0,1,&view);
            auto* filter=sampler.get();context->PSSetSamplers(0,1,&filter);context->Draw(3,0);
            ID3D11ShaderResourceView* none=nullptr;context->PSSetShaderResources(0,1,&none);
            context->CopyResource(preview.get(),previewTarget.get());
            D3D11_MAPPED_SUBRESOURCE mapped{};
            check_hresult(context->Map(preview.get(),0,D3D11_MAP_READ,0,&mapped));
            QImage image(static_cast<const uchar*>(mapped.pData),previewDesc.Width,previewDesc.Height,mapped.RowPitch,QImage::Format_ARGB32);
            QByteArray encoded;QBuffer buffer(&encoded);buffer.open(QIODevice::WriteOnly);bool saved=image.save(&buffer,"PNG");
            context->Unmap(preview.get(),0);
            if(!saved) throw std::runtime_error("Cannot encode the native preview");
            previewPng=std::move(encoded);previewError.clear();
        } catch(hresult_error const& e) {previewError=to_string(e.message());preview=nullptr;}
          catch(std::exception const& e) {previewError=e.what();}
        wantPreview=false;
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
            input=nullptr; inputView=nullptr;preview=nullptr;previewTarget=nullptr;previewView=nullptr;
            check_hresult(device->CreateTexture2D(&desc,nullptr,input.put()));
            check_hresult(device->CreateShaderResourceView(input.get(),nullptr,inputView.put()));
        }
        auto sx=[&](LONG v) { return static_cast<UINT>(std::clamp((double)(v-host.left)/(host.right-host.left)*sourceW,0.0,(double)sourceW)); };
        auto sy=[&](LONG v) { return static_cast<UINT>(std::clamp((double)(v-host.top)/(host.bottom-host.top)*sourceH,0.0,(double)sourceH)); };
        D3D11_BOX crop{sx(monitor.left),sy(monitor.top),0,sx(monitor.right),sy(monitor.bottom),1};
        context->CopySubresourceRegion(input.get(),0,0,0,0,source,0,&crop);
        context->GenerateMips(inputView.get());
        sampleRetained();
        if(wantPreview) capturePreview();
        received=Clock::now(); ++frames;
        error.clear();
        if(frames==1) firstReceived=received;
        lastProcessingMs=std::chrono::duration<double,std::milli>(received-began).count();
    }
    void sampleRetained() {
        if(!input) return;
        D3D11_VIEWPORT viewport{0,0,static_cast<float>(colors.size()),1,0,1};
        context->RSSetViewports(1,&viewport);
        ID3D11RenderTargetView* targets[]={target.get(),boundsTarget.get()};context->OMSetRenderTargets(2,targets,nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vertex.get(),nullptr,0); context->PSSetShader(pixel.get(),nullptr,0);
        auto* constants=regions.get(); context->PSSetConstantBuffers(0,1,&constants);
        auto* settings=options.get();context->PSSetConstantBuffers(1,1,&settings);
        auto* view=inputView.get(); context->PSSetShaderResources(0,1,&view);
        auto* filter=sampler.get(); context->PSSetSamplers(0,1,&filter);
        context->Draw(3,0);
        ID3D11ShaderResourceView* none=nullptr; context->PSSetShaderResources(0,1,&none);
        context->CopyResource(readback.get(),output.get());
        D3D11_MAPPED_SUBRESOURCE mapped{}; check_hresult(context->Map(readback.get(),0,D3D11_MAP_READ,0,&mapped));
        auto* bytes=static_cast<const unsigned char*>(mapped.pData);
        for(size_t i=0;i<colors.size();++i) colors[i]=ToRGBColor(bytes[i*4],bytes[i*4+1],bytes[i*4+2]);
        context->Unmap(readback.get(),0);
        context->CopyResource(boundsReadback.get(),boundsOutput.get());
        check_hresult(context->Map(boundsReadback.get(),0,D3D11_MAP_READ,0,&mapped));
        std::memcpy(effectiveRegions.data(),mapped.pData,effectiveRegions.size()*sizeof(Sample));context->Unmap(boundsReadback.get(),0);
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
        try {
        pool=Direct3D11CaptureFramePool::CreateFreeThreaded(winDevice,DirectXPixelFormat::B8G8R8A8UIntNormalized,2,item.Size());
        session=pool.CreateCaptureSession(item); session.IsCursorCaptureEnabled(false);
        try { session.IsBorderRequired(false); } catch(hresult_error const&) {}
        auto owned=state;
        closed=item.Closed([owned](auto const&,auto const&) { std::lock_guard lock(owned->mutex); owned->closed=true; });
        auto size=item.Size();
        arrived=pool.FrameArrived([owned,winDevice,size](auto const& sender,auto const&) mutable {
            std::lock_guard lock(owned->mutex);
            if(!owned->active) return;
            try {
                auto frame=sender.TryGetNextFrame(); if(!frame) return;
                auto content=frame.ContentSize();
                auto callbackBegan=Clock::now();
                if(content.Width==size.Width && content.Height==size.Height) {owned->process(frame);owned->received=callbackBegan;}
                frame.Close();
                if(content.Width>0 && content.Height>0 && (content.Width!=size.Width || content.Height!=size.Height)) {
                    size=content;
                    sender.Recreate(winDevice,DirectXPixelFormat::B8G8R8A8UIntNormalized,2,size);
                }
            }
            catch(hresult_error const& error) { owned->error=to_string(error.message());if(FAILED(owned->device->GetDeviceRemovedReason())) owned->closed=true; }
            catch(std::exception const& error) { owned->error=error.what(); }
        });
        session.StartCapture();
        } catch(...) {stop();throw;}
    }
    void stop() noexcept {
        { std::lock_guard lock(state->mutex); state->active=false; }
        try {if(pool && arrived.value) pool.FrameArrived(arrived);} catch(...) {}
        try {if(item && closed.value) item.Closed(closed);} catch(...) {}
        try {if(session) session.Close();} catch(...) {}
        try {if(pool) pool.Close();} catch(...) {}
    }
    ~Capture() {stop();}
};
} // namespace

struct OfficeAmbience::Impl {
    QTimer output;
    OpenRGBPluginAPIInterface* api;
    std::filesystem::path configPath,statusPath;
    std::wstring display;
    std::vector<Device> devices;
    mutable std::mutex controllersMutex;
    std::string controllerError;
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
    uint64_t deliveredFrames=0,revision=1,sourceGeneration=0;
    double measuredFps=0;
    uint64_t previousStatusOutput=0;
    Json config,layout;
    std::unique_ptr<OfficeControlServer> control;
    std::string identifyDevice;
    bool calibration=false;
    Clock::time_point identifyUntil{};
    HWND eventWindow{};
    bool initialized=false;
    std::atomic_bool dirty{true};
    std::atomic_bool invalidate{false};
    HWINEVENTHOOK systemHook{},objectHook{};
    static inline std::map<HWINEVENTHOOK,Impl*> hooks;
    Apartment apartment;
    DpiContext dpi;

    static LRESULT CALLBACK nativeEvent(HWND window,UINT message,WPARAM w,LPARAM l) {
        auto self=reinterpret_cast<Impl*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if(message==WM_NCCREATE) {
            self=static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
            SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
        }
        if(self && (message==WM_DISPLAYCHANGE || (message==WM_POWERBROADCAST && w==PBT_APMRESUMEAUTOMATIC) ||
                    (message==WM_WTSSESSION_CHANGE && (w==WTS_SESSION_UNLOCK || w==WTS_CONSOLE_CONNECT || w==WTS_REMOTE_CONNECT)))) {self->invalidate=true;self->dirty=true;}
        return DefWindowProcW(window,message,w,l);
    }

    Json defaultLayout() const {
        auto result=Json::array();
        for(auto& d:devices) {
            auto regions=std::vector<Sample>(samples.begin()+d.start,samples.begin()+d.start+d.count);
            auto groups=Json::array();
            auto group=[&](const char* id,const char* name,unsigned first,unsigned count) {
                groups.push_back({{"id",id},{"name",name},{"first",first},{"count",count}});
            };
            if(d.key=="H6609") {group("left","Monitor - Left",0,3);group("top","Monitor - Top",3,7);group("right","Monitor - Right",10,4);group("bottom","Monitor - Bottom",14,6);}
            else if(d.key=="H8048") {group("left","Tower - Left",0,10);group("right","Tower - Right",10,10);}
            else if(d.key=="Legion") {group("keys","Laptop keyboard",0,102);group("front","Laptop front",102,6);group("aux","Laptop auxiliary",108,1);}
            else group("all",d.name.c_str(),0,static_cast<unsigned>(d.count));
            result.push_back({{"id",d.key},{"name",d.name},{"regions",office::encode(regions)},
                              {"led_names",d.ledNames},{"groups",groups},{"enabled",true}});
        }
        return result;
    }
    std::vector<Sample> validatedLayout(const Json& candidate) const {
        if(!candidate.is_array() || candidate.size()!=devices.size()) throw std::runtime_error("The map must contain all six connected devices");
        auto defaults=defaultLayout();std::vector<Sample> result;
        for(size_t i=0;i<devices.size();++i) {
            const auto& value=candidate.at(i);const auto& expected=defaults.at(i);
            if(value.at("id")!=devices[i].key || !value.at("enabled").is_boolean()) throw std::runtime_error("Device identity changed");
            if(value.at("name")!=expected.at("name") || value.at("led_names")!=expected.at("led_names")) throw std::runtime_error("Physical LED metadata cannot be renamed by editing zone names");
            auto points=office::validateRegions(value.at("regions"),devices[i].count);
            if(value.at("groups").size()!=expected.at("groups").size()) throw std::runtime_error("Physical zone grouping changed");
            for(size_t g=0;g<value.at("groups").size();++g) {
                const auto& group=value["groups"][g];const auto& baseline=expected["groups"][g];
                for(auto key:{"id","first","count"}) if(group.at(key)!=baseline.at(key)) throw std::runtime_error("Hardware LED identities cannot be renumbered by editing the map");
                auto name=group.at("name").get<std::string>();if(name.empty() || name.size()>120) throw std::runtime_error("Zone names need 1-120 UTF-8 bytes");
            }
            result.insert(result.end(),points.begin(),points.end());
        }
        return result;
    }
    void validateSettings(const Json& candidate) const {
        if(candidate.at("monitor")!=config.at("monitor")) throw std::runtime_error("This panel controls the configured LG display only");
        if(!candidate.at("enabled").is_boolean() || !candidate.at("fps").is_number_integer() || candidate.at("fps")<1 || candidate.at("fps")>60)
            throw std::runtime_error("Output FPS must be an integer from 1 to 60");
        double smooth=candidate.at("smoothing_ms").get<double>();
        if(!std::isfinite(smooth) || smooth<0 || smooth>2000) throw std::runtime_error("Smoothing must be 0-2000 milliseconds");
        office::validateProcessing(candidate);
    }
    Json snapshot(bool preview=false) {
        if(preview && capture) {std::lock_guard lock(capture->state->mutex);capture->state->wantPreview=true;capture->state->capturePreview();}
        auto colours=Json::array();
        for(auto& d:devices) {auto row=Json::array();for(auto c:d.colors) row.push_back(Json::array({RGBGetRValue(c),RGBGetGValue(c),RGBGetBValue(c)}));colours.push_back(row);}
        std::string image,previewError;Json effective=Json::array();
        if(capture) {std::lock_guard lock(capture->state->mutex);image=capture->state->previewPng.toBase64().toStdString();previewError=capture->state->previewError;
            for(auto& d:devices) effective.push_back(office::encode(std::vector<Sample>(capture->state->effectiveRegions.begin()+d.start,capture->state->effectiveRegions.begin()+d.start+d.count)));}
        auto online=Json::array();{std::lock_guard lock(controllersMutex);for(auto& d:devices) online.push_back(d.controller!=nullptr);}
        Json settings={{"monitor",config.at("monitor")},{"enabled",config.at("enabled")},{"fps",config.at("fps")},{"smoothing_ms",config.at("smoothing_ms")}};
        const auto defaults=office::processingDefaults();for(auto& [key,value]:defaults.items()) settings[key]=config.at(key);
        return {{"revision",revision},{"online",online},{"settings",settings},
                {"layout",layout},{"status",report()},{"colors",colours},{"effective_regions",effective},{"preview_png",image},{"preview_error",previewError},
                {"test_mode",calibration?"calibration":identifyDevice.empty()?"none":"identify"}};
    }
    Json command(const Json& request) {
        auto name=request.at("command").get<std::string>();
        if(name=="snapshot") return snapshot(request.value("preview",false));
        if(name=="recover") {invalidate=true;dirty=true;return {{"accepted",true}};}
        if(name=="stop_test") {calibration=false;identifyDevice.clear();return {{"accepted",true}};}
        if(name=="identify") {
            auto key=request.at("device").get<std::string>();
            if(std::none_of(devices.begin(),devices.end(),[&](const auto& d){return d.key==key;})) throw std::runtime_error("Unknown physical device");
            calibration=false;identifyDevice=key;
            // User-visible identify duration; never a capture/recovery deadline.
            identifyUntil=Clock::now()+std::chrono::seconds(2);return {{"accepted",true}};
        }
        if(name=="calibrate") {identifyDevice.clear();calibration=true;return {{"accepted",true}};}
        if(name!="save") throw std::runtime_error("Unknown control command");
        if(request.at("revision").get<uint64_t>()!=revision) throw std::runtime_error("Settings changed in another panel. Reload before saving");
        auto next=config;
        for(auto key:{"monitor","enabled","fps","smoothing_ms"}) next[key]=request.at("settings").at(key);
        const auto defaults=office::processingDefaults();for(auto& [key,value]:defaults.items()) if(request.at("settings").contains(key)) next[key]=request.at("settings").at(key);
        validateSettings(next);
        auto proposed=request.at("layout");auto points=validatedLayout(proposed);next["layout"]=proposed;
        // Detect external edits before replacing a private configuration file.
        std::ifstream currentFile(configPath);Json onDisk;currentFile>>onDisk;
        // MSVC's stream handle does not share delete access. Release it before
        // QSaveFile atomically replaces the file identity on Windows.
        currentFile.close();
        // Compare against the exact loaded document, including its original defaults.
        if(onDisk!=diskConfig) throw std::runtime_error("The private config was edited externally; restart the owner to reload it");
        std::unique_lock<std::mutex> lock;
        com_ptr<ID3D11Buffer> newRegions;
        com_ptr<ID3D11Buffer> newOptions;
        if(capture) {lock=std::unique_lock(capture->state->mutex);newRegions=capture->state->makeRegions(points);newOptions=capture->state->makeOptions(next);}
        QSaveFile file(QString::fromStdWString(configPath.wstring()));
        auto bytes=next.dump(2);
        if(!file.open(QIODevice::WriteOnly) || file.write(bytes.data(),bytes.size())!=static_cast<qint64>(bytes.size()) || !file.commit())
            throw std::runtime_error("Could not save settings; the active map is unchanged");
        if(capture) {capture->state->regions=std::move(newRegions);capture->state->options=std::move(newOptions);capture->state->sampleRetained();}
        samples=std::move(points);layout=std::move(proposed);config=next;diskConfig=next;smoothingMs=config["smoothing_ms"].get<double>();
        output.setInterval(std::max(1,static_cast<int>(std::round(1000.0/config["fps"].get<int>()))));
        ++revision;
        if(lock.owns_lock()) lock.unlock();
        return snapshot();
    }
    Json diskConfig;

    static void CALLBACK winEvent(HWINEVENTHOOK hook,DWORD,HWND,LONG object,LONG,DWORD,DWORD) {
        if(object!=OBJID_WINDOW) return;
        auto found=hooks.find(hook); if(found!=hooks.end()) found->second->dirty=true;
    }
    // Test construction skips only OS event/IPC activation, retaining the actual
    // inventory, map and output implementation used by the installed owner.
    Impl(OpenRGBPluginAPIInterface* api,const std::filesystem::path& path,bool activate=true):api(api),configPath(path) {
        std::ifstream file(path);file>>config;diskConfig=config;
        if(!config.contains("monitor")) config["monitor"]="\\\\.\\DISPLAY2";
        if(!config.contains("fps")) config["fps"]=15;
        if(!config.contains("smoothing_ms")) config["smoothing_ms"]=140;
        if(!config.contains("enabled")) config["enabled"]=true;
        office::processingDefaults(config);
        validateSettings(config);
        auto wide=to_hstring(config.value("monitor",std::string("\\\\.\\DISPLAY2")));
        display.assign(wide.c_str(),wide.size());
        int fps=config.value("fps",15);
        if(fps<1 || fps>60) throw std::runtime_error("Office FPS must be in 1..60");
        smoothingMs=config.value("smoothing_ms",140.0);
        statusPath=path.parent_path()/"office-ambience-status.json";
        std::map<std::string,RGBControllerInterface*> live;
        std::vector<std::string> order;
        for(auto* controller:api->GetRGBControllers()) {
            auto key=deviceKey(controller);
            if(key.empty() || controller->GetLEDCount()!=expectedCount(key)) {
                if(!config.contains("layout")) throw std::runtime_error("Unexpected office controller before the initial map exists");
                controllerError="A detected controller has no matching office identity/LED count; its saved map remains offline";
                continue;
            }
            if(!live.emplace(key,controller).second) throw std::runtime_error("Duplicate office controller identity");
            order.push_back(key);
        }
        if(config.contains("layout")) {
            order.clear();for(auto& d:config["layout"]) order.push_back(d.at("id").get<std::string>());
        }
        std::set<std::string> identities;
        for(size_t i=0;i<order.size();++i) {
            const auto& key=order[i];auto count=expectedCount(key);
            if(!identities.insert(key).second) throw std::runtime_error("Duplicate persisted controller identity");
            auto* controller=live.contains(key)?live.at(key):nullptr;
            std::vector<Sample> points;Json names=Json::array();std::string name;
            if(controller) {points=deviceSamples(controller);name=controller->GetName();for(unsigned led=0;led<count;++led) names.push_back(controller->GetLEDName(led));}
            else {
                const auto& saved=config.at("layout").at(i);
                points=office::validateRegions(saved.at("regions"),count);name=saved.at("name").get<std::string>();names=saved.at("led_names");
                if(!names.is_array() || names.size()!=count || !std::all_of(names.begin(),names.end(),[](const auto& n){return n.is_string();})) throw std::runtime_error("Persisted LED metadata is invalid");
            }
            devices.push_back({controller,samples.size(),points.size(),std::vector<RGBColor>(points.size()),key,name,names});
            samples.insert(samples.end(),points.begin(),points.end());
        }
        if(devices.size()!=6 || samples.size()!=314) throw std::runtime_error("Office ambience requires the verified six-device 314-LED inventory");
        layout=defaultLayout();
        if(config.contains("layout")) {samples=validatedLayout(config["layout"]);layout=config["layout"];}
        for(auto& device:devices) if(device.controller) device.controller->SetCustomMode();
        if(!activate) return;
        try {
        systemHook=SetWinEventHook(EVENT_SYSTEM_FOREGROUND,EVENT_SYSTEM_MINIMIZEEND,nullptr,winEvent,0,0,WINEVENT_OUTOFCONTEXT);
        objectHook=SetWinEventHook(EVENT_OBJECT_CREATE,EVENT_OBJECT_LOCATIONCHANGE,nullptr,winEvent,0,0,WINEVENT_OUTOFCONTEXT);
        if(!systemHook || !objectHook) {
            throw std::runtime_error("Window-state event registration failed");
        }
        hooks[systemHook]=this; hooks[objectHook]=this;
        WNDCLASSW cls{};cls.lpfnWndProc=nativeEvent;cls.lpszClassName=L"OpenRGBOfficeCaptureEvents";cls.hInstance=GetModuleHandleW(nullptr);
        if(!RegisterClassW(&cls) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) throw std::runtime_error("Cannot register capture lifecycle events");
        eventWindow=CreateWindowW(cls.lpszClassName,L"",0,0,0,0,0,nullptr,nullptr,cls.hInstance,this);
        if(!eventWindow || !WTSRegisterSessionNotification(eventWindow,NOTIFY_FOR_THIS_SESSION)) throw std::runtime_error("Cannot register session capture events");
        control=std::make_unique<OfficeControlServer>(QString::fromStdWString(path.parent_path().wstring()),[this](const Json& request){return command(request);});
        QObject::connect(&output,&QTimer::timeout,[this] { tick(); });
        output.setTimerType(Qt::PreciseTimer); output.start(std::max(1,static_cast<int>(std::round(1000.0/fps))));
        status();
        } catch(...) {control.reset();releaseEvents();throw;}
    }
    void releaseEvents() noexcept {
        if(eventWindow) {WTSUnRegisterSessionNotification(eventWindow);DestroyWindow(eventWindow);eventWindow=nullptr;}
        UnregisterClassW(L"OpenRGBOfficeCaptureEvents",GetModuleHandleW(nullptr));
        for(auto hook:{systemHook,objectHook}) if(hook) { hooks.erase(hook); UnhookWinEvent(hook); }
        systemHook=objectHook=nullptr;
    }
    ~Impl() {
        control.reset();
        output.stop();
        releaseEvents();
        capture.reset();
        mode="stopped"; status();
    }
    void resourcesUpdated(unsigned reason) {
        // OpenRGB emits these synchronously while removed controllers are still
        // alive. This barrier must not be queued onto the Qt output thread:
        // removal may free a controller immediately after this callback returns.
        std::lock_guard lock(controllersMutex);
        if(reason==RESOURCEMANAGER_UPDATE_REASON_DETECTION_STARTED) {
            for(auto& d:devices) d.controller=nullptr;
            return;
        }
        if(reason!=RESOURCEMANAGER_UPDATE_REASON_DEVICE_LIST_UPDATED && reason!=RESOURCEMANAGER_UPDATE_REASON_DETECTION_COMPLETE) return;
        auto current=api->GetRGBControllers();controllerError.clear();
        for(auto& d:devices) {
            RGBControllerInterface* found=nullptr;
            for(auto* c:current) if(deviceKey(c)==d.key) {
                if(found || c->GetLEDCount()!=d.count) {controllerError="Reconnected controller identity/count differs: "+d.key;found=nullptr;break;}
                found=c;
            }
            if(found && found!=d.controller) found->SetCustomMode();
            d.controller=found;
        }
    }
    void chooseSource() {
        auto monitor=monitorByName(display);
        HWND candidate=monitor.handle?maximizedApp(monitor):nullptr;
        std::string selected=candidate?"app":"wallpaper";
        if(!candidate && monitor.handle) candidate=wallpaperHost(monitor);
        RECT bounds{}; if(candidate) GetWindowRect(candidate,&bounds);
        bool force=invalidate.exchange(false);
        if(!force && candidate==source && capture && EqualRect(&bounds,&sourceBounds) && EqualRect(&monitor.bounds,&monitorBounds)) return;
        capture.reset(); source=candidate; mode=candidate?selected:"waiting";
        ++sourceGeneration;
        sourceBounds=bounds; monitorBounds=monitor.bounds;
        switched=Clock::now();firstFrameDelayMs=0;inputFrames=deliveredFrames=0;
        if(!candidate) error="No capturable wallpaper host or maximized LG application is available; holding the last valid colours";
        if(candidate) try { capture=std::make_unique<Capture>(candidate,monitor.bounds,samples);std::lock_guard lock(capture->state->mutex);capture->state->configure(config); }
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
                    if(state->error.empty()) error.clear();
                    if(firstFrameDelayMs==0) firstFrameDelayMs=std::chrono::duration<double,std::milli>(state->firstReceived-switched).count();
                }
                if(!state->error.empty()) error=state->error;
            }
            if(closed) { capture.reset(); source=nullptr; dirty=true; }
        }
        if(!identifyDevice.empty() && now>=identifyUntil) identifyDevice.clear();
        if(!latest.empty() || calibration || !identifyDevice.empty()) {
            auto submitBegan=Clock::now();
            double elapsedMs=std::chrono::duration<double,std::milli>(now-lastOutput).count();
            size_t deviceIndex=0;
            {std::lock_guard lock(controllersMutex);
            for(auto& device:devices) {
                for(size_t index=0;index<device.count;++index) {
                    bool enabled=config["enabled"].get<bool>() && layout[deviceIndex]["enabled"].get<bool>();
                    auto target=latest.empty()?0:latest[device.start+index];
                    bool testing=calibration || !identifyDevice.empty();
                    if(calibration) {
                        const auto p=samples[device.start+index];float x=(p.left+p.right)/2,y=(p.top+p.bottom)/2;
                        target=ToRGBColor(static_cast<unsigned char>(x*255),static_cast<unsigned char>((1-y)*255),static_cast<unsigned char>(index%2?40:160));
                    } else if(!identifyDevice.empty()) target=device.key==identifyDevice?ToRGBColor(255,255,255):0;
                    else if(!enabled) target=0;
                    auto previous=device.colors[index];
                    double delta=std::max({std::abs(int(RGBGetRValue(target))-int(RGBGetRValue(previous))),std::abs(int(RGBGetGValue(target))-int(RGBGetGValue(previous))),std::abs(int(RGBGetBValue(target))-int(RGBGetBValue(previous)))});
                    SmoothingColorPostProcessor smooth{office::smoothingWeight(config,elapsedMs,delta)};
                    auto color=initialized?smooth.process(RGBGetRValue(target),RGBGetGValue(target),RGBGetBValue(target),previous):target;
                    if(testing || !enabled) color=target;
                    device.colors[index]=color;
                    if(device.controller) device.controller->SetColor(static_cast<unsigned>(index),color);
                }
                if(device.controller) device.controller->UpdateLEDs();
                ++deviceIndex;
            }}
            initialized=true; ++outputFrames;
            auto submitted=Clock::now();
            submitMs=std::chrono::duration<double,std::milli>(submitted-submitBegan).count();
            if(inputFrames && deliveredFrames!=inputFrames) {
                callbackToSubmitMs=std::chrono::duration<double,std::milli>(submitted-received).count();deliveredFrames=inputFrames;
            }
        }
        lastOutput=now;
        // Diagnostic reporting cadence never classifies a silent/static frame as
        // failure and never cancels or restarts a valid capture session.
        if(now-lastStatus>=std::chrono::seconds(1)) status();
    }
    Json report() const {
        std::lock_guard lock(controllersMutex);
        return {{"mode",mode},{"source_hwnd",reinterpret_cast<uintptr_t>(source)},
            {"source_generation",sourceGeneration},
            {"devices",devices.size()},{"leds",samples.size()},{"output_frames",outputFrames},
            {"input_frames",inputFrames},{"gpu_processing_ms",processingMs},
            {"callback_to_native_submit_ms",callbackToSubmitMs},{"native_submit_ms",submitMs},
            {"frame_age_ms",received==Clock::time_point{}?Json(nullptr):Json(std::chrono::duration<double,std::milli>(Clock::now()-received).count())},
            {"output_fps",measuredFps},
            {"online_devices",std::count_if(devices.begin(),devices.end(),[](const auto& d){return d.controller!=nullptr;})},{"controller_error",controllerError},
            {"first_frame_after_switch_ms",firstFrameDelayMs},{"has_valid_frame",initialized},
            {"error",error},{"updated_utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toStdString()}};
    }
    void status() {
        auto elapsed=std::chrono::duration<double>(Clock::now()-lastStatus).count();
        if(elapsed>0) measuredFps=(outputFrames-previousStatusOutput)/elapsed;
        previousStatusOutput=outputFrames;
        auto value=report();
        QSaveFile file(QString::fromStdWString(statusPath.wstring()));
        if(file.open(QIODevice::WriteOnly)) { auto data=value.dump(2); file.write(data.data(),data.size()); file.commit(); }
        lastStatus=Clock::now();
    }
};

OfficeAmbience::OfficeAmbience(OpenRGBPluginAPIInterface* api,const std::filesystem::path& config):impl(std::make_unique<Impl>(api,config)) {}
OfficeAmbience::~OfficeAmbience()=default;
void OfficeAmbience::stop() { impl.reset(); }
void OfficeAmbience::resourcesUpdated(unsigned reason) {if(impl) impl->resourcesUpdated(reason);}
