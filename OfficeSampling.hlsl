// Same filtered source for LED extraction and the topology's image preview.
Texture2D image : register(t0);
SamplerState linearSampler : register(s0);
cbuffer Regions : register(b0) { float4 region[384]; };
cbuffer Options : register(b1) { float4 algorithm; float4 appearance; float4 borders; float4 spare; };
struct Vertex { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
Vertex vs(uint index : SV_VertexID) {
    Vertex o; o.uv=float2((index<<1)&2,index&2);
    o.position=float4(o.uv*float2(2,-2)+float2(-1,1),0,1);return o;
}
float luminance(float3 c) {return dot(c,float3(.2126,.7152,.0722));}
float3 filtered(float2 uv,float lod) {
    uint w,h;image.GetDimensions(w,h);
    float2 radius=appearance.x*float2((float)h/w,1);
    float3 c=0;
    // Separable binomial kernel in 9 taps; clamped at the LG's image edge.
    [unroll] for(int y=-1;y<=1;++y) [unroll] for(int x=-1;x<=1;++x)
        c+=image.SampleLevel(linearSampler,uv+radius*float2(x,y),lod).rgb*(x==0?2:1)*(y==0?2:1)/16;
    return c;
}
float3 corrected(float3 c) {
    c=saturate(lerp(luminance(c).xxx,c,appearance.y));
    float l=luminance(c);if(l>0.0001 && l<appearance.w) c*=appearance.w/l;
    return saturate(c*appearance.z);
}
bool blackLine(float at,bool vertical) {
    float l=0;
    [unroll] for(int i=0;i<9;++i) {
        float t=(i+.5)/9;
        l=max(l,luminance(image.SampleLevel(linearSampler,vertical?float2(at,t):float2(t,at),0).rgb));
    }
    return l<.012;
}
float4 contentBounds() {
    float4 b=float4(0,0,1,1);
    if(borders.x<.5) return b;
    // Bounded content scan is image processing, not a capture timeout/retry.
    for(int side=0;side<4;++side) {
        float edge=0;
        for(int i=0;i<32;++i) {
            float d=(i+.5)/128;
            if(!blackLine(side<2?d:1-d,side%2==0)) break;
            edge=(i+1.)/128;
        }
        if(side<2) b[side]=edge;else b[side]=1-edge;
    }
    // Do not crop an entirely black frame into a synthetic bright frame.
    return b;
}
float4 ps(Vertex input) : SV_TARGET {
    float4 r=region[(uint)input.position.x];float4 content=contentBounds();
    r=float4(lerp(content.xy,content.zw,r.xy),lerp(content.xy,content.zw,r.zw));
    if(algorithm.x==1 || algorithm.x==2) {
        float2 mid=(r.xy+r.zw)*.5;float depth=algorithm.y;
        if(algorithm.x==2) {
            float contrast=abs(luminance(filtered(r.xy,0))-luminance(filtered(r.zw,0)));
            depth=lerp(algorithm.z,algorithm.w,saturate(contrast*4));
        }
        uint w,h;image.GetDimensions(w,h);
        // Depth is measured in pixels of the monitor's smaller dimension.
        if((r.z-r.x)*w>(r.w-r.y)*h) {
            float top=clamp(mid.y-depth*.5,content.y,max(content.y,content.w-depth));r.y=top;r.w=min(content.w,top+depth);
        } else {
            depth*=((float)h/w);float left=clamp(mid.x-depth*.5,content.x,max(content.x,content.z-depth));r.x=left;r.z=min(content.z,left+depth);
        }
    }
    float3 values[16];float brightness[16];float3 total=0;
    uint width,height;image.GetDimensions(width,height);
    float lod=max(0,log2(max((r.z-r.x)*width,(r.w-r.y)*height)/4));
    [unroll] for(uint y=0;y<4;++y) [unroll] for(uint x=0;x<4;++x) {
        uint i=y*4+x;values[i]=filtered(lerp(r.xy,r.zw,(float2(x,y)+.5)/4),lod);
        brightness[i]=luminance(values[i]);total+=values[i];
    }
    float3 color=total/16;
    if(algorithm.x>=3) {
        // Order actual colours by luminance, retaining hue at the chosen rank.
        for(int i=1;i<16;++i) for(int j=i;j>0;--j) if(brightness[j]<brightness[j-1]) {
            float l=brightness[j];brightness[j]=brightness[j-1];brightness[j-1]=l;
            float3 c=values[j];values[j]=values[j-1];values[j-1]=c;
        }
        color=values[algorithm.x==3?12:8];
    }
    return float4(corrected(color),1);
}
float4 previewPs(Vertex input) : SV_TARGET {return float4(corrected(filtered(input.uv,0)),1);}
