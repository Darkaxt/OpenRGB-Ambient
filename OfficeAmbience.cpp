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
#include "OfficeJournal.h"
#include "OfficeHardware.h"
#include "DisplayBrightness.h"
#include "OfficeScreen.h"
#include "OfficeAwake.h"
#include "OfficeRest.h"
#include "OfficeSamplingShader.h"
#include <OpenRGBPluginInterface.h>
#include <ResourceManagerCallback.h>
#include <QTimer>
#include <QSaveFile>
#include <QFile>
#include <QDateTime>
#include <QImage>
#include <QBuffer>
#include <QImageWriter>
#include <QUuid>
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
#include <condition_variable>
#include <thread>
#include <functional>
#include <numeric>
#include <set>
#include <cstring>
#include "OfficeWallpaper.h"

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
struct Monitor { HMONITOR handle{}; RECT bounds{}; std::string name; };

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
Monitor primaryMonitor() {
    Monitor result;
    EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR handle,HDC,LPRECT,LPARAM data)->BOOL {
        MONITORINFOEXW info{}; info.cbSize=sizeof(info);
        if(GetMonitorInfoW(handle,&info) && (info.dwFlags&MONITORINFOF_PRIMARY)) {
            *reinterpret_cast<Monitor*>(data)={handle,info.rcMonitor,to_string(info.szDevice)};
            return FALSE;
        }
        return TRUE;
    },reinterpret_cast<LPARAM>(&result));
    return result;
}
void readDisplayBrightness(office::DisplayBrightness& brightness,const Monitor& monitor) {
    auto name=to_hstring(monitor.name);
    HDC dc=monitor.handle?CreateDCW(L"DISPLAY",name.c_str(),nullptr,nullptr):nullptr;
    office::GammaRamp ramp{};
    bool ok=dc && GetDeviceGammaRamp(dc,&ramp);
    const DWORD failure=ok?0:GetLastError();
    if(dc) DeleteDC(dc);
    brightness.observe(monitor.name,ok?std::optional(ramp):std::nullopt,
        "Display gamma read unavailable (Win32 "+std::to_string(failure)+")");
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

// Enumerate the actual input desktop without switching the user's desktop or
// moving the Qt event thread (which already owns windows) to another desktop.
struct InputDesktop {
    HDESK handle=OpenInputDesktop(0,FALSE,DESKTOP_READOBJECTS|DESKTOP_ENUMERATE);
    std::wstring name;
    InputDesktop() {
        if(handle) {wchar_t value[256]{};DWORD needed{};
            if(GetUserObjectInformationW(handle,UOI_NAME,value,sizeof(value),&needed)) name=value;}
    }
    ~InputDesktop() {if(handle) CloseDesktop(handle);}
    bool secure() const {return !handle || (name!=L"Default" && name!=L"Screen-saver");}
    bool screensaver() const {
        BOOL running=FALSE;SystemParametersInfoW(SPI_GETSCREENSAVERRUNNING,0,&running,0);
        return name==L"Screen-saver" || running;
    }
};
HWND screensaverWindow(HDESK desktop,const Monitor& monitor,bool saverDesktop=true) {
    struct Search {const Monitor& monitor;bool saverDesktop;HWND window{};} search{monitor,saverDesktop};
    EnumDesktopWindows(desktop,[](HWND window,LPARAM data)->BOOL {
        auto& s=*reinterpret_cast<Search*>(data);RECT r{};
        if(!IsWindowVisible(window) || IsIconic(window) || !GetWindowRect(window,&r)) return TRUE;
        const auto& m=s.monitor.bounds;
        if(r.left>m.left+2 || r.top>m.top+2 || r.right<m.right-2 || r.bottom<m.bottom-2) return TRUE;
        auto cls=windowClass(window);
        if(cls==L"Progman" || cls==L"WorkerW" || cls==L"Shell_TrayWnd" || cls==L"Shell_SecondaryTrayWnd") return TRUE;
        // Manually launching a .scr does not set SPI_GETSCREENSAVERRUNNING or
        // switch desktops. The installed Wallpaper Engine saver also uses a
        // tool window spanning both screens, which ordinary app selection skips.
        if(!s.saverDesktop && cls!=L"WindowsScreenSaverClass") {
            auto name=ownerName(window);std::transform(name.begin(),name.end(),name.begin(),towlower);
            if(!name.ends_with(L".scr")) return TRUE;
        }
        s.window=window;return FALSE;
    },reinterpret_cast<LPARAM>(&search));
    return search.window;
}

struct Device {
    RGBControllerInterface* controller;
    size_t start,count;
    std::vector<RGBColor> colors;
    std::string key;
    std::string name;
    Json ledNames;
    bool delivered=false;
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

// Bytecode is independent of the capture window and D3D device. Recompiling
// these identical programs on every unlock adds seconds before a fresh frame.
struct CapturePrograms {
    com_ptr<ID3DBlob> vertex,pixel,preview,content,compare;
    CapturePrograms() {
        auto compile=[](const char* entry,const char* profile,com_ptr<ID3DBlob>& result) {
            com_ptr<ID3DBlob> errors;
            auto hr=D3DCompile(shader,strlen(shader),nullptr,nullptr,nullptr,entry,profile,D3DCOMPILE_OPTIMIZATION_LEVEL3,0,result.put(),errors.put());
            if(FAILED(hr)) throw std::runtime_error(errors?std::string(static_cast<const char*>(errors->GetBufferPointer()),errors->GetBufferSize()):"GPU shader compilation failed");
        };
        compile("vs","vs_5_0",vertex);compile("ps","ps_5_0",pixel);
        compile("previewPs","ps_5_0",preview);compile("detectContentPs","ps_5_0",content);
        compile("compareCs","cs_5_0",compare);
    }
};
const CapturePrograms& capturePrograms() {static const CapturePrograms programs;return programs;}

struct CaptureState {
    std::mutex mutex;
    bool active=true,closed=false;
    std::string error;
    std::vector<RGBColor> colors;
    std::vector<Sample> effectiveRegions;
    uint64_t frames=0,changedFrames=0,skippedFrames=0,samplingPasses=0;
    double lastProcessingMs=0;
    Clock::time_point received{};
    Clock::time_point firstReceived{};
    com_ptr<ID3D11Device> device;
    com_ptr<ID3D11DeviceContext> context;
    com_ptr<ID3D11Texture2D> input,output,readback;
    com_ptr<ID3D11ShaderResourceView> inputView;
    com_ptr<ID3D11Texture2D> incoming;
    com_ptr<ID3D11ShaderResourceView> incomingView;
    com_ptr<ID3D11ComputeShader> compare;
    com_ptr<ID3D11Buffer> difference,differenceReadback;
    com_ptr<ID3D11UnorderedAccessView> differenceView;
    com_ptr<ID3D11RenderTargetView> target;
    com_ptr<ID3D11Texture2D> boundsOutput,boundsReadback;
    com_ptr<ID3D11RenderTargetView> boundsTarget;
    com_ptr<ID3D11VertexShader> vertex;
    com_ptr<ID3D11PixelShader> pixel;
    com_ptr<ID3D11PixelShader> previewPixel;
    com_ptr<ID3D11PixelShader> contentPixel;
    com_ptr<ID3D11Texture2D> contentTexture;
    com_ptr<ID3D11RenderTargetView> contentTarget;
    com_ptr<ID3D11ShaderResourceView> contentView;
    com_ptr<ID3D11Buffer> options;
    com_ptr<ID3D11Buffer> regions;
    com_ptr<ID3D11SamplerState> sampler;
    unsigned sourceW=0,sourceH=0;
    RECT host{},monitor{};
    std::mutex previewMutex;
    std::condition_variable previewCondition;
    bool previewPending=false,previewStopping=false;
    uint64_t previewRevision=0,previewAttempt=0,previewGeneration=0,previewEncodes=0;
    std::string previewBase64;
    std::string previewError;
    double previewEncodeMs=0;
    float previewRequestedBlur=-1,previewAttemptBlur=-2,previewRenderedBlur=-1;
    float previewBufferBlur=-2;
    com_ptr<ID3D11Buffer> previewOptions;
    std::thread previewWorker;
#ifdef OFFICE_CAPTURE_TEST
    std::function<void()> beforePreviewEncode;
#endif
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
        const auto& programs=capturePrograms();
        check_hresult(device->CreateVertexShader(programs.vertex->GetBufferPointer(),programs.vertex->GetBufferSize(),nullptr,vertex.put()));
        check_hresult(device->CreatePixelShader(programs.pixel->GetBufferPointer(),programs.pixel->GetBufferSize(),nullptr,pixel.put()));
        check_hresult(device->CreatePixelShader(programs.preview->GetBufferPointer(),programs.preview->GetBufferSize(),nullptr,previewPixel.put()));
        check_hresult(device->CreatePixelShader(programs.content->GetBufferPointer(),programs.content->GetBufferSize(),nullptr,contentPixel.put()));
        check_hresult(device->CreateComputeShader(programs.compare->GetBufferPointer(),programs.compare->GetBufferSize(),nullptr,compare.put()));
        D3D11_BUFFER_DESC flag{};flag.ByteWidth=4;flag.Usage=D3D11_USAGE_DEFAULT;
        flag.BindFlags=D3D11_BIND_UNORDERED_ACCESS;flag.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
        check_hresult(device->CreateBuffer(&flag,nullptr,difference.put()));
        D3D11_UNORDERED_ACCESS_VIEW_DESC view{};view.Format=DXGI_FORMAT_R32_TYPELESS;view.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;
        view.Buffer.NumElements=1;view.Buffer.Flags=D3D11_BUFFER_UAV_FLAG_RAW;
        check_hresult(device->CreateUnorderedAccessView(difference.get(),&view,differenceView.put()));
        flag.Usage=D3D11_USAGE_STAGING;flag.BindFlags=0;flag.MiscFlags=0;flag.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        check_hresult(device->CreateBuffer(&flag,nullptr,differenceReadback.put()));
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
        desc.Width=desc.Height=1;desc.Usage=D3D11_USAGE_DEFAULT;desc.CPUAccessFlags=0;
        desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        check_hresult(device->CreateTexture2D(&desc,nullptr,contentTexture.put()));
        check_hresult(device->CreateRenderTargetView(contentTexture.get(),nullptr,contentTarget.put()));
        check_hresult(device->CreateShaderResourceView(contentTexture.get(),nullptr,contentView.put()));
        previewWorker=std::thread([this]{runPreview();});
    }
    ~CaptureState() {
        {std::lock_guard lock(previewMutex);previewStopping=true;}
        previewCondition.notify_one();
        if(previewWorker.joinable()) previewWorker.join();
    }
    void requestPreview(float blur=-1) {
        {std::lock_guard lock(previewMutex);previewPending=true;previewRequestedBlur=blur;}
        previewCondition.notify_one();
    }
    void runPreview() {
        for(;;) {
            float blur;
            {std::unique_lock lock(previewMutex);previewCondition.wait(lock,[&]{return previewStopping || previewPending;});
             if(previewStopping) return;previewPending=false;blur=previewRequestedBlur;}
            try {
                QImage image;uint64_t generation;
                {std::lock_guard lock(mutex);
                 if(!active || !input || (previewAttempt==previewRevision && previewAttemptBlur==blur)) continue;
                 generation=previewRevision;previewAttempt=generation;previewAttemptBlur=blur;image=copyPreview(blur);}
                // The CPU owns this image. No capture mutex, mapped GPU resource,
                // output event thread or unbounded frame queue survives this point.
#ifdef OFFICE_CAPTURE_TEST
                if(beforePreviewEncode) beforePreviewEncode();
#endif
                auto began=Clock::now();QByteArray encoded;QBuffer buffer(&encoded);buffer.open(QIODevice::WriteOnly);
                QImageWriter writer(&buffer,"PNG");writer.setCompression(12); // Qt's 0..100 scale: fast lossless zlib level 1.
                if(!writer.write(image)) throw std::runtime_error("Cannot encode the native preview");
                auto base64=encoded.toBase64().toStdString();
                auto elapsed=std::chrono::duration<double,std::milli>(Clock::now()-began).count();
                {std::lock_guard lock(previewMutex);previewBase64=std::move(base64);previewGeneration=generation;
                 previewRenderedBlur=blur;++previewEncodes;previewEncodeMs=elapsed;previewError.clear();}
                previewCondition.notify_all();
            } catch(hresult_error const& e) {std::lock_guard lock(previewMutex);previewError=to_string(e.message());previewCondition.notify_all();}
              catch(std::exception const& e) {std::lock_guard lock(previewMutex);previewError=e.what();previewCondition.notify_all();}
        }
    }
    void process(const Direct3D11CaptureFrame& frame) {
        auto access=frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        com_ptr<ID3D11Texture2D> source;
        check_hresult(access->GetInterface(__uuidof(ID3D11Texture2D),source.put_void()));
        processTexture(source.get());
    }
    QImage copyPreview(float blur=-1) {
            if(!previewOptions || previewBufferBlur!=blur) {
                std::array<float,4> values{blur<0?-1.f:blur/100,0,0,0};
                D3D11_BUFFER_DESC desc{};desc.ByteWidth=sizeof(values);desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
                D3D11_SUBRESOURCE_DATA data{};data.pSysMem=values.data();previewOptions=nullptr;
                check_hresult(device->CreateBuffer(&desc,&data,previewOptions.put()));previewBufferBlur=blur;
            }
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
            auto* draft=previewOptions.get();context->PSSetConstantBuffers(2,1,&draft);
            auto* view=inputView.get();context->PSSetShaderResources(0,1,&view);
            auto* filter=sampler.get();context->PSSetSamplers(0,1,&filter);context->Draw(3,0);
            ID3D11ShaderResourceView* none=nullptr;context->PSSetShaderResources(0,1,&none);
            context->CopyResource(preview.get(),previewTarget.get());
            D3D11_MAPPED_SUBRESOURCE mapped{};
            check_hresult(context->Map(preview.get(),0,D3D11_MAP_READ,0,&mapped));
            QImage image(static_cast<const uchar*>(mapped.pData),previewDesc.Width,previewDesc.Height,mapped.RowPitch,QImage::Format_ARGB32);
            QImage owned=image.copy();
            context->Unmap(preview.get(),0);
            if(owned.isNull()) throw std::runtime_error("Cannot copy the native preview");
            return owned;
    }
    void processTexture(ID3D11Texture2D* source) {
        auto began=Clock::now();
        D3D11_TEXTURE2D_DESC desc{}; source->GetDesc(&desc);
        bool resized=!input || sourceW!=desc.Width || sourceH!=desc.Height;
        if(resized) {
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
            desc.MipLevels=1;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;desc.MiscFlags=0;
            incoming=nullptr;incomingView=nullptr;
            check_hresult(device->CreateTexture2D(&desc,nullptr,incoming.put()));
            check_hresult(device->CreateShaderResourceView(incoming.get(),nullptr,incomingView.put()));
        }
        auto sx=[&](LONG v) { return static_cast<UINT>(std::clamp((double)(v-host.left)/(host.right-host.left)*sourceW,0.0,(double)sourceW)); };
        auto sy=[&](LONG v) { return static_cast<UINT>(std::clamp((double)(v-host.top)/(host.bottom-host.top)*sourceH,0.0,(double)sourceH)); };
        D3D11_BOX crop{sx(monitor.left),sy(monitor.top),0,sx(monitor.right),sy(monitor.bottom),1};
        context->CopySubresourceRegion(incoming.get(),0,0,0,0,source,0,&crop);
        bool changed=resized;
        if(!resized) {
            const UINT zero[4]={};context->ClearUnorderedAccessViewUint(differenceView.get(),zero);
            context->CSSetShader(compare.get(),nullptr,0);
            auto* previous=inputView.get();auto* next=incomingView.get();auto* flag=differenceView.get();
            context->CSSetShaderResources(0,1,&previous);context->CSSetShaderResources(2,1,&next);
            context->CSSetUnorderedAccessViews(0,1,&flag,nullptr);
            D3D11_TEXTURE2D_DESC size{};incoming->GetDesc(&size);
            context->Dispatch((size.Width+15)/16,(size.Height+15)/16,1);
            ID3D11ShaderResourceView* empty[3]={};ID3D11UnorderedAccessView* noFlag=nullptr;
            context->CSSetShaderResources(0,3,empty);context->CSSetUnorderedAccessViews(0,1,&noFlag,nullptr);
            context->CopyResource(differenceReadback.get(),difference.get());
            D3D11_MAPPED_SUBRESOURCE mapped{};check_hresult(context->Map(differenceReadback.get(),0,D3D11_MAP_READ,0,&mapped));
            changed=*static_cast<const UINT*>(mapped.pData)!=0;context->Unmap(differenceReadback.get(),0);
        }
        received=Clock::now();++frames;error.clear();if(frames==1) firstReceived=received;
        if(!changed) {++skippedFrames;lastProcessingMs=std::chrono::duration<double,std::milli>(received-began).count();return;}
        ++changedFrames;
        context->CopySubresourceRegion(input.get(),0,0,0,0,incoming.get(),0,nullptr);
        context->GenerateMips(inputView.get());
        sampleRetained();
        lastProcessingMs=std::chrono::duration<double,std::milli>(Clock::now()-began).count();
    }
    void sampleRetained() {
        if(!input) return;
        // Compute a single content rectangle on the GPU, shared by all LEDs.
        // There is no full-frame CPU readback or repeated border scan per LED.
        D3D11_VIEWPORT one{0,0,1,1,0,1};context->RSSetViewports(1,&one);
        auto* contentRT=contentTarget.get();context->OMSetRenderTargets(1,&contentRT,nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vertex.get(),nullptr,0);context->PSSetShader(contentPixel.get(),nullptr,0);
        auto* settings=options.get();context->PSSetConstantBuffers(1,1,&settings);
        auto* view=inputView.get();context->PSSetShaderResources(0,1,&view);
        auto* filter=sampler.get();context->PSSetSamplers(0,1,&filter);context->Draw(3,0);
        context->OMSetRenderTargets(0,nullptr,nullptr);
        auto* content=contentView.get();context->PSSetShaderResources(1,1,&content);
        D3D11_VIEWPORT viewport{0,0,static_cast<float>(colors.size()),1,0,1};
        context->RSSetViewports(1,&viewport);
        ID3D11RenderTargetView* targets[]={target.get(),boundsTarget.get()};context->OMSetRenderTargets(2,targets,nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vertex.get(),nullptr,0); context->PSSetShader(pixel.get(),nullptr,0);
        auto* constants=regions.get(); context->PSSetConstantBuffers(0,1,&constants);
        context->Draw(3,0);
        ID3D11ShaderResourceView* none[2]={}; context->PSSetShaderResources(0,2,none);
        context->CopyResource(readback.get(),output.get());
        D3D11_MAPPED_SUBRESOURCE mapped{}; check_hresult(context->Map(readback.get(),0,D3D11_MAP_READ,0,&mapped));
        auto* bytes=static_cast<const unsigned char*>(mapped.pData);
        for(size_t i=0;i<colors.size();++i) colors[i]=ToRGBColor(bytes[i*4],bytes[i*4+1],bytes[i*4+2]);
        context->Unmap(readback.get(),0);
        context->CopyResource(boundsReadback.get(),boundsOutput.get());
        check_hresult(context->Map(boundsReadback.get(),0,D3D11_MAP_READ,0,&mapped));
        std::memcpy(effectiveRegions.data(),mapped.pData,effectiveRegions.size()*sizeof(Sample));context->Unmap(boundsReadback.get(),0);
        ++previewRevision;
        ++samplingPasses;
    }
};

class Capture {
    GraphicsCaptureItem item{nullptr};
    Direct3D11CaptureFramePool pool{nullptr};
    GraphicsCaptureSession session{nullptr};
    event_token arrived{},closed{};
public:
    std::shared_ptr<CaptureState> state;
    std::shared_ptr<office::Journal> journal;
    uint64_t generation=0;
    Capture(HWND window,RECT monitor,const std::vector<Sample>& samples,std::shared_ptr<office::Journal> targetJournal={},uint64_t sourceGeneration=0):journal(std::move(targetJournal)),generation(sourceGeneration) {
        if(journal) journal->write("gpu_create_begin",{{"generation",generation}});
        RECT bounds{}; check_bool(GetWindowRect(window,&bounds));
        state=std::make_shared<CaptureState>(samples,bounds,monitor);
        if(journal) {
            auto dxgiDevice=state->device.as<IDXGIDevice>();com_ptr<IDXGIAdapter> adapter;
            auto result=dxgiDevice->GetAdapter(adapter.put());DXGI_ADAPTER_DESC desc{};LARGE_INTEGER driver{};
            if(SUCCEEDED(result))result=adapter->GetDesc(&desc);
            auto driverResult=adapter?adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice),&driver):E_NOINTERFACE;
            journal->write("gpu_create_complete",{{"generation",generation},{"adapter_query_hresult",static_cast<int32_t>(result)},
                {"adapter",to_string(desc.Description)},{"vendor_id",desc.VendorId},{"device_id",desc.DeviceId},
                {"adapter_luid_low",desc.AdapterLuid.LowPart},{"adapter_luid_high",desc.AdapterLuid.HighPart},
                {"driver_query_hresult",static_cast<int32_t>(driverResult)},{"driver_version_raw",driver.QuadPart}});
        }
        if(journal) journal->write("capture_item_create_begin",{{"generation",generation}});
        auto factory=get_activation_factory<GraphicsCaptureItem,IGraphicsCaptureItemInterop>();
        check_hresult(factory->CreateForWindow(window,guid_of<GraphicsCaptureItem>(),put_abi(item)));
        if(journal) journal->write("capture_item_create_complete",{{"generation",generation}});
        auto dxgi=state->device.as<IDXGIDevice>(); com_ptr<IInspectable> wrapper;
        check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.get(),wrapper.put()));
        auto winDevice=wrapper.as<IDirect3DDevice>();
        try {
        if(journal)journal->write("capture_pool_create_begin",{{"generation",generation}});
        pool=Direct3D11CaptureFramePool::CreateFreeThreaded(winDevice,DirectXPixelFormat::B8G8R8A8UIntNormalized,2,item.Size());
        session=pool.CreateCaptureSession(item); session.IsCursorCaptureEnabled(false);
        if(journal)journal->write("capture_pool_create_complete",{{"generation",generation}});
        try { session.IsBorderRequired(false); } catch(hresult_error const&) {}
        auto owned=state;
        auto log=journal;auto gen=generation;
        closed=item.Closed([owned,log,gen](auto const&,auto const&) { if(log)log->write("capture_item_closed",{{"generation",gen}});std::lock_guard lock(owned->mutex); owned->closed=true; });
        auto size=item.Size();
        arrived=pool.FrameArrived([owned,winDevice,size,log,gen](auto const& sender,auto const&) mutable {
            std::lock_guard lock(owned->mutex);
            if(!owned->active) return;
            try {
                auto frame=sender.TryGetNextFrame(); if(!frame) return;
                auto content=frame.ContentSize();
                auto callbackBegan=Clock::now();
                if(content.Width==size.Width && content.Height==size.Height) {
                    bool first=owned->frames==0;owned->process(frame);owned->received=callbackBegan;
                    if(first && log)log->write("capture_first_frame",{{"generation",gen},{"width",content.Width},{"height",content.Height}});
                }
                frame.Close();
                if(content.Width>0 && content.Height>0 && (content.Width!=size.Width || content.Height!=size.Height)) {
                    if(log)log->write("capture_resize_begin",{{"generation",gen},{"width",content.Width},{"height",content.Height}});
                    size=content;
                    sender.Recreate(winDevice,DirectXPixelFormat::B8G8R8A8UIntNormalized,2,size);
                    if(log)log->write("capture_resize_complete",{{"generation",gen}});
                }
            }
            catch(hresult_error const& error) {
                auto message=to_string(error.message());auto removed=owned->device->GetDeviceRemovedReason();
                if(log && owned->error!=message)log->write("capture_error",{{"generation",gen},{"hresult",static_cast<int32_t>(error.code())},{"device_removed_reason",static_cast<int32_t>(removed)},{"message",message}});
                owned->error=message;if(FAILED(removed)) owned->closed=true;
            }
            catch(std::exception const& error) {if(log && owned->error!=error.what())log->write("capture_error",{{"generation",gen},{"message",error.what()}});owned->error=error.what(); }
        });
        if(journal)journal->write("capture_start_begin",{{"generation",generation}});
        session.StartCapture();
        if(journal)journal->write("capture_start_complete",{{"generation",generation}});
        } catch(...) {stop();throw;}
    }
    void stop() noexcept {
        if(journal)journal->write("capture_stop_begin",{{"generation",generation}});
        { std::lock_guard lock(state->mutex); state->active=false; }
        auto step=[&](const char* name,auto action) {
            if(journal)journal->write("capture_release_begin",{{"generation",generation},{"operation",name}});
            try {action();if(journal)journal->write("capture_release_complete",{{"generation",generation},{"operation",name}});}
            catch(hresult_error const& e) {if(journal)journal->write("capture_release_error",{{"generation",generation},{"operation",name},{"hresult",static_cast<int32_t>(e.code())}});}
            catch(...) {if(journal)journal->write("capture_release_error",{{"generation",generation},{"operation",name}});}
        };
        step("frame_unsubscribe",[&]{if(pool && arrived.value)pool.FrameArrived(arrived);});
        step("item_unsubscribe",[&]{if(item && closed.value)item.Closed(closed);});
        step("session_close",[&]{if(session)session.Close();});
        step("pool_close",[&]{if(pool)pool.Close();});
        if(journal)journal->write("capture_stop_complete",{{"generation",generation}});
    }
    ~Capture() {stop();}
};
} // namespace

struct OfficeAmbience::Impl {
    std::shared_ptr<office::Journal> journal;
    const std::string previewEpoch=QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    QTimer output;
    OpenRGBPluginAPIInterface* api;
    std::filesystem::path configPath,statusPath;
    Monitor selectedMonitor;
    office::DisplayBrightness displayBrightness;
    std::unique_ptr<office::OfficeScreen> screen;
    office::OfficeAwake awake;
    office::OfficeRest rest;
    bool observeNativeInput=true;
    bool observeNativeSources=true,capturePaused=false;
    bool observeWallpaper=true,wallpaperImageRequested=true,wallpaperImageActive=false,wallpaperImageSubmitted=false;
    std::string wallpaperImageError;
    double wallpaperImageLoadMs=0;
    Clock::time_point wallpaperImageBegan{};
    Clock::time_point lastBrightnessRead{};
    std::vector<Device> devices;
    std::unique_ptr<office::HardwareState> hardware;
    std::map<std::string,RGBControllerInterface*> nativeControllers;
    bool detectionActive=false,outputPaused=false;
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
    Clock::time_point lastHealth=Clock::now();
    std::string loggedRest,loggedDesktop,statusWriteError;
    uint64_t outputFrames=0,inputFrames=0;
    uint64_t changedInputFrames=0,skippedInputFrames=0,samplingPasses=0,deviceSubmissions=0,ledWrites=0;
    double smoothingMs=140,processingMs=0,firstFrameDelayMs=0;
    double callbackToSubmitMs=0,submitMs=0;
    uint64_t deliveredFrames=0,revision=1,sourceGeneration=0;
    double measuredFps=0;
    uint64_t previousStatusOutput=0;
    Json config,layout;
    std::unique_ptr<OfficeControlServer> control;
    std::string identifyDevice;
    bool calibration=false;
    bool sessionLocked=false,displayOff=false,standbyPreview=false,powerSuspended=false;
    bool desktopUnavailable=false,monitorMissing=false,automaticSaver=false;
    std::string inputDesktop;
    Clock::time_point standbyBegan=Clock::now();
    HPOWERNOTIFY displayNotification{};
    static constexpr GUID sessionDisplayState{0x2b84c20e,0xad23,0x4ddf,{0x93,0xdb,0x05,0xff,0xbd,0x7e,0xfc,0xa5}};
    Clock::time_point identifyUntil{};
    HWND eventWindow{};
    bool initialized=false;
    std::atomic_bool dirty{true};
    std::atomic_bool invalidate{false};
    HWINEVENTHOOK systemHook{},objectHook{},desktopHook{};
    static inline std::map<HWINEVENTHOOK,Impl*> hooks;
    Apartment apartment;
    DpiContext dpi;

    static LRESULT CALLBACK nativeEvent(HWND window,UINT message,WPARAM w,LPARAM l) {
        auto self=reinterpret_cast<Impl*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if(message==WM_NCCREATE) {
            self=static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
            SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
        }
        if(self && self->screen)try {self->screen->nativeEvent(message,w,l);}catch(const std::exception& e){self->journal->write("screen_event_error",{{"message",e.what()}});}
        if(self && message==WM_ENDSESSION && w)self->awake.reset();
        if(self && message==WM_WTSSESSION_CHANGE) {
            self->journal->write("session_event",{{"notification",w},{"session_id",l}});
            if(w==WTS_SESSION_LOCK) {self->sessionLocked=true;self->standbyBegan=Clock::now();self->awake.reset();}
            if(w==WTS_SESSION_UNLOCK) {self->sessionLocked=false;self->desktopUnavailable=false;}
        }
        if(self && message==WM_POWERBROADCAST){
            if(w==PBT_APMSUSPEND){self->powerSuspended=true;self->awake.reset();}
            if(w==PBT_APMRESUMEAUTOMATIC||w==PBT_APMRESUMESUSPEND||w==PBT_APMPOWERSTATUSCHANGE){self->powerSuspended=false;self->awake.reset();self->dirty=true;}
        }
        if(self && message==WM_POWERBROADCAST && w==PBT_POWERSETTINGCHANGE) {
            auto* setting=reinterpret_cast<POWERBROADCAST_SETTING*>(l);
            if(setting && setting->PowerSetting==sessionDisplayState && setting->DataLength==sizeof(DWORD)) {
                DWORD state{};std::memcpy(&state,setting->Data,sizeof(state));
                self->journal->write("display_power",{{"state",state}});
                if(state==0 && !self->displayOff) self->standbyBegan=Clock::now();
                self->displayOff=state==0;
                if(self->displayOff)self->awake.reset();
                if(!self->displayOff) {self->invalidate=true;self->dirty=true;}
            }
        }
        if(self && (message==WM_DISPLAYCHANGE || (message==WM_POWERBROADCAST && w==PBT_APMRESUMEAUTOMATIC) ||
                    (message==WM_WTSSESSION_CHANGE && (w==WTS_SESSION_UNLOCK || w==WTS_CONSOLE_CONNECT || w==WTS_REMOTE_CONNECT)))) {
            self->journal->write("capture_invalidated",{{"message",message},{"notification",w}});self->invalidate=true;self->dirty=true;}
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
            if(value.contains("rotation_quarters")) {
                const auto& turns=value.at("rotation_quarters");
                if(!turns.is_array() || turns.size()!=devices[i].count)throw std::runtime_error("Orientation must retain every physical LED");
                for(const auto& turn:turns)if(!turn.is_number_integer() || turn<0 || turn>3)throw std::runtime_error("Orientation must be a quarter turn from 0 to 3");
            }
            if(value.at("groups").size()!=expected.at("groups").size()) throw std::runtime_error("Physical zone grouping changed");
            for(size_t g=0;g<value.at("groups").size();++g) {
                const auto& group=value["groups"][g];const auto& baseline=expected["groups"][g];
                if(group.contains("enabled") && !group.at("enabled").is_boolean()) throw std::runtime_error("Zone enabled state must be a boolean");
                for(auto key:{"id","first","count"}) if(group.at(key)!=baseline.at(key)) throw std::runtime_error("Hardware LED identities cannot be renumbered by editing the map");
                auto name=group.at("name").get<std::string>();if(name.empty() || name.size()>120) throw std::runtime_error("Zone names need 1-120 UTF-8 bytes");
            }
            result.insert(result.end(),points.begin(),points.end());
        }
        return result;
    }
    void validateSettings(const Json& candidate) const {
        if(candidate.at("monitor")!="primary") throw std::runtime_error("Capture always follows the Windows main display");
        if(!candidate.at("enabled").is_boolean() || !candidate.at("fps").is_number_integer() || candidate.at("fps")<1 || candidate.at("fps")>60)
            throw std::runtime_error("Output FPS must be an integer from 1 to 60");
        double smooth=candidate.at("smoothing_ms").get<double>();
        if(!std::isfinite(smooth) || smooth<0 || smooth>2000) throw std::runtime_error("Smoothing must be 0-2000 milliseconds");
        office::validateProcessing(candidate);
    }
    Json snapshot(bool preview=false,const std::string& knownPreview={},float previewBlur=-1) {
        if(preview && capture) capture->state->requestPreview(previewBlur);
        auto colours=Json::array();
        for(auto& d:devices) {auto row=Json::array();for(auto c:d.colors) row.push_back(Json::array({RGBGetRValue(c),RGBGetGValue(c),RGBGetBValue(c)}));colours.push_back(row);}
        std::string image,previewError,previewKey;Json effective=Json::array();uint64_t encodes=0;double encodeMs=0;
        if(capture) {std::lock_guard lock(capture->state->mutex);
            for(auto& d:devices) effective.push_back(office::encode(std::vector<Sample>(capture->state->effectiveRegions.begin()+d.start,capture->state->effectiveRegions.begin()+d.start+d.count)));}
        if(capture) {std::lock_guard lock(capture->state->previewMutex);auto& state=*capture->state;
            previewKey=previewEpoch+":"+std::to_string(sourceGeneration)+":"+std::to_string(state.previewGeneration)+":"+std::to_string(state.previewRenderedBlur);
            if(preview && state.previewRenderedBlur==previewBlur && knownPreview!=previewKey) image=state.previewBase64;
            previewError=state.previewError;encodes=state.previewEncodes;encodeMs=state.previewEncodeMs;}
        if(preview&&capturePaused)previewError="Lighting is disabled; live capture is stopped";
        auto online=Json::array();{std::lock_guard lock(controllersMutex);for(auto& d:devices) online.push_back(d.controller!=nullptr);}
        Json settings={{"monitor",config.at("monitor")},{"enabled",config.at("enabled")},{"fps",config.at("fps")},{"smoothing_ms",config.at("smoothing_ms")}};
        const auto defaults=office::processingDefaults();for(auto& [key,value]:defaults.items()) settings[key]=config.at(key);
        return {{"revision",revision},{"online",online},{"settings",settings},
                {"layout",layout},{"default_layout",factoryLayout},{"status",report()},{"screen",screen->snapshot()},{"colors",colours},{"effective_regions",effective},{"preview_png",image},{"preview_generation",previewKey},{"preview_blur",previewBlur},{"preview_encodes",encodes},{"preview_encode_ms",encodeMs},{"preview_error",previewError},
                {"test_mode",standbyPreview?"standby":calibration?"calibration":identifyDevice.empty()?"none":"identify"}};
    }
    Json command(const Json& request) {
        auto name=request.at("command").get<std::string>();
        if(name=="snapshot") {
            float blur=-1;
            if(request.contains("preview_blur")) {
                if(!request["preview_blur"].is_number()) throw std::runtime_error("Preview blur must be numeric");
                double value=request["preview_blur"];
                if(!std::isfinite(value) || value<0 || value>10) throw std::runtime_error("Preview blur must be 0-10 percent");
                blur=static_cast<float>(value);
            }
            auto result=snapshot(request.value("preview",false),request.value("preview_generation",std::string()),blur);
            if(request.value("hardware",false))result["hardware"]=hardwareSnapshot();
            return result;
        }
        journal->write("control_action",{{"command",name}});
        if(name=="screen")return screen->command(request);
        if(name=="awake")return awake.command(request);
        if(name=="recover") {outputPaused=false;invalidate=true;dirty=true;return {{"accepted",true}};}
        if(name=="pause_output") {screen->command({{"action","pause"},{"target","lighting"},{"mode",screen->lightingPaused()?"enable":"forever"}});return {{"paused",screen->lightingPaused()}};}
        if(name=="hardware")return hardwareCommand(request);
        if(name=="stop_test") {calibration=false;standbyPreview=false;identifyDevice.clear();return {{"accepted",true}};}
        if(name=="preview_standby") {calibration=false;identifyDevice.clear();standbyPreview=true;standbyBegan=Clock::now();return {{"accepted",true}};}
        if(name=="identify_all") {standbyPreview=false;calibration=false;identifyDevice="*";identifyUntil=Clock::now()+std::chrono::seconds(2);return {{"accepted",true}};}
        if(name=="identify") {
            auto key=request.at("device").get<std::string>();
            if(std::none_of(devices.begin(),devices.end(),[&](const auto& d){return d.key==key;})) throw std::runtime_error("Unknown physical device");
            standbyPreview=false;calibration=false;identifyDevice=key;
            // User-visible identify duration; never a capture/recovery deadline.
            identifyUntil=Clock::now()+std::chrono::seconds(2);return {{"accepted",true}};
        }
        if(name=="calibrate") {standbyPreview=false;identifyDevice.clear();calibration=true;return {{"accepted",true}};}
        if(name!="save") throw std::runtime_error("Unknown control command");
        if(request.at("revision").get<uint64_t>()!=revision) throw std::runtime_error("Settings changed in another panel. Reload before saving");
        auto next=config;
        for(auto key:{"monitor","enabled","fps","smoothing_ms"}) next[key]=request.at("settings").at(key);
        const auto defaults=office::processingDefaults();for(auto& [key,value]:defaults.items()) if(request.at("settings").contains(key)) next[key]=request.at("settings").at(key);
        validateSettings(next);
        auto proposed=request.at("layout");auto points=validatedLayout(proposed);next["layout"]=proposed;
        // The runtime resolves aliases and missing defaults without migrating
        // the user's file. A topology Save must preserve those untouched keys.
        auto document=diskConfig;
        for(auto& [key,value]:request.at("settings").items())
            if(next.contains(key) && config.contains(key) && next.at(key)!=config.at(key)) document[key]=next.at(key);
        document["layout"]=proposed;
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
        auto bytes=document.dump(2);
        if(!file.open(QIODevice::WriteOnly) || file.write(bytes.data(),bytes.size())!=static_cast<qint64>(bytes.size()) || !file.commit())
            throw std::runtime_error("Could not save settings; the active map is unchanged");
        if(capture) {capture->state->regions=std::move(newRegions);capture->state->options=std::move(newOptions);capture->state->sampleRetained();}
        samples=std::move(points);layout=std::move(proposed);config=next;diskConfig=document;smoothingMs=config["smoothing_ms"].get<double>();
        output.setInterval(std::max(1,static_cast<int>(std::round(1000.0/config["fps"].get<int>()))));
        ++revision;
        if(lock.owns_lock()) lock.unlock();
        journal->write("settings_applied",{{"revision",revision},{"fps",config["fps"]}});
        return snapshot();
    }
    Json diskConfig;
    Json factoryLayout;

    std::string nativeKey(RGBControllerInterface* c) {
        auto key=deviceKey(c);return key.empty()?"native-"+office::HardwareState::identity(c):key;
    }
    void bindHardware(const std::vector<RGBControllerInterface*>& current) {
        auto previous=std::move(nativeControllers);nativeControllers.clear();
        for(auto* c:current) {auto key=nativeKey(c);if(nativeControllers.contains(key))throw std::runtime_error("Ambiguous native device identity");nativeControllers[key]=c;if(!previous.contains(key) || previous.at(key)!=c)hardware->reconnected(key);}
    }
    Json hardwareSnapshot() {
        std::lock_guard lock(controllersMutex);auto rows=Json::array();
        for(auto& [key,c]:nativeControllers)rows.push_back(hardware->describe(key,c,std::any_of(devices.begin(),devices.end(),[&](auto& d){return d.key==key;})));
        return {{"devices",rows},{"profiles",api->GetProfileList()},{"detecting",detectionActive},{"detection_percent",api->GetDetectionPercent()},{"detection_status",api->GetDetectionString()}};
    }
    Json hardwareCommand(const Json& request) {
        auto action=request.at("action").get<std::string>();
        if(action=="rescan") {bool detecting;{std::lock_guard lock(controllersMutex);detecting=detectionActive;}if(!detecting)api->RescanDevices();return {{"accepted",true}};}
        if(action=="profile_save" || action=="profile_load") {
            auto name=request.at("name").get<std::string>();
            if(name.empty() || name.size()>100 || name.find_first_of("\\/:*?\"<>|.")!=std::string::npos)throw std::runtime_error("Use a profile name without path characters or dots");
            {
            std::lock_guard lock(controllersMutex);if(detectionActive)throw std::runtime_error("Wait for discovery before using profiles");
            if(!restReason().empty() || outputPaused || screen->lightingPaused() || calibration || !identifyDevice.empty())throw std::runtime_error("End temporary tests and resume lighting before using native profiles");
            if(action=="profile_save") {if(!api->SaveProfile(name))throw std::runtime_error("Native profile save failed");}
            else {
                auto profiles=api->GetProfileList();if(std::find(profiles.begin(),profiles.end(),name)==profiles.end())throw std::runtime_error("Native profile no longer exists");
                if(!api->LoadProfile(name))throw std::runtime_error("Native profile load failed");
                for(auto& [key,c]:nativeControllers) {
                    if(c->SupportsPerZoneModes()) {for(unsigned z=0;z<c->GetZoneCount();++z)if(c->GetZoneActiveMode(z)>=0 && unsigned(c->GetZoneActiveMode(z))<c->GetZoneModeCount(z))hardware->setMode(key,c,office::HardwareState::identity(c),office::HardwareState::mode(c,c->GetZoneActiveMode(z),z));}
                    else if(c->GetActiveMode()>=0 && unsigned(c->GetActiveMode())<c->GetModeCount()) {auto m=office::HardwareState::mode(c,c->GetActiveMode());if(m.at("color_mode")==MODE_COLORS_PER_LED){m["led_colors"]=Json::array();for(unsigned i=0;i<c->GetLEDCount();++i)m["led_colors"].push_back(c->GetColor(i));}hardware->setMode(key,c,office::HardwareState::identity(c),m);}
                }
            }
            }
            return hardwareSnapshot();
        }
        std::lock_guard lock(controllersMutex);
        auto key=request.at("device").get<std::string>();auto found=nativeControllers.find(key);
        if(found==nativeControllers.end() || detectionActive)throw std::runtime_error("Device is offline or discovery is in progress");
        auto* c=found->second;hardware->checkIdentity(c,request.at("identity"));
        if(!restReason().empty() || outputPaused || screen->lightingPaused() || calibration || !identifyDevice.empty())throw std::runtime_error("End temporary tests and resume lighting before changing hardware settings");
        if(action=="ambience") {hardware->ambience(key,c);for(auto& d:devices)if(d.key==key)d.delivered=false;}
        else if(action=="mode")hardware->setMode(key,c,request.at("identity"),request.at("mode"));
        else if(action=="save_mode") {
            int m=c->GetActiveMode();if(!hardware->hardware(key) || outputPaused || calibration || !identifyDevice.empty() || m<0 || request.at("mode")!=m || request.at("name")!=c->GetModeName(m) || !(c->GetModeFlags(m)&MODE_FLAG_MANUAL_SAVE))throw std::runtime_error("Apply this native mode before saving it to device memory");c->SaveMode();
        } else if(action=="resize") {
            int z=request.at("zone");if(z<0 || unsigned(z)>=c->GetZoneCount() || !(c->GetZoneFlags(z)&(ZONE_FLAG_MANUALLY_CONFIGURABLE_SIZE|ZONE_FLAG_MANUALLY_CONFIGURABLE_SIZE_EFFECTS_ONLY)))throw std::runtime_error("Hardware zone is not resizable");
            if(std::any_of(devices.begin(),devices.end(),[&](auto& d){return d.key==key;}))throw std::runtime_error("This hardware geometry is bound to a saved ambience map; edit its sampling areas in Topology");
            auto count=request.at("count");if(!count.is_number_integer() || count<c->GetZoneLEDsMin(z) || count>c->GetZoneLEDsMax(z))throw std::runtime_error("Invalid native zone size");c->ResizeZone(z,count);
        } else if(action=="configuration") {
            int z=request.value("zone",-1);if(z<-1 || (z>=0 && unsigned(z)>=c->GetZoneCount()))throw std::runtime_error("Invalid hardware zone");
            // Geometry-affecting driver options cannot invalidate existing maps.
            if(std::any_of(devices.begin(),devices.end(),[&](auto& d){return d.key==key;}))throw std::runtime_error("Mapped hardware configuration is fixed; sampling remains editable in Topology");
            auto schema=z<0?c->GetDeviceSpecificConfigurationSchema():c->GetDeviceSpecificZoneConfigurationSchema(z);
            if(schema.is_null() || schema.empty())throw std::runtime_error("Driver supplies no configuration schema");
            auto value=request.at("configuration");office::validateHardwareConfiguration(schema,value);
            if(z<0)c->SetDeviceSpecificConfiguration(value);else c->SetDeviceSpecificZoneConfiguration(z,value);api->SaveSettings();
        } else throw std::runtime_error("Unknown hardware action");
        return hardware->describe(key,c,std::any_of(devices.begin(),devices.end(),[&](auto& d){return d.key==key;}));
    }

    static void CALLBACK winEvent(HWINEVENTHOOK hook,DWORD event,HWND window,LONG object,LONG,DWORD,DWORD) {
        if(event!=EVENT_SYSTEM_DESKTOPSWITCH && object!=OBJID_WINDOW) return;
        auto found=hooks.find(hook); if(found!=hooks.end()) {
            if(found->second->screen && (event==EVENT_SYSTEM_FOREGROUND||event==EVENT_SYSTEM_DESKTOPSWITCH||(event==EVENT_OBJECT_LOCATIONCHANGE&&window==GetForegroundWindow())))found->second->screen->foregroundChanged(event==EVENT_SYSTEM_FOREGROUND||event==EVENT_SYSTEM_DESKTOPSWITCH);
            if(event==EVENT_SYSTEM_DESKTOPSWITCH) found->second->journal->write("desktop_switch");
            found->second->dirty=true;
            if(event==EVENT_SYSTEM_DESKTOPSWITCH) found->second->invalidate=true;
        }
    }
    // Test construction skips OS event/IPC activation and automatic native
    // source observation, retaining inventory/map/output and explicit source
    // selection. Output fixtures supply their own fresh image rows.
    Impl(OpenRGBPluginAPIInterface* api,const std::filesystem::path& path,bool activate=true):journal(std::make_shared<office::Journal>(path.parent_path()/"logs"/"office-ambience-events.jsonl")),api(api),configPath(path),awake(path.parent_path(),activate),rest(path.parent_path(),activate),observeNativeInput(activate),observeNativeSources(activate),observeWallpaper(activate) {
        journal->write("owner_start",{{"executable",QCoreApplication::applicationFilePath().toStdString()}});
        std::ifstream file(path);file>>config;diskConfig=config;
        screen=std::make_unique<office::OfficeScreen>(path.parent_path(),activate);
        hardware=std::make_unique<office::HardwareState>(QString::fromStdWString((path.parent_path()/"office-hardware.json").wstring()));
        bindHardware(api->GetRGBControllers());
        // Legacy display names are accepted on disk but never pin capture to
        // disconnected hardware. Do not rewrite the saved map merely on load.
        config["monitor"]="primary";
        if(!config.contains("fps")) config["fps"]=15;
        if(!config.contains("smoothing_ms")) config["smoothing_ms"]=140;
        if(!config.contains("enabled")) config["enabled"]=true;
        office::processingDefaults(config);
        validateSettings(config);
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
        factoryLayout=layout;
        if(config.contains("layout")) {samples=validatedLayout(config["layout"]);layout=config["layout"];}
        for(auto& device:devices) if(device.controller) device.controller->SetCustomMode();
        if(!activate) return;
        try {
        systemHook=SetWinEventHook(EVENT_SYSTEM_FOREGROUND,EVENT_SYSTEM_MINIMIZEEND,nullptr,winEvent,0,0,WINEVENT_OUTOFCONTEXT);
        objectHook=SetWinEventHook(EVENT_OBJECT_CREATE,EVENT_OBJECT_LOCATIONCHANGE,nullptr,winEvent,0,0,WINEVENT_OUTOFCONTEXT);
        desktopHook=SetWinEventHook(EVENT_SYSTEM_DESKTOPSWITCH,EVENT_SYSTEM_DESKTOPSWITCH,nullptr,winEvent,0,0,WINEVENT_OUTOFCONTEXT);
        if(!systemHook || !objectHook || !desktopHook) {
            throw std::runtime_error("Window-state event registration failed");
        }
        hooks[systemHook]=this; hooks[objectHook]=this;hooks[desktopHook]=this;
        WNDCLASSW cls{};cls.lpfnWndProc=nativeEvent;cls.lpszClassName=L"OpenRGBOfficeCaptureEvents";cls.hInstance=GetModuleHandleW(nullptr);
        if(!RegisterClassW(&cls) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) throw std::runtime_error("Cannot register capture lifecycle events");
        eventWindow=CreateWindowW(cls.lpszClassName,L"",0,0,0,0,0,nullptr,nullptr,cls.hInstance,this);
        if(!eventWindow || !WTSRegisterSessionNotification(eventWindow,NOTIFY_FOR_THIS_SESSION)) throw std::runtime_error("Cannot register session capture events");
        screen->attach(eventWindow,[]{auto pid=GetCurrentProcessId();auto name=L"Local\\OpenRGB.Office.Show."+std::to_wstring(pid);HANDLE event=OpenEventW(EVENT_MODIFY_STATE,FALSE,name.c_str());if(event){SetEvent(event);CloseHandle(event);}});
        displayNotification=RegisterPowerSettingNotification(eventWindow,&sessionDisplayState,DEVICE_NOTIFY_WINDOW_HANDLE);
        if(!displayNotification) throw std::runtime_error("Cannot register session display power events");
        // WTS notifications cover subsequent transitions; query the initial
        // state too so launching while already locked never emits saved colours.
        LPWSTR sessionInfo{};DWORD sessionBytes{};
        if(WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE,WTS_CURRENT_SESSION,WTSSessionInfoEx,&sessionInfo,&sessionBytes)) {
            auto info=reinterpret_cast<WTSINFOEXW*>(sessionInfo);
            if(sessionBytes>=sizeof(WTSINFOEXW) && info->Level==1)
                sessionLocked=info->Data.WTSInfoExLevel1.SessionFlags==WTS_SESSIONSTATE_LOCK;
            WTSFreeMemory(sessionInfo);
        }
        control=std::make_unique<OfficeControlServer>(QString::fromStdWString(path.parent_path().wstring()),[this](const Json& request){return command(request);});
        QObject::connect(&output,&QTimer::timeout,[this] { tick(); });
        output.setTimerType(Qt::PreciseTimer); output.start(std::max(1,static_cast<int>(std::round(1000.0/fps))));
        status();
        } catch(...) {control.reset();releaseEvents();throw;}
    }
    void releaseEvents() noexcept {
        awake.reset();
        if(displayNotification) {UnregisterPowerSettingNotification(displayNotification);displayNotification=nullptr;}
        if(eventWindow) {WTSUnRegisterSessionNotification(eventWindow);DestroyWindow(eventWindow);eventWindow=nullptr;}
        UnregisterClassW(L"OpenRGBOfficeCaptureEvents",GetModuleHandleW(nullptr));
        for(auto hook:{systemHook,objectHook,desktopHook}) if(hook) { hooks.erase(hook); UnhookWinEvent(hook); }
        systemHook=objectHook=desktopHook=nullptr;
    }
    ~Impl() {
        journal->write("owner_stop_begin");
        control.reset();
        output.stop();
        screen->shutdown();
        releaseEvents();
        capture.reset();
        mode="stopped"; status();
        journal->write("owner_stop_complete");
    }
    void resourcesUpdated(unsigned reason) {
        // OpenRGB emits these synchronously while removed controllers are still
        // alive. This barrier must not be queued onto the Qt output thread:
        // removal may free a controller immediately after this callback returns.
        std::lock_guard lock(controllersMutex);
        if(reason==RESOURCEMANAGER_UPDATE_REASON_DETECTION_STARTED) {
            journal->write("detection_started");
            for(auto& d:devices) d.controller=nullptr;
            detectionActive=true;nativeControllers.clear();
            return;
        }
        if(reason!=RESOURCEMANAGER_UPDATE_REASON_DEVICE_LIST_UPDATED && reason!=RESOURCEMANAGER_UPDATE_REASON_DETECTION_COMPLETE) return;
        auto current=api->GetRGBControllers();controllerError.clear();
        bindHardware(current);detectionActive=reason!=RESOURCEMANAGER_UPDATE_REASON_DETECTION_COMPLETE;
        for(auto& d:devices) {
            RGBControllerInterface* found=nullptr;
            for(auto* c:current) if(deviceKey(c)==d.key) {
                if(found || c->GetLEDCount()!=d.count) {controllerError="Reconnected controller identity/count differs: "+d.key;found=nullptr;break;}
                found=c;
            }
            if(found!=d.controller)journal->write("device_availability",{{"device",d.key},{"online",found!=nullptr}});
            if(found && found!=d.controller) {found->SetCustomMode();d.delivered=false;}
            d.controller=found;
        }
    }
    static std::optional<uint32_t> lastInput(){
        LASTINPUTINFO value{sizeof(value)};
        return GetLastInputInfo(&value)?std::optional<uint32_t>(value.dwTime):std::nullopt;
    }
    void observeRest(bool saver,bool normal,std::optional<uint32_t> input){
        const auto before=rest.status();rest.observe(saver,normal,input);const auto after=rest.status();
        if((before.at("held")==true||after.at("held")==true)&&before!=after)journal->write("rest_resume_gate",after);
        if(before.at("held")==true&&after.at("held")==false){latest.clear();invalidate=true;dirty=true;}
    }
    void chooseSource() {
        chooseSource(primaryMonitor());
    }
    bool captureSuppressed() const {return outputPaused||screen->lightingPaused()||!config.at("enabled").get<bool>();}
    void releaseCaptureInput() {
        capture.reset();source=nullptr;latest.clear();received={};error.clear();
        inputFrames=deliveredFrames=changedInputFrames=skippedInputFrames=samplingPasses=0;
        processingMs=firstFrameDelayMs=0;
        wallpaperImageActive=false;wallpaperImageSubmitted=false;wallpaperImageRequested=true;
    }
    void primeWallpaperImage(const QImage& image,const Monitor& monitor,D3D_DRIVER_TYPE driver=D3D_DRIVER_TYPE_HARDWARE) {
        // Feed a newly read wallpaper through exactly the live GPU sampler;
        // neither pre-rest images nor a second colour-processing path survive.
        auto pixels=image.convertToFormat(QImage::Format_ARGB32);
        if(pixels.isNull())throw std::runtime_error("Windows wallpaper image is unavailable");
        CaptureState state(samples,monitor.bounds,monitor.bounds,driver);state.configure(config);
        D3D11_TEXTURE2D_DESC desc{};desc.Width=pixels.width();desc.Height=pixels.height();
        desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;desc.Usage=D3D11_USAGE_DEFAULT;
        D3D11_SUBRESOURCE_DATA data{pixels.constBits(),static_cast<UINT>(pixels.bytesPerLine()),0};
        com_ptr<ID3D11Texture2D> texture;check_hresult(state.device->CreateTexture2D(&desc,&data,texture.put()));
        state.processTexture(texture.get());latest=state.colors;
        wallpaperImageActive=true;wallpaperImageSubmitted=false;wallpaperImageRequested=false;wallpaperImageError.clear();
    }
    void chooseSource(const Monitor& monitor) {
        selectedMonitor=monitor;
        monitorMissing=!monitor.handle;
        InputDesktop desktop;inputDesktop=to_string(desktop.name);desktopUnavailable=desktop.secure();
        // Windows' idle saver is a rest transition, not a window whose WGC
        // availability may leave the previous wallpaper colours illuminated.
        // Shortcut-started .scr windows do not set this state and remain sources.
        automaticSaver=desktop.screensaver();
        observeRest(automaticSaver,!desktopUnavailable&&!sessionLocked&&!displayOff&&!powerSuspended&&!monitorMissing,
                    observeNativeInput?lastInput():std::nullopt);
        if(inputDesktop!=loggedDesktop) {journal->write("input_desktop",{{"desktop",inputDesktop},{"secure",desktopUnavailable},{"automatic_saver",automaticSaver}});loggedDesktop=inputDesktop;}
        if(monitorMissing || desktopUnavailable || automaticSaver || rest.active() || sessionLocked || displayOff || powerSuspended) {awake.setContext(false,!monitorMissing&&!desktopUnavailable&&!automaticSaver&&!rest.active()&&!sessionLocked&&!powerSuspended,!displayOff);return;}
        // After checking the input desktop's security/rest state, inspect the
        // same normal desktop used by EnumWindows for application selection.
        HWND candidate=screensaverWindow(GetThreadDesktop(GetCurrentThreadId()),monitor,false);
        bool saver=candidate!=nullptr;
        if(!saver) candidate=maximizedApp(monitor);
        const auto previousAwake=awake.status();awake.setContext(!saver&&candidate!=nullptr,!saver);
        if(previousAwake!=awake.status())journal->write("awake_transition",awake.status());
        std::string selected=saver?"screensaver":candidate?"app":"wallpaper";
        // Awake's application policy and screen adjustment do not need image
        // capture. Keep those independent while RGB is disabled, without GPU
        // allocation, wallpaper-file reads, or a hidden preview capture.
        if(captureSuppressed()) {
            capturePaused=true;
            if(capture||source||!latest.empty())releaseCaptureInput();
            mode=selected;return;
        }
        if(observeWallpaper&&wallpaperImageRequested&&!saver) {
            wallpaperImageRequested=false;wallpaperImageBegan=Clock::now();
            try {
                primeWallpaperImage(office::desktopWallpaperImage(monitor.bounds),monitor);
                wallpaperImageLoadMs=std::chrono::duration<double,std::milli>(Clock::now()-wallpaperImageBegan).count();
                mode=selected;dirty=true;
                journal->write("wallpaper_image_ready",{{"load_ms",wallpaperImageLoadMs},{"display",monitor.name}});
                // Submit this fresh image on this output tick before potentially
                // blocking Windows capture setup on the following tick.
                return;
            } catch(hresult_error const& failure) {wallpaperImageError="Windows wallpaper query failed (HRESULT "+std::to_string(static_cast<int32_t>(failure.code()))+")";}
              catch(std::exception const& failure) {wallpaperImageError=failure.what();}
            journal->write("wallpaper_image_error",{{"message",wallpaperImageError}});
        }
        if(!candidate && !saver) candidate=wallpaperHost(monitor);
        // Source readiness has no guaranteed WinEvent (for example a renderer
        // returning after unlock). Keep observing on the existing output tick.
        if(!candidate) {selected="waiting";dirty=true;}
        RECT bounds{}; if(candidate) GetWindowRect(candidate,&bounds);
        bool force=invalidate.exchange(false);
        if(!force && candidate==source && mode==selected && (!candidate || capture) && EqualRect(&bounds,&sourceBounds) && EqualRect(&monitor.bounds,&monitorBounds)) return;
        DWORD sourcePid{};if(candidate)GetWindowThreadProcessId(candidate,&sourcePid);
        journal->write("source_switch_begin",{{"old_generation",sourceGeneration},{"old_hwnd",reinterpret_cast<uintptr_t>(source)},{"new_hwnd",reinterpret_cast<uintptr_t>(candidate)},{"source_pid",sourcePid},{"source_process",to_string(ownerName(candidate))},{"source_class",to_string(windowClass(candidate))},{"mode",selected},{"forced",force},{"display",monitor.name},{"bounds",{monitor.bounds.left,monitor.bounds.top,monitor.bounds.right,monitor.bounds.bottom}}});
        capture.reset(); source=candidate; mode=candidate?selected:"waiting";
        ++sourceGeneration;
        sourceBounds=bounds; monitorBounds=monitor.bounds;
        switched=Clock::now();firstFrameDelayMs=0;inputFrames=deliveredFrames=0;changedInputFrames=skippedInputFrames=samplingPasses=0;
        if(!candidate) error=saver?"Waiting for the main-display screensaver window; holding the last valid colours":"No capturable wallpaper host or maximized main-display application is available; holding the last valid colours";
        if(candidate) try { capture=std::make_unique<Capture>(candidate,monitor.bounds,samples,journal,sourceGeneration);std::lock_guard lock(capture->state->mutex);capture->state->configure(config); }
        catch(hresult_error const& failure) { error=to_string(failure.message());journal->write("capture_create_error",{{"generation",sourceGeneration},{"hresult",static_cast<int32_t>(failure.code())},{"message",error}}); }
        catch(std::exception const& failure) { error=failure.what();journal->write("capture_create_error",{{"generation",sourceGeneration},{"message",error}}); }
        journal->write("source_switch_complete",{{"generation",sourceGeneration},{"mode",mode},{"capture_created",capture!=nullptr}});
        status();
    }
    std::string restReason() const {
        return sessionLocked?"session-locked":displayOff?"display-asleep":automaticSaver?"automatic-screensaver":desktopUnavailable?"secure-desktop":monitorMissing?"monitor-disconnected":rest.active()?"awaiting-user-input":"";
    }
    void tick() {
        // RGB pause owns capture lifetime, independently of gamma and Awake.
        const bool paused=outputPaused||screen->lightingPaused();
        const bool nextCapturePaused=captureSuppressed();
        if(nextCapturePaused!=capturePaused) {
            capturePaused=nextCapturePaused;
            if(capturePaused)releaseCaptureInput();
            else wallpaperImageRequested=true;
            dirty=true;invalidate=true;
            journal->write("capture_pause",{{"paused",capturePaused}});
        }
        if(capturePaused&&(capture||source||!latest.empty()))releaseCaptureInput();
        // There is no session last-input change notification. Observe its token
        // on the existing output cadence only while the resume gate is held.
        if(observeNativeInput&&rest.active()&&rest.inputChanged(lastInput()))dirty=true;
        if(sessionLocked||powerSuspended||rest.active())awake.setContext(false,false);
        else if(displayOff)awake.setContext(false,!desktopUnavailable&&!automaticSaver&&!monitorMissing,false);
        if(observeNativeSources && !sessionLocked && !displayOff && !powerSuspended && dirty.exchange(false)) chooseSource();
        awake.tick();
        const bool resting=!restReason().empty();
        if(restReason()!=loggedRest) {journal->write("rest_transition",{{"previous",loggedRest},{"reason",restReason()}});loggedRest=restReason();}
        if(resting) {
            capture.reset();source=nullptr;mode="resting";latest.clear();
            wallpaperImageRequested=true;wallpaperImageActive=false;wallpaperImageSubmitted=false;
            inputFrames=deliveredFrames=0;changedInputFrames=skippedInputFrames=samplingPasses=0;received={};error.clear();
        }
        auto now=Clock::now();
        auto managedBrightness=screen->appliedBrightness(selectedMonitor.name);
        if(managedBrightness){displayBrightness.display=selectedMonitor.name;displayBrightness.factor=*managedBrightness;displayBrightness.valid=true;displayBrightness.error.clear();}
        if(!managedBrightness && !resting && config.at("follow_display_brightness").get<bool>() &&
           (lastBrightnessRead==Clock::time_point{} || displayBrightness.display!=selectedMonitor.name || now-lastBrightnessRead>=std::chrono::seconds(1))) {
            // Gamma has no change-notification API. Sample at 1 Hz independently
            // of capture; this observation period is not a recovery deadline.
            auto before=displayBrightness;
            readDisplayBrightness(displayBrightness,selectedMonitor);lastBrightnessRead=now;
            if(before.display!=displayBrightness.display || before.valid!=displayBrightness.valid || before.error!=displayBrightness.error || std::abs(before.factor-displayBrightness.factor)>=.001)
                journal->write("display_brightness",{{"display",displayBrightness.display},{"factor",displayBrightness.factor},{"valid",displayBrightness.valid},{"error",displayBrightness.error}});
        }
        if(capture) {
            auto state=capture->state;
            bool closed=false;
            {
                std::lock_guard lock(state->mutex);
                closed=state->closed;
                if(!closed && state->frames) {
                    if(wallpaperImageActive) {journal->write("wallpaper_image_replaced",{{"generation",sourceGeneration}});wallpaperImageActive=false;}
                    if(!sessionLocked && !displayOff) latest=state->colors;
                    inputFrames=state->frames;changedInputFrames=state->changedFrames;skippedInputFrames=state->skippedFrames;
                    samplingPasses=state->samplingPasses;processingMs=state->lastProcessingMs;
                    received=state->received;
                    if(state->error.empty()) error.clear();
                    if(firstFrameDelayMs==0) firstFrameDelayMs=std::chrono::duration<double,std::milli>(state->firstReceived-switched).count();
                }
                if(!state->error.empty()) error=state->error;
            }
            if(closed) { capture.reset(); source=nullptr; dirty=true; }
        }
        if(!identifyDevice.empty() && now>=identifyUntil) identifyDevice.clear();
        bool standby=resting || standbyPreview;
        if(!latest.empty() || calibration || !identifyDevice.empty() || standby || paused || hardware->any()) {
            auto submitBegan=Clock::now();
            auto sentBefore=deviceSubmissions;
            double elapsedMs=std::chrono::duration<double,std::milli>(now-lastOutput).count();
            size_t deviceIndex=0;
            {std::lock_guard lock(controllersMutex);
            for(auto& device:devices) {
                bool changed=false;
                bool overrideHardware=resting || standbyPreview || paused || !config.at("enabled").get<bool>() || calibration || !identifyDevice.empty();
                try {if(hardware->output(device.key,device.controller,overrideHardware,true)){++deviceIndex;continue;}}
                catch(const std::exception& e){controllerError=e.what();++deviceIndex;continue;}
                if(hardware->consumeReset(device.key))device.delivered=false;
                if(latest.empty() && !overrideHardware){++deviceIndex;continue;}
                for(size_t index=0;index<device.count;++index) {
                    bool enabled=config["enabled"].get<bool>() && layout[deviceIndex]["enabled"].get<bool>();
                    for(const auto& group:layout[deviceIndex]["groups"]) if(index>=group.at("first").get<size_t>() && index<group.at("first").get<size_t>()+group.at("count").get<size_t>()) enabled=enabled && group.value("enabled",true);
                    auto target=latest.empty()?0:latest[device.start+index];
                    bool testing=calibration || !identifyDevice.empty();
                    if(standby && !testing) {
                        auto r=samples[device.start+index];auto c=office::standbyColor(config,{static_cast<unsigned char>(RGBGetRValue(target)),static_cast<unsigned char>(RGBGetGValue(target)),static_cast<unsigned char>(RGBGetBValue(target))},std::chrono::duration<double>(now-standbyBegan).count(),(r.left+r.right)/2,(r.top+r.bottom)/2);
                        target=ToRGBColor(c[0],c[1],c[2]);
                    }
                    if(calibration) {
                        const auto p=samples[device.start+index];float x=(p.left+p.right)/2,y=(p.top+p.bottom)/2;
                        target=ToRGBColor(static_cast<unsigned char>(x*255),static_cast<unsigned char>((1-y)*255),static_cast<unsigned char>(index%2?40:160));
                    } else if(!identifyDevice.empty()) target=device.key==identifyDevice || identifyDevice=="*"?ToRGBColor(255,255,255):0;
                    else if(!enabled) target=0;
                    if(!testing) {
                        double gain=displayBrightness.applied(config.at("follow_display_brightness").get<bool>());
                        target=ToRGBColor(office::dimChannel(RGBGetRValue(target),gain),office::dimChannel(RGBGetGValue(target),gain),office::dimChannel(RGBGetBValue(target),gain));
                    }
                    if(resting || paused) target=0;
                    auto previous=device.colors[index];
                    double delta=std::max({std::abs(int(RGBGetRValue(target))-int(RGBGetRValue(previous))),std::abs(int(RGBGetGValue(target))-int(RGBGetGValue(previous))),std::abs(int(RGBGetBValue(target))-int(RGBGetBValue(previous)))});
                    auto weight=office::smoothingWeight(config,elapsedMs,delta);
                    auto color=initialized?ToRGBColor(
                        office::adaptedChannel(RGBGetRValue(target),RGBGetRValue(previous),weight),
                        office::adaptedChannel(RGBGetGValue(target),RGBGetGValue(previous),weight),
                        office::adaptedChannel(RGBGetBValue(target),RGBGetBValue(previous),weight)):target;
                    if(testing || !enabled || resting || paused) color=target;
                    device.colors[index]=color;
                    if(device.controller && (!device.delivered || previous!=color)) {
                        device.controller->SetColor(static_cast<unsigned>(index),color);changed=true;++ledWrites;
                    }
                }
                if(device.controller && changed) {device.controller->UpdateLEDs();device.delivered=true;++deviceSubmissions;}
                ++deviceIndex;
            }
            for(auto& [key,c]:nativeControllers)if(std::none_of(devices.begin(),devices.end(),[&](auto& d){return d.key==key;}) && hardware->hardware(key)) {
                try {bool over=resting || paused || !config.at("enabled").get<bool>();if(!hardware->output(key,c,over,false) && over && hardware->consumeReset(key)){c->SetAllColors(0);c->UpdateLEDs();}}
                catch(const std::exception& e){controllerError=e.what();}
            }}
            initialized=true; ++outputFrames;
            auto submitted=Clock::now();
            submitMs=deviceSubmissions==sentBefore?0:std::chrono::duration<double,std::milli>(submitted-submitBegan).count();
            if(wallpaperImageActive&&!wallpaperImageSubmitted&&deviceSubmissions!=sentBefore&&!resting&&!paused) {
                wallpaperImageSubmitted=true;
                journal->write("wallpaper_image_submitted",{{"load_to_submit_ms",std::chrono::duration<double,std::milli>(submitted-wallpaperImageBegan).count()},
                    {"device_submissions",deviceSubmissions-sentBefore},{"hardware_acknowledged",false}});
            }
            if(deviceSubmissions!=sentBefore && inputFrames && deliveredFrames!=inputFrames) {
                callbackToSubmitMs=std::chrono::duration<double,std::milli>(submitted-received).count();deliveredFrames=inputFrames;
            }
        }
        lastOutput=now;
        // Diagnostic reporting cadence never classifies a silent/static frame as
        // failure and never cancels or restarts a valid capture session.
        if(now-lastStatus>=std::chrono::seconds(1)) status();
        // Health cadence is observation only; never cancels/restarts capture.
        if(now-lastHealth>=std::chrono::seconds(10)) {journal->write("health",report());lastHealth=now;}
    }
    Json report() const {
        std::lock_guard lock(controllersMutex);
        return {{"mode",mode},{"standby",!restReason().empty() || standbyPreview},{"rest_reason",restReason()},{"input_desktop",inputDesktop},{"source_hwnd",reinterpret_cast<uintptr_t>(source)},
            {"monitor_policy","primary"},{"capture_display",selectedMonitor.name},{"awake",awake.status()},{"rest_resume",rest.status()},
            {"display_brightness_factor",displayBrightness.applied(config.at("follow_display_brightness").get<bool>())},
            {"display_brightness_valid",displayBrightness.valid},{"display_brightness_error",displayBrightness.error},
            {"display_brightness_display",displayBrightness.display},
            {"capture_bounds",{selectedMonitor.bounds.left,selectedMonitor.bounds.top,selectedMonitor.bounds.right,selectedMonitor.bounds.bottom}},
            {"source_generation",sourceGeneration},
            {"capture_paused",capturePaused},{"capture_active",capture!=nullptr},
            {"wallpaper_image_active",wallpaperImageActive},{"wallpaper_image_submitted",wallpaperImageSubmitted},
            {"wallpaper_image_load_ms",wallpaperImageLoadMs},{"wallpaper_image_error",wallpaperImageError},
            {"application_model","unified"},{"output_thread",GetCurrentThreadId()},{"output_paused",outputPaused||screen->lightingPaused()},{"screen",screen->status()},
            {"devices",devices.size()},{"leds",samples.size()},{"output_frames",outputFrames},
            {"input_frames",inputFrames},{"gpu_processing_ms",processingMs},
            {"changed_input_frames",changedInputFrames},{"skipped_input_frames",skippedInputFrames},{"sampling_passes",samplingPasses},
            {"device_submissions",deviceSubmissions},{"led_writes",ledWrites},
            {"callback_to_native_submit_ms",callbackToSubmitMs},{"native_submit_ms",submitMs},
            {"frame_age_ms",received==Clock::time_point{}?Json(nullptr):Json(std::chrono::duration<double,std::milli>(Clock::now()-received).count())},
            {"output_fps",measuredFps},
            {"online_devices",std::count_if(devices.begin(),devices.end(),[](const auto& d){return d.controller!=nullptr;})},{"controller_error",controllerError},
            {"first_frame_after_switch_ms",firstFrameDelayMs},{"has_valid_frame",!latest.empty()},
            {"error",error},{"diagnostic_error",journal->error()},{"updated_utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toStdString()}};
    }
    void status() {
        auto elapsed=std::chrono::duration<double>(Clock::now()-lastStatus).count();
        if(elapsed>0) measuredFps=(outputFrames-previousStatusOutput)/elapsed;
        previousStatusOutput=outputFrames;
        auto value=report();
        QSaveFile file(QString::fromStdWString(statusPath.wstring()));
        auto data=value.dump(2);
        bool saved=file.open(QIODevice::WriteOnly) && file.write(data.data(),data.size())==static_cast<qint64>(data.size()) && file.commit();
        auto failure=saved?std::string():file.errorString().toStdString();
        if(failure!=statusWriteError) {journal->write(saved?"status_write_recovered":"status_write_error",{{"message",failure}});statusWriteError=failure;}
        lastStatus=Clock::now();
    }
};

OfficeAmbience::OfficeAmbience(OpenRGBPluginAPIInterface* api,const std::filesystem::path& config):impl(std::make_unique<Impl>(api,config)) {}
OfficeAmbience::~OfficeAmbience()=default;
void OfficeAmbience::stop() { impl.reset(); }
void OfficeAmbience::resourcesUpdated(unsigned reason) {if(impl) impl->resourcesUpdated(reason);}
office::Json OfficeAmbience::command(const office::Json& request) {return impl->command(request);}
