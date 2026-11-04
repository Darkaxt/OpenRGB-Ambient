// Exercise the production shader and monitor crop using actual D3D11 WARP.
// The hardware capture path is separately verified against the running LG.
#include "../OfficeAmbience.cpp"
#include <iostream>

int main() {
    try {
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
        // Genuine dark imagery must become dark, not be treated as a bad frame.
        std::fill(pixels.begin(),pixels.end(),0xff000000);
        state.context->UpdateSubresource(texture.get(),0,nullptr,pixels.data(),128*4,0);
        state.processTexture(texture.get());
        for(auto color:state.colors) if(color) throw std::runtime_error("Valid black frame was discarded");
        if(state.frames!=2) throw std::runtime_error("Frame accounting failed");
        std::cout<<"PASS: production D3D11 shader, monitor isolation, RGB ordering, valid black, frame accounting\n";
        return 0;
    } catch(hresult_error const& error) { std::cerr<<to_string(error.message())<<'\n'; }
      catch(std::exception const& error) { std::cerr<<error.what()<<'\n'; }
    return 1;
}
