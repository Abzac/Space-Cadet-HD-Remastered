#include "pch.h"
#include "RailReflections.h"
#include "HdRenderer.h"

namespace
{
    constexpr int S = HdRenderer::Scale;
    constexpr int Tile = 12;
    constexpr int Width = 365, Height = 470; // Windows DAT table group 107
    constexpr int Cols = (Width + Tile - 1) / Tile;
    constexpr int Rows = (Height + Tile - 1) / Tile;
    constexpr uint16_t TableOwner = 107 + 1;
    struct Point { float x, y, halfWidth; };
    struct Site
    {
        uint16_t x, y;
        float rx, ry, rz, strength;
        bool extra;
    };
    std::vector<Site> sites[Rows][Cols];
    std::vector<uint32_t> buffer;
    SDL_Texture* overlay = nullptr;
    int capacity = 0, visibleCount = 0;
    bool ready = false, diagnosticBypass = false, extraBypass = false;

    // Center and width of the exposed face of each stamped guide, in table
    // pixels. Sections end at each painted coupler. The plunger trough itself
    // has an orange wear strip; only its steel sidewall is designated here.
    const Point leftWire[] = {
        {58.1f,319.8f,.74f}, {64.3f,325.0f,.90f}, {79.4f,336.9f,1.02f},
        {98.6f,351.4f,1.05f}, {117.6f,365.3f,1.0f}, {131.0f,375.5f,.82f}};
    const Point rightWire[] = {
        {235.7f,375.4f,.82f}, {247.7f,366.2f,1.0f}, {266.2f,352.4f,1.1f},
        {279.8f,340.6f,1.0f}, {285.2f,335.9f,.80f}};
    const Point shooterInner[] = {
        {295.8f,260.0f,1.48f}, {297.5f,277,1.65f}, {300.6f,300,1.68f},
        {303.9f,325,1.63f}, {306.4f,345,1.57f}, {308.6f,365,1.62f}, {310.1f,375,1.40f}};
    const Point shooterOuter[] = {
        {317.5f,256.0f,1.62f}, {320.6f,277,1.58f}, {324.1f,300,1.88f},
        {327.9f,325,1.90f}, {330.8f,345,1.88f}, {333.0f,365,1.62f}, {334.3f,376,1.46f}};
    const Point upperReturn[] = {
        {79.0f,39.0f,1.53f},{83.0f,30.2f,1.86f},{88,27.3f,1.8f},
        {93,23.7f,2.28f},{98,22.3f,2.33f},{103,21.8f,2.48f},{108,22,2.32f},
        {113,22.8f,2.32f},{118,24.3f,2.22f},{123,26.2f,2.23f},{128,29.1f,2.1f}};
    const Point upperRun[] = {
        {146,30.5f,2.32f},{153,28.9f,2.5f},{163,27.6f,2.48f},{178,26.8f,2.57f},
        {188,26.8f,2.56f},{198,27.8f,2.53f},{208,29.2f,2.58f},{217,31.0f,2.50f}};
    const Point upperCorner[] = {
        {235,37.2f,2.35f},{243,40.0f,2.42f},{248,42.1f,2.29f}};
    const Point shooterCorner[] = {
        {258,48,2.32f},{263,51.5f,2.29f},{268,55.2f,2.40f},{273,59.8f,2.4f}};
    const Point leftReturn[] = {
        {27.7f,378.2f,.82f},{27.8f,375,1.48f},{28.5f,370,1.60f},{29.9f,360,1.60f},
        {31.4f,350,1.62f},{33.0f,340,1.60f},{34.6f,330,1.43f},{35.8f,320,1.50f},
        {37.6f,310,1.70f},{39.3f,300,1.68f},{40.9f,290,1.51f},{42.0f,285,1.24f},
        {43.3f,280,.80f},{44.8f,275.8f,.40f}};

    float sat(float v) { return std::max(0.f, std::min(v, 1.f)); }

    void bakeSite(const SDL_Surface* art, int ix, int iy, float nx, float ny, float weight, bool extra)
    {
        const auto row = reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(art->pixels)+iy*art->pitch);
        const uint32_t c = row[ix];
        const int r = (c>>16)&255, g = (c>>8)&255, b = c&255;
        const int chroma = std::max(r,std::max(g,b))-std::min(r,std::min(g,b));
        // Gate the mapped shape by its actual metallic albedo. This excludes
        // the red rubber lip, copper edging, black openings and painted bands.
        const float neutral = sat((62.f-chroma)/32.f) * sat((std::min(r,std::min(g,b))-34.f)/32.f);
        const float strength = neutral * weight;
        if (strength < .02f) return;
        const float nz = std::sqrt(std::max(0.f,1.f-nx*nx-ny*ny));
        sites[iy/(S*Tile)][ix/(S*Tile)].push_back({uint16_t(ix),uint16_t(iy),
            2*nz*nx,2*nz*ny,2*nz*nz-1.f,strength,extra});
    }

    template<size_t N> void trace(const Point (&points)[N], const SDL_Surface* art, float polish, bool extra=false)
    {
        float xmin = Width, xmax = 0, ymin = Height, ymax = 0;
        for (const auto& p : points)
        {
            xmin = std::min(xmin,p.x-p.halfWidth); xmax = std::max(xmax,p.x+p.halfWidth);
            ymin = std::min(ymin,p.y-p.halfWidth); ymax = std::max(ymax,p.y+p.halfWidth);
        }
        for (int iy = std::max(0,int(ymin*S)); iy < std::min(Height*S,int(ymax*S)+1); ++iy)
            for (int ix = std::max(0,int(xmin*S)); ix < std::min(Width*S,int(xmax*S)+1); ++ix)
            {
                const float x = (ix+.5f)/S, y = (iy+.5f)/S;
                float best = 1.f, nx = 0, ny = 0;
                for (size_t i = 1; i < N; ++i)
                {
                    const auto& a = points[i-1]; const auto& b = points[i];
                    const float vx = b.x-a.x, vy = b.y-a.y;
                    const float length2 = vx*vx+vy*vy;
                    const float u = sat(((x-a.x)*vx+(y-a.y)*vy)/length2);
                    const float dx = x-a.x-u*vx, dy = y-a.y-u*vy;
                    const float w = a.halfWidth+u*(b.halfWidth-a.halfWidth);
                    const float r2 = (dx*dx+dy*dy)/(w*w);
                    if (r2 >= best) continue;
                    best = r2;
                    const float t = (dx*vy-dy*vx)/(w*length2);
                    nx = t*vy; ny = -t*vx;
                }
                if (best >= 1) continue;
                bakeSite(art,ix,iy,nx,ny,polish*sat((1-best)*9.f),extra);
            }
    }

    // A shallow pressed bridge or folded hood has a broad face between two
    // short bevels. Coordinates follow its long axis; the ends stop before
    // its mounting bolts or the opening below the hood.
    void stampedFace(const SDL_Surface* art, Point a, Point b, float tilt, float crown, float polish)
    {
        const float length = hypotf(b.x-a.x,b.y-a.y);
        const float ux = (b.x-a.x)/length, uy = (b.y-a.y)/length;
        const float margin = std::max(a.halfWidth,b.halfWidth);
        const int left = int(floorf((std::min(a.x,b.x)-margin)*S));
        const int right = int(ceilf((std::max(a.x,b.x)+margin)*S));
        const int top = int(floorf((std::min(a.y,b.y)-margin)*S));
        const int bottom = int(ceilf((std::max(a.y,b.y)+margin)*S));
        for (int iy=top; iy<bottom; ++iy)
            for (int ix=left; ix<right; ++ix)
            {
                const float x=(ix+.5f)/S-a.x, y=(iy+.5f)/S-a.y;
                const float along=x*ux+y*uy;
                if (along<=0 || along>=length) continue;
                const float width=a.halfWidth+(b.halfWidth-a.halfWidth)*(along/length);
                const float cross=(-x*uy+y*ux)/width;
                if (fabsf(cross)>=1.f) continue;
                const float edge=sat((fabsf(cross)-.72f)/.28f);
                const float acrossNormal=std::max(-.89f,std::min(.89f,
                    tilt+crown*cross+copysignf(.33f*edge, cross)));
                const float lengthNormal=.10f*(2.f*along/length-1.f);
                const float coverage=sat((1.f-fabsf(cross))*width*S)*sat(std::min(along,length-along)*S);
                bakeSite(art,ix,iy,lengthNormal*ux-acrossNormal*uy,
                         lengthNormal*uy+acrossNormal*ux,polish*coverage,true);
            }
    }

    uint32_t brushedBall(const uint32_t* ball, int side, int x, int y, int spread)
    {
        // A brushed face samples a finite cone of incoming directions. Keep
        // transparent parts of the sphere texture outside the footprint.
        const auto c = ball[y*side+x];
        int r = 2*((c>>16)&255), g = 2*((c>>8)&255), b = 2*(c&255), count=2;
        const SDL_Point offsets[]={{-spread,0},{spread,0},{0,-spread},{0,spread}};
        for (auto offset:offsets)
        {
            const int u=x+offset.x,v=y+offset.y;
            if (u<0 || u>=side || v<0 || v>=side) continue;
            const auto sample=ball[v*side+u];
            if ((sample>>24)<190) continue;
            r+=(sample>>16)&255; g+=(sample>>8)&255; b+=sample&255; ++count;
        }
        return uint32_t((r/count)<<16 | (g/count)<<8 | b/count);
    }
}

void RailReflections::Init(const std::string& dir)
{
    Shutdown();
    auto src = SDL_LoadBMP((dir+"107.bmp").c_str());
    if (!src) return;
    auto art = SDL_ConvertSurfaceFormat(src,SDL_PIXELFORMAT_ARGB8888,0);
    SDL_FreeSurface(src);
    if (!art) return;
    if (art->w==Width*S && art->h==Height*S)
    {
        trace(leftWire,art,.84f); trace(rightWire,art,.84f);
        trace(shooterInner,art,.73f); trace(shooterOuter,art,.73f);
        trace(upperReturn,art,.69f); trace(upperRun,art,.69f);
        trace(upperCorner,art,.67f); trace(shooterCorner,art,.67f);
        trace(leftReturn,art,.55f,true);
        stampedFace(art,{282.6f,252.7f,2.69f},{297.7f,245.8f,2.74f}, .42f,.29f,.57f);
        stampedFace(art,{136.2f,30.4f,2.29f},{141.4f,37.9f,2.38f}, .39f,.28f,.52f);
        stampedFace(art,{231.2f,29.1f,2.47f},{224.2f,37.9f,2.31f},-.43f,.24f,.52f);
        stampedFace(art,{258.6f,216.8f,2.95f},{271.0f,225.0f,2.94f}, .54f,.10f,.32f);
        ready = true;
    }
    SDL_FreeSurface(art);
}

void RailReflections::Shutdown()
{
    if (overlay) SDL_DestroyTexture(overlay);
    overlay = nullptr; capacity = 0; ready = false; diagnosticBypass = false; extraBypass = false; visibleCount = 0;
    buffer.clear();
    for (auto& row : sites) for (auto& tile : row) tile.clear();
}

void RailReflections::Draw(SDL_Renderer* renderer, const SDL_FRect& dest, int viewWidth, int viewHeight,
                           const uint16_t* owners, int hdPitch, int boardWidth, float cx, float cy, float radius,
                           const uint32_t* ball, int side, int originX, int originY)
{
    visibleCount = 0;
    if (!ready || diagnosticBypass || !owners || !ball || radius<=0) return;
    const float reach = radius*3.35f;
    const int left = std::max(0,int(std::floor((cx-reach)*S)));
    const int top = std::max(0,int(std::floor((cy-reach)*S)));
    const int right = std::min(std::min(Width,boardWidth)*S,int(std::ceil((cx+reach)*S)));
    const int bottom = std::min(std::min(Height,viewHeight)*S,int(std::ceil((cy+reach)*S)));
    if (left>=right || top>=bottom) return;
    const int w = right-left, h = bottom-top;
    const float invRadius = 1.f/radius;
    // Intersect the reflected camera ray at each polished surface sample with
    // the nearest physical ball. A cylindrical strip contracts the reflection
    // across its width, while preserving its motion along the guide.
    for (int ty = top/(S*Tile); ty <= (bottom-1)/(S*Tile); ++ty)
        for (int tx = left/(S*Tile); tx <= (right-1)/(S*Tile); ++tx)
            for (const auto& p : sites[ty][tx])
            {
                if ((extraBypass && p.extra) || p.x<left || p.x>=right || p.y<top || p.y>=bottom ||
                    owners[p.y*hdPitch+p.x]!=TableOwner) continue;
                const float x=(p.x+.5f)/S, y=(p.y+.5f)/S;
                const float dx=(cx-x)*invRadius, dy=(cy-y)*invRadius, dz=.32f;
                const float dist2=dx*dx+dy*dy+dz*dz;
                if (dist2 > 11.2f) continue;
                const float along=dx*p.rx+dy*p.ry+dz*p.rz;
                const float impact2=dist2-along*along;
                if (along<=0.f || impact2>=1.f) continue;
                const float nearHit=along-std::sqrt(std::max(0.f,1.f-impact2));
                if (nearHit<0) continue;
                // Sample the live, lit steel sphere, including its current
                // environment and lamp reflections. Avoid the silhouette's
                // transparency and contact shadow at its extreme edge.
                const float hx=(nearHit*p.rx-dx)*.90f, hy=(nearHit*p.ry-dy)*.90f;
                const int bx=int((cx+hx*radius-originX)*S), by=int((cy+hy*radius-originY)*S);
                if (bx<0 || by<0 || bx>=side || by>=side) continue;
                const uint32_t c=ball[by*side+bx];
                if ((c>>24)<190) continue;
                const float falloff = sat((11.2f-dist2)/7.f);
                const float edge = sat((1-impact2)*(p.extra ? 2.5f : 6.f));
                const int alpha = int(255*p.strength*falloff*edge);
                if (alpha<3) continue;
                if (!visibleCount) buffer.assign(w*h,0);
                const auto reflected = p.extra ? brushedBall(ball,side,bx,by,std::max(1,int(radius*S*.09f))) : c;
                buffer[(p.y-top)*w+p.x-left] = (uint32_t(alpha)<<24) | (reflected&0xffffff);
                ++visibleCount;
            }
    if (!visibleCount) return;
    if (!overlay || w>capacity || h>capacity)
    {
        if (overlay) SDL_DestroyTexture(overlay);
        capacity=std::max(128,std::max(w,h)+16);
        overlay=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,capacity,capacity);
        if (!overlay) return;
        SDL_SetTextureBlendMode(overlay,SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(overlay,SDL_ScaleModeLinear);
    }
    SDL_Rect src{0,0,w,h};
    if (SDL_UpdateTexture(overlay,&src,buffer.data(),w*4)!=0) return;
    const float sx=dest.w/(viewWidth*S), sy=dest.h/(viewHeight*S);
    SDL_FRect where{dest.x+left*sx,dest.y+top*sy,w*sx,h*sy};
    SDL_RenderCopyF(renderer,overlay,&src,&where);
}

int RailReflections::LastSampleCount() { return visibleCount; }
void RailReflections::SetDiagnosticBypass(bool bypass) { diagnosticBypass = bypass; }
void RailReflections::SetAdditionalDiagnosticBypass(bool bypass) { extraBypass = bypass; }
