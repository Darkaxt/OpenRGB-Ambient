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
            for(int j=0;j<4;++j) if(std::abs(a[j]-b[j])>tolerance) throw std::runtime_error("Media matte format was not mapped into its detected content rectangle: "+std::to_string(ratio)+
                " region "+std::to_string(i)+" coordinate "+std::to_string(j)+" actual "+std::to_string(a[j])+" expected "+std::to_string(b[j]));
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
        auto awaitPreview=[&] {
            auto expectedGeneration=state.previewRevision;state.requestPreview();
            std::unique_lock lock(state.previewMutex);
            state.previewCondition.wait(lock,[&]{return state.previewGeneration>=expectedGeneration || !state.previewError.empty();});
            if(!state.previewError.empty()) throw std::runtime_error(state.previewError);
            return state.previewBase64;
        };
        auto firstPreview=awaitPreview();
        if(firstPreview.empty() || state.frames!=1)
            throw std::runtime_error("Static retained frame preview requires another capture callback");
        for(int i=0;i<100;++i) state.requestPreview();
        if(awaitPreview()!=firstPreview || state.previewEncodes!=1) throw std::runtime_error("Unchanged static frames must share one encoded preview across clients");
        // The panel's save path must update the actual constant buffer, with
        // unchanged hardware indexes, rather than merely redrawing an editor.
        state.regions=state.makeRegions({around(.75f,.75f,.2f,.2f),around(.25f,.75f,.2f,.2f),
                                        around(.75f,.25f,.2f,.2f),around(.25f,.25f,.2f,.2f)});
        {std::lock_guard lock(state.mutex);state.processTexture(texture.get());}
        awaitPreview();
        std::reverse(expected.begin(),expected.end());
        if(state.colors!=expected || state.previewBase64.empty())
            throw std::runtime_error("Live GPU region replacement or requested capture preview failed");
        // Genuine dark imagery must become dark, not be treated as a bad frame.
        std::fill(pixels.begin(),pixels.end(),0xff000000);
        state.context->UpdateSubresource(texture.get(),0,nullptr,pixels.data(),128*4,0);
        state.processTexture(texture.get());
        for(auto color:state.colors) if(color) throw std::runtime_error("Valid black frame was discarded");
        if(state.frames!=3) throw std::runtime_error("Frame accounting failed");
        // Controls exercise the same GPU path used by the actual lighting.
        std::fill(pixels.begin(),pixels.end(),0xff000000);
        for(unsigned y=0;y<64;++y) for(unsigned x=64;x<96;++x) pixels[y*128+x]=0xffff0000;
        state.context->UpdateSubresource(texture.get(),0,nullptr,pixels.data(),128*4,0);
        state.regions=state.makeRegions({around(.55f,.5f,.02f,.02f),around(.2f,.5f,.02f,.02f),around(.8f,.5f,.02f,.02f),around(.2f,.5f,.02f,.02f)});
        auto options=office::processingDefaults();
        state.configure(options);state.processTexture(texture.get());auto unblurred=state.colors;
        options["blur"]=10.0;state.configure(options);state.sampleRetained();
        if(RGBGetRValue(state.colors[0])<=RGBGetRValue(unblurred[0])) throw std::runtime_error("Blur did not spread adjacent colour into the actual LED sample");
        options["blur"]=0;options["brightness"]=0;state.configure(options);state.sampleRetained();
        for(auto c:state.colors) if(c) throw std::runtime_error("Brightness control is not wired to GPU output");
        options["brightness"]=1;options["saturation"]=0;state.configure(options);state.sampleRetained();
        auto gray=state.colors[1];if(RGBGetRValue(gray)!=RGBGetGValue(gray) || RGBGetGValue(gray)!=RGBGetBValue(gray)) throw std::runtime_error("Saturation control did not produce gray");
        options["saturation"]=1;state.configure(options);state.sampleRetained();
        if(state.colors!=unblurred || state.frames!=4) throw std::runtime_error("Static-frame settings failed to restore exact identity without inventing capture frames");
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
        std::cout<<"PASS: production D3D11 shader, monitor isolation, RGB ordering, valid black, frame accounting\n";
        return 0;
    } catch(hresult_error const& error) { std::cerr<<to_string(error.message())<<'\n'; }
      catch(std::exception const& error) { std::cerr<<error.what()<<'\n'; }
    return 1;
}
