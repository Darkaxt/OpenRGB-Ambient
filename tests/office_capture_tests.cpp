// Exercise the production shader and monitor crop using actual D3D11 WARP.
// The hardware capture path is separately verified against the running LG.
#define OFFICE_CAPTURE_TEST
#include "../OfficeAmbience.cpp"
#include <iostream>

void mediaMattes() {
    const unsigned width=512,height=216;
    const std::vector<Sample> map={{0,.25f,.03f,.75f},{.25f,0,.75f,.03f},{.97f,.25f,1,.75f},{.25f,.97f,.75f,1}};
    CaptureState state(map,{0,0,width,height},{0,0,width,height},D3D_DRIVER_TYPE_WARP);
    auto options=office::processingDefaults();options["black_bar_detect"]=true;state.configure(options);
    std::vector<uint32_t> pixels(width*height);com_ptr<ID3D11Texture2D> texture;
    D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
    desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;desc.Usage=D3D11_USAGE_DEFAULT;
    check_hresult(state.device->CreateTexture2D(&desc,nullptr,texture.put()));
    auto upload=[&] {state.context->UpdateSubresource(texture.get(),0,nullptr,pixels.data(),width*4,0);state.processTexture(texture.get());};
    auto unchanged=[&] {
        for(size_t i=0;i<map.size();++i) {
            const float* a=reinterpret_cast<const float*>(&state.effectiveRegions[i]);const float* b=reinterpret_cast<const float*>(&map[i]);
            for(int j=0;j<4;++j) if(std::abs(a[j]-b[j])>.0001f) throw std::runtime_error("Uncertain/dark imagery must retain the entire image mapping");
        }
    };
    // The same retained GPU input changes between pillarbox, letterbox and full
    // image. Saved normalized LED coordinates never change with media format.
    for(double ratio:{4./3,16./9,16./10,1.85,2.,2.2,2.35,2.39,2.4,2.55,2.76}) {
        bool vertical=ratio<double(width)/height;
        int extent=std::lround(vertical?height*ratio:width/ratio);
        int edge=(int(vertical?width:height)-extent)/2;
        std::fill(pixels.begin(),pixels.end(),0xff000000);
        for(unsigned y=0;y<height;++y) for(unsigned x=0;x<width;++x) {
            int p=vertical?x:y;
            if(p>=edge && p<edge+extent) pixels[y*width+x]=0xff28b4f0;
        }
        upload();
        auto expected=vertical?Sample{float(edge)/width,0,float(edge+extent)/width,1}:Sample{0,float(edge)/height,1,float(edge+extent)/height};
        const float tolerance=3.f/(vertical?width:height);
        for(size_t i=0;i<map.size();++i) {
            auto r=map[i];Sample target{expected.left+r.left*(expected.right-expected.left),expected.top+r.top*(expected.bottom-expected.top),
                expected.left+r.right*(expected.right-expected.left),expected.top+r.bottom*(expected.bottom-expected.top)};
            const float* a=reinterpret_cast<const float*>(&state.effectiveRegions[i]);const float* b=reinterpret_cast<const float*>(&target);
            for(int j=0;j<4;++j) if(std::abs(a[j]-b[j])>tolerance) {
                // Diagnose the production GPU boundary, not a CPU reimplementation.
                std::string diagnostic=std::string(shader)+R"(
float4 diagnose(Vertex input):SV_TARGET {
 uint w,h;image.GetDimensions(w,h);float edge=(1-(4./3)/((float)w/h))*.5;
 return float4((float)w/h,borders.x,mediaEdge(edge,1./w,true,false),mediaEdge(1-edge,1./w,true,true));
})";
                com_ptr<ID3DBlob> code,errors;
                check_hresult(D3DCompile(diagnostic.data(),diagnostic.size(),nullptr,nullptr,nullptr,"diagnose","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,code.put(),errors.put()));
                state.contentPixel=nullptr;check_hresult(state.device->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,state.contentPixel.put()));
                state.sampleRetained();D3D11_TEXTURE2D_DESC d{};state.contentTexture->GetDesc(&d);
                d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
                com_ptr<ID3D11Texture2D> read;check_hresult(state.device->CreateTexture2D(&d,nullptr,read.put()));
                state.context->CopyResource(read.get(),state.contentTexture.get());D3D11_MAPPED_SUBRESOURCE m{};
                check_hresult(state.context->Map(read.get(),0,D3D11_MAP_READ,0,&m));auto v=static_cast<float*>(m.pData);
                std::string detail=" aspect="+std::to_string(v[0])+" enabled="+std::to_string(v[1])+" left="+std::to_string(v[2])+" right="+std::to_string(v[3]);
                state.context->Unmap(read.get(),0);
                throw std::runtime_error("Media matte format was not mapped: "+std::to_string(ratio)+" coordinate "+std::to_string(j)+detail);
            }
        }
        if(std::all_of(state.colors.begin(),state.colors.end(),[](auto c){return !c;})) throw std::runtime_error("Detected media content did not reach actual LED output");
    }
    std::fill(pixels.begin(),pixels.end(),0xff28b4f0);upload();unchanged();
    std::fill(pixels.begin(),pixels.end(),0xff020204);upload();unchanged();
    std::fill(pixels.begin(),pixels.end(),0xff000000);upload();unchanged();
    for(auto c:state.colors) if(c) throw std::runtime_error("Black media frame must produce black LEDs");
    // An ordinary dark edge, even near a common ratio, lacks paired-matte proof.
    std::fill(pixels.begin(),pixels.end(),0xff28b4f0);
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<112;++x) pixels[y*width+x]=0xff000000;
    upload();unchanged();
    // Uniform black margins with no supported/content-confirmed ratio are not
    // license to guess a crop. Disabling detection also restores exact mapping.
    std::fill(pixels.begin(),pixels.end(),0xff000000);
    for(unsigned y=0;y<height;++y)for(unsigned x=97;x<415;++x) pixels[y*width+x]=0xff28b4f0;
    upload();unchanged();options["black_bar_detect"]=false;state.configure(options);state.sampleRetained();unchanged();
}

int main() {
    try {
        mediaMattes();
        CaptureState state({around(.25f,.25f,.2f,.2f),around(.75f,.25f,.2f,.2f),
                            around(.25f,.75f,.2f,.2f),around(.75f,.75f,.2f,.2f)},
                           {0,0,128,64},{64,0,128,64},D3D_DRIVER_TYPE_WARP);
        std::vector<uint32_t> pixels(128*64);
        for(unsigned y=0;y<64;++y) for(unsigned x=0;x<128;++x) {
            unsigned r=0,g=0,b=0;
            if(x<64) r=g=b=255; // Other monitor must never enter LED samples.
            else if(y<32) { if(x<96) r=255; else g=255; }
            else { if(x<96) b=255; else r=g=255; }
            pixels[y*128+x]=0xff000000|(r<<16)|(g<<8)|b;
        }
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width=128;desc.Height=64;desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
        desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;desc.Usage=D3D11_USAGE_DEFAULT;
        D3D11_SUBRESOURCE_DATA data{pixels.data(),128*4,0};
        com_ptr<ID3D11Texture2D> texture;
        check_hresult(state.device->CreateTexture2D(&desc,&data,texture.put()));
        state.processTexture(texture.get());
        std::vector<RGBColor> expected={ToRGBColor(255,0,0),ToRGBColor(0,255,0),ToRGBColor(0,0,255),ToRGBColor(255,255,0)};
        if(state.colors!=expected) throw std::runtime_error("GPU region average or selected-monitor crop is incorrect");
        // A static wallpaper may produce no further WGC callbacks. Opening the
        // editor must still obtain a preview from the retained GPU texture.
        auto awaitPreview=[&](float blur=-1) {
            auto expectedGeneration=state.previewRevision;state.requestPreview(blur);
            std::unique_lock lock(state.previewMutex);
            state.previewCondition.wait(lock,[&]{return (state.previewGeneration>=expectedGeneration && state.previewRenderedBlur==blur) || !state.previewError.empty();});
            if(!state.previewError.empty()) throw std::runtime_error(state.previewError);
            return state.previewBase64;
        };
        auto firstPreview=awaitPreview();
        if(firstPreview.empty() || state.frames!=1)
            throw std::runtime_error("Static retained frame preview requires another capture callback");
        auto revision=state.previewRevision,passes=state.samplingPasses;
        state.processTexture(texture.get());
        if(state.frames!=2 || state.changedFrames!=1 || state.skippedFrames!=1 || state.previewRevision!=revision || state.samplingPasses!=passes)
            throw std::runtime_error("Identical source must skip sampling and preview invalidation while counting the real callback");
        if(awaitPreview()!=firstPreview || state.previewEncodes!=1) throw std::runtime_error("Identical captured image must reuse encoded preview");
        // A pixel outside this LG crop cannot affect the wallpaper input.
        pixels[0]^=1;state.context->UpdateSubresource(texture.get(),0,nullptr,pixels.data(),128*4,0);state.processTexture(texture.get());
        if(state.skippedFrames!=2 || state.samplingPasses!=passes) throw std::runtime_error("Other-monitor changes must remain static for the LG");
        // Exact comparison includes even one low-bit pixel, not just sampled LEDs.
        pixels[64]^=1;state.context->UpdateSubresource(texture.get(),0,nullptr,pixels.data(),128*4,0);state.processTexture(texture.get());
        if(state.changedFrames!=2 || state.samplingPasses!=passes+1) throw std::runtime_error("Single-pixel LG change was missed by GPU equality");
        auto changedPreview=awaitPreview();
        for(int i=0;i<100;++i) state.requestPreview();
        if(awaitPreview()!=changedPreview || state.previewEncodes!=2) throw std::runtime_error("Unchanged static frames must share one encoded preview across clients");
        // The panel's save path must update the actual constant buffer, with
        // unchanged hardware indexes, rather than merely redrawing an editor.
        state.regions=state.makeRegions({around(.75f,.75f,.2f,.2f),around(.25f,.75f,.2f,.2f),
                                        around(.75f,.25f,.2f,.2f),around(.25f,.25f,.2f,.2f)});
        // Production map saves explicitly resample the retained GPU texture;
        // an unchanged capture callback must not be needed to apply that edit.
        {std::lock_guard lock(state.mutex);state.sampleRetained();}
        awaitPreview();
        std::reverse(expected.begin(),expected.end());
        if(state.colors!=expected || state.previewBase64.empty())
            throw std::runtime_error("Live GPU region replacement or requested capture preview failed");
        // Genuine dark imagery must become dark, not be treated as a bad frame.
        std::fill(pixels.begin(),pixels.end(),0xff000000);
        state.context->UpdateSubresource(texture.get(),0,nullptr,pixels.data(),128*4,0);
        state.processTexture(texture.get());
        for(auto color:state.colors) if(color) throw std::runtime_error("Valid black frame was discarded");
        if(state.frames!=5) throw std::runtime_error("Frame accounting failed");
        // Controls exercise the same GPU path used by the actual lighting.
        std::fill(pixels.begin(),pixels.end(),0xff000000);
        for(unsigned y=0;y<64;++y) for(unsigned x=64;x<96;++x) pixels[y*128+x]=0xffff0000;
        state.context->UpdateSubresource(texture.get(),0,nullptr,pixels.data(),128*4,0);
        state.regions=state.makeRegions({around(.55f,.5f,.02f,.02f),around(.2f,.5f,.02f,.02f),around(.8f,.5f,.02f,.02f),around(.2f,.5f,.02f,.02f)});
        auto options=office::processingDefaults();
        state.configure(options);state.processTexture(texture.get());auto unblurred=state.colors;
        // Unsaved slider edits must affect the retained image without applying
        // processing settings, sampling LEDs, changing colours or WGC callbacks.
        auto decode=[](const std::string& value) {return QImage::fromData(QByteArray::fromBase64(QByteArray::fromStdString(value)),"PNG");};
        auto draftZero=awaitPreview(0);auto previewBefore=decode(draftZero);
        auto beforePasses=state.samplingPasses,beforeFrames=state.frames;
        auto draftTen=awaitPreview(10);auto previewAfter=decode(draftTen);
        if(draftTen==draftZero || previewBefore.isNull() || previewAfter.isNull()
            || previewAfter.pixelColor(352,320).red()<=previewBefore.pixelColor(352,320).red())
            throw std::runtime_error("Draft blur does not visibly spread colour in the actual GPU preview");
        auto encodes=state.previewEncodes;
        if(awaitPreview(10)!=draftTen || state.previewEncodes!=encodes)
            throw std::runtime_error("Unchanged draft blur re-encodes a static preview");
        if(awaitPreview(0)!=draftZero || awaitPreview()!=draftZero)
            throw std::runtime_error("Returning draft blur to zero/default did not restore the original image");
        if(state.colors!=unblurred || state.samplingPasses!=beforePasses || state.frames!=beforeFrames)
            throw std::runtime_error("Draft preview changed applied LED processing or invented capture callbacks");
        options["blur"]=10.0;state.configure(options);state.sampleRetained();
        if(RGBGetRValue(state.colors[0])<=RGBGetRValue(unblurred[0])) throw std::runtime_error("Blur did not spread adjacent colour into the actual LED sample");
        if(awaitPreview()!=draftTen) throw std::runtime_error("Draft and applied blur disagree or apply blur twice");
        options["blur"]=0;options["brightness"]=0;state.configure(options);state.sampleRetained();
        for(auto c:state.colors) if(c) throw std::runtime_error("Brightness control is not wired to GPU output");
        options["brightness"]=1;options["saturation"]=0;state.configure(options);state.sampleRetained();
        auto gray=state.colors[1];if(RGBGetRValue(gray)!=RGBGetGValue(gray) || RGBGetGValue(gray)!=RGBGetBValue(gray)) throw std::runtime_error("Saturation control did not produce gray");
        options["saturation"]=1;state.configure(options);state.sampleRetained();
        if(state.colors!=unblurred || state.frames!=6) throw std::runtime_error("Static-frame settings failed to restore exact identity without inventing capture frames");
        // A wide blur must integrate high-frequency detail. Nine separated
        // mip-zero taps leave sharp repeated edges instead of a smooth image.
        for(unsigned y=0;y<64;++y) for(unsigned x=64;x<128;++x) pixels[y*128+x]=(x%2)?0xffffffff:0xff000000;
        state.context->UpdateSubresource(texture.get(),0,nullptr,pixels.data(),128*4,0);state.processTexture(texture.get());
        auto blurredDetail=decode(awaitPreview(10));int low=255,high=0;
        for(int x=160;x<480;++x) {int v=blurredDetail.pixelColor(x,320).red();low=std::min(low,v);high=std::max(high,v);}
        if(high-low>20 || low<100 || high>155) throw std::runtime_error("Wide blur retains sharp ghosted detail instead of integrating its footprint");
        for(unsigned y=0;y<64;++y) for(unsigned x=64;x<128;++x) pixels[y*128+x]=(y<16 || y>=48)?0xff000000:0xffff0000;
        state.context->UpdateSubresource(texture.get(),0,nullptr,pixels.data(),128*4,0);
        state.regions=state.makeRegions(std::vector<Sample>(4,Sample{.1f,0,.9f,.1f}));state.processTexture(texture.get());
        auto bar=RGBGetRValue(state.colors[0]);options["black_bar_detect"]=true;state.configure(options);state.sampleRetained();
        if(RGBGetRValue(state.colors[0])<=bar+20) throw std::runtime_error("Black bars were not excluded from the actual sample");
        options["black_bar_detect"]=false;options["strategy"]="fixed";options["depth"]=50;state.configure(options);state.sampleRetained();
        if(RGBGetRValue(state.colors[0])<=bar+20) throw std::runtime_error("Fixed sampling depth did not reach the image content");
        for(auto algorithm:{"adaptive","percentile","dominant"}) {options["strategy"]=algorithm;state.configure(options);state.sampleRetained();}
        // Deliberately hold the real encoder at its boundary. Sampling must
        // continue and the last valid preview must survive until publication.
        std::mutex barrier;std::condition_variable ready;bool entered=false,released=false;
        state.beforePreviewEncode=[&]{std::unique_lock lock(barrier);entered=true;ready.notify_all();ready.wait(lock,[&]{return released;});};
        std::string retained;{std::lock_guard lock(state.previewMutex);retained=state.previewBase64;}
        state.requestPreview();{std::unique_lock lock(barrier);ready.wait(lock,[&]{return entered;});}
        bool available=state.mutex.try_lock();
        if(available) {auto before=state.frames;state.processTexture(texture.get());available=state.frames==before+1;state.mutex.unlock();}
        bool held;{std::lock_guard lock(state.previewMutex);held=state.previewBase64==retained;}
        state.requestPreview();{std::lock_guard lock(barrier);released=true;ready.notify_all();}
        awaitPreview();
        if(!available || !held) throw std::runtime_error("Preview encoding blocks capture or clears the last valid image");
        state.beforePreviewEncode={};
        auto changedBefore=state.changedFrames;desc.Width=256;desc.Height=128;
        std::vector<uint32_t> resized(256*128,0xff000000);D3D11_SUBRESOURCE_DATA resizedData{resized.data(),256*4,0};
        com_ptr<ID3D11Texture2D> resizedTexture;check_hresult(state.device->CreateTexture2D(&desc,&resizedData,resizedTexture.put()));
        state.processTexture(resizedTexture.get());
        if(state.changedFrames!=changedBefore+1) throw std::runtime_error("Resized capture must invalidate equality and sampling");
        auto passesBefore=state.samplingPasses;state.processTexture(resizedTexture.get());
        if(state.samplingPasses!=passesBefore) throw std::runtime_error("Resized static capture must settle again");
        std::cout<<"PASS: production D3D11 shader, monitor isolation, RGB ordering, valid black, frame accounting\n";
        return 0;
    } catch(hresult_error const& error) { std::cerr<<to_string(error.message())<<'\n'; }
      catch(std::exception const& error) { std::cerr<<error.what()<<'\n'; }
    return 1;
}
