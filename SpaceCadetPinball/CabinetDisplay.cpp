#include "pch.h"
#include "CabinetDisplay.h"
#include "translations.h"
#include "CabinetCyrillic.h"
#include "TPinballTable.h"
#include "pb.h"
#include "render.h"
#include "score.h"
#include "GroupData.h"
#include <bitset>
#include <climits>
#include <unordered_map>

namespace
{
    // Five columns per row, leftmost LED in bit 4. Lowercase forms keep their
    // ascenders and descenders, including Cyrillic breve, diaeresis and tails.
    struct Stencil { char32_t code; uint8_t rows[7]; };
    const Stencil font[] = {
        {U'?',{14,17,1,2,4,0,4}}, {U'!',{4,4,4,4,4,0,4}},
        {U'.',{0,0,0,0,0,0,4}}, {U',',{0,0,0,0,0,4,8}},
        {U':',{0,0,4,0,0,4,0}}, {U';',{0,0,4,0,0,4,8}},
        {U'/',{1,1,2,4,8,16,16}}, {U'\\',{16,16,8,4,2,1,1}},
        {U'-',{0,0,0,14,0,0,0}}, {U'—',{0,0,0,31,0,0,0}},
        {U'(',{2,4,8,8,8,4,2}}, {U')',{8,4,2,2,2,4,8}},
        {U'[',{14,8,8,8,8,8,14}}, {U']',{14,2,2,2,2,2,14}},
        {U'%',{25,26,2,4,8,11,19}}, {U'+',{0,4,4,31,4,4,0}},
        {U'*',{0,21,14,31,14,21,0}}, {U'=',{0,0,31,0,31,0,0}},
        {U'×',{0,17,10,4,10,17,0}},
        {U'«',{0,0,10,20,10,0,0}}, {U'»',{0,0,10,5,10,0,0}},
        {U'…',{0,0,0,0,0,0,21}}, {U'"',{10,10,0,0,0,0,0}},
        {U'\'',{4,4,0,0,0,0,0}}, {U'_',{0,0,0,0,0,0,31}},
        {U'&',{12,18,20,8,21,18,13}}, {U'№',{17,25,21,19,17,3,3}},
        {U'#',{10,10,31,10,31,10,10}}, {U'·',{0,0,0,4,0,0,0}},
        {U'<',{0,2,4,8,4,2,0}}, {U'>',{0,8,4,2,4,8,0}},
        {U'0',{14,17,19,21,25,17,14}}, {U'1',{4,12,4,4,4,4,14}},
        {U'2',{14,17,1,2,4,8,31}}, {U'3',{30,1,1,14,1,1,30}},
        {U'4',{2,6,10,18,31,2,2}}, {U'5',{31,16,16,30,1,1,30}},
        {U'6',{6,8,16,30,17,17,14}}, {U'7',{31,1,2,4,8,8,8}},
        {U'8',{14,17,17,14,17,17,14}}, {U'9',{14,17,17,15,1,2,12}},
        {U'A',{14,17,17,31,17,17,17}}, {U'B',{30,17,17,30,17,17,30}},
        {U'C',{14,17,16,16,16,17,14}}, {U'D',{28,18,17,17,17,18,28}},
        {U'E',{31,16,16,30,16,16,31}}, {U'F',{31,16,16,30,16,16,16}},
        {U'G',{14,17,16,23,17,17,15}}, {U'H',{17,17,17,31,17,17,17}},
        {U'I',{14,4,4,4,4,4,14}}, {U'J',{7,2,2,2,2,18,12}},
        {U'K',{17,18,20,24,20,18,17}}, {U'L',{16,16,16,16,16,16,31}},
        {U'M',{17,27,21,21,17,17,17}}, {U'N',{17,25,25,21,19,19,17}},
        {U'O',{14,17,17,17,17,17,14}}, {U'P',{30,17,17,30,16,16,16}},
        {U'Q',{14,17,17,17,21,18,13}}, {U'R',{30,17,17,30,20,18,17}},
        {U'S',{15,16,16,14,1,1,30}}, {U'T',{31,4,4,4,4,4,4}},
        {U'U',{17,17,17,17,17,17,14}}, {U'V',{17,17,17,17,17,10,4}},
        {U'W',{17,17,17,21,21,27,17}}, {U'X',{17,17,10,4,10,17,17}},
        {U'Y',{17,17,10,4,4,4,4}}, {U'Z',{31,1,2,4,8,16,31}},
        {U'a',{0,0,14,1,15,17,15}}, {U'b',{16,16,22,25,17,17,30}},
        {U'c',{0,0,14,17,16,17,14}}, {U'd',{1,1,13,19,17,17,15}},
        {U'e',{0,0,14,17,31,16,14}}, {U'f',{6,9,8,28,8,8,8}},
        {U'g',{0,0,15,17,15,1,14}}, {U'h',{16,16,22,25,17,17,17}},
        {U'i',{4,0,12,4,4,4,14}}, {U'j',{2,0,6,2,2,18,12}},
        {U'k',{16,16,18,20,24,20,18}}, {U'l',{12,4,4,4,4,4,14}},
        {U'm',{0,0,26,21,21,21,21}}, {U'n',{0,0,22,25,17,17,17}},
        {U'o',{0,0,14,17,17,17,14}}, {U'p',{0,0,30,17,30,16,16}},
        {U'q',{0,0,15,17,15,1,1}}, {U'r',{0,0,22,25,16,16,16}},
        {U's',{0,0,15,16,14,1,30}}, {U't',{8,8,28,8,8,9,6}},
        {U'u',{0,0,17,17,17,19,13}}, {U'v',{0,0,17,17,17,10,4}},
        {U'w',{0,0,17,17,21,21,10}}, {U'x',{0,0,17,10,4,10,17}},
        {U'y',{0,0,17,17,15,1,14}}, {U'z',{0,0,31,2,4,8,31}},
        {U'Б',{31,16,16,30,17,17,30}}, {U'Г',{31,16,16,16,16,16,16}},
        {U'Д',{6,10,10,10,10,31,17}}, {U'Ё',{10,0,31,16,30,16,31}},
        {U'Ж',{21,21,14,4,14,21,21}}, {U'З',{14,17,1,6,1,17,14}},
        {U'И',{17,17,19,21,25,17,17}}, {U'Й',{10,4,17,19,21,25,17}},
        {U'Л',{7,9,9,9,9,9,17}}, {U'П',{31,17,17,17,17,17,17}},
        {U'У',{17,17,17,15,1,2,12}}, {U'Ф',{4,14,21,21,21,14,4}},
        {U'Ц',{18,18,18,18,18,31,1}}, {U'Ч',{17,17,17,15,1,1,1}},
        {U'Ш',{21,21,21,21,21,21,31}}, {U'Щ',{21,21,21,21,21,31,1}},
        {U'Ъ',{24,8,8,14,9,9,14}}, {U'Ы',{17,17,17,29,21,21,29}},
        {U'Ь',{16,16,16,30,17,17,30}}, {U'Э',{14,17,1,15,1,17,14}},
        {U'Ю',{18,21,21,29,21,21,18}}, {U'Я',{15,17,17,15,5,9,17}},
        {U'б',{3,12,16,30,17,17,14}}, {U'в',{0,0,30,17,30,17,30}},
        {U'г',{0,0,31,16,16,16,16}}, {U'д',{0,0,6,10,10,31,17}},
        {U'ё',{10,0,14,17,31,16,14}}, {U'ж',{0,0,21,14,4,14,21}},
        {U'з',{0,0,14,1,6,1,14}}, {U'и',{0,0,17,19,21,25,17}},
        {U'й',{10,4,17,19,21,25,17}}, {U'к',{0,0,17,18,28,18,17}},
        {U'л',{0,0,7,9,9,9,17}}, {U'м',{0,0,17,27,21,17,17}},
        {U'н',{0,0,17,17,31,17,17}}, {U'п',{0,0,31,17,17,17,17}},
        {U'т',{0,0,31,4,4,4,4}}, {U'ф',{4,4,14,21,21,14,4}},
        {U'ц',{0,0,18,18,18,31,1}}, {U'ч',{0,0,17,17,15,1,1}},
        {U'ш',{0,0,21,21,21,21,31}}, {U'щ',{0,0,21,21,21,31,1}},
        {U'ъ',{0,0,24,8,14,9,14}}, {U'ы',{0,0,17,17,29,21,29}},
        {U'ь',{0,0,16,16,30,17,30}}, {U'э',{0,0,14,1,7,1,14}},
        {U'ю',{0,0,18,21,29,21,18}}, {U'я',{0,0,15,17,15,9,17}}
    };

    char32_t equivalent(char32_t code)
    {
        const char32_t* alternatives = U"АВСЕНКМОРТХаесорух–‑−“”‘’";
        const char32_t* shapes       = U"ABCEHKMOPTXaecopyx———\"\"''";
        for (int i = 0; alternatives[i]; ++i)
            if (alternatives[i] == code) return shapes[i];
        return code;
    }

    struct Letter { const Stencil* face; int first, width; };
    Letter glyph(char32_t code)
    {
        code = equivalent(code);
        const Stencil* face = &font[0];
        for (const auto& entry : font)
            if (entry.code == code) { face = &entry; break; }
        int bits = 0;
        for (auto row : face->rows) bits |= row;
        int first = 0, last = 4;
        while (!(bits & (16 >> first))) ++first;
        while (!(bits & (16 >> last))) --last;
        return {face, first, last - first + 1};
    }

    struct MatrixLetter
    {
        int width = 0;
        std::vector<SDL_Point> sites;
    };

    MatrixLetter readLetter(char32_t code, int stockFont)
    {
        MatrixLetter ch;
        if (const auto* letter = cabinetCyrillic::find(code))
        {
            ch.width = 2 * letter->width;
            for (int y = 0; y < 11; ++y)
                for (int x = 0; x < letter->width; ++x)
                    if (letter->rows[y] & (1 << x)) ch.sites.push_back({2*x, 2*y});
            return ch;
        }
        code = equivalent(code);
        gdrv_bitmap8* source = nullptr;
        const bool counterDigit = code >= U'0' && code <= U'9' && !pb::FullTiltMode && pb::MainTable && pb::MainTable->CurScoreStruct;
        if (counterDigit)
            source = pb::MainTable->CurScoreStruct->CharBmp[code - U'0'];
        else if (code>=32 && code<128 && stockFont>=0)
            source=pb::record_table->GetBitmap(stockFont+int(code)-32);

        if (source && source->IndexedBmpPtr)
        {
            ch.width = source->Width;
            for (int y = 0; y < source->Height; ++y)
            {
                const auto* row = reinterpret_cast<const Uint8*>(source->IndexedBmpPtr) + (source->Height - 1 - y) * source->IndexedStride;
                for (int x = 0; x < source->Width; ++x)
                    if (row[x] == 100) ch.sites.push_back({x,y}); // centre of an original emitting lens
            }
            if (!ch.sites.empty())
            {
                if (counterDigit)
                {
                    int left=source->Width, right=0;
                    const int top=ch.sites.front().y;
                    for (const auto& p : ch.sites) { left=std::min(left,p.x); right=std::max(right,p.x); }
                    for (auto& p : ch.sites) { p.x-=left; p.y-=top; }
                    ch.width=right-left+2;
                }
                else
                {
                    // PB_MSGFT glyphs have integer-pixel side bearings. Place
                    // their original silhouettes onto the panel's two-pixel lattice.
                    const int bearing=ch.sites.front().x % 2;
                    for (auto& p : ch.sites)
                    {
                        p.x-=bearing;
                        p.y-=p.y%2;
                    }
                    ch.width=(ch.width+1-bearing)/2*2;
                }
                return ch;
            }
        }
        const auto fallback=glyph(code);
        ch.width=2*fallback.width;
        for (int y=0;y<7;++y)
            for (int x=0;x<fallback.width;++x)
                if (fallback.face->rows[y] & (16>>(x+fallback.first)))
                    ch.sites.push_back({2*x,2*y+2});
        return ch;
    }

    using Word = std::vector<MatrixLetter>;
    using Paragraph = std::vector<Word>;
    std::vector<Paragraph> readWords(const char* text)
    {
        const int stockFont=pb::record_table && !pb::FullTiltMode ? pb::record_table->record_labeled("pbmsg_ft") : -1;
        std::vector<Paragraph> paragraphs(1);
        Word word;
        while (*text)
        {
            unsigned code;
            const int count = ImTextCharFromUtf8(&code,text,nullptr);
            if (count<=0) break;
            text+=count;
            if (code=='\r') continue;
            if (code=='\n' || code==' ' || code=='\t' || code==0xa0 || code==0x202f)
            {
                if (!word.empty()) { paragraphs.back().push_back(std::move(word)); word.clear(); }
                if (code=='\n') paragraphs.emplace_back();
            }
            else word.push_back(readLetter(code,stockFont));
        }
        if (!word.empty()) paragraphs.back().push_back(std::move(word));
        return paragraphs;
    }

    struct Positioned { const MatrixLetter* letter; int x; };
    using Line = std::vector<Positioned>;
    std::vector<Line> wrap(const std::vector<Paragraph>& text, int width)
    {
        constexpr int gap=2, space=6; // one lens between letters, three extra at a word break
        std::vector<Line> lines;
        for (const auto& paragraph : text)
        {
            lines.emplace_back();
            int cursor=0;
            for (const auto& word : paragraph)
            {
                int wordWidth=-gap;
                for (const auto& ch : word) wordWidth+=gap+ch.width;
                if (cursor && cursor+space+wordWidth>width) { lines.emplace_back(); cursor=0; }
                if (cursor) cursor+=space;
                for (const auto& ch : word)
                {
                    if (cursor && cursor+ch.width>width) { lines.emplace_back(); cursor=0; }
                    lines.back().push_back({&ch,cursor});
                    cursor+=ch.width+gap;
                }
            }
        }
        return lines;
    }

    constexpr int texels=4;
    constexpr uint32_t coreBit=1u<<12;
    constexpr uint32_t nearBits=(7u<<6)|(7u<<11)|(7u<<16);

    uint32_t neighborhood(const std::vector<uint8_t>& sites,int w,int h,int x,int y)
    {
        uint32_t bits=0;
        for (int j=std::max(0,y-2); j<std::min(h,y+3); ++j)
            for (int i=std::max(0,x-2);i<std::min(w,x+3);++i)
                if (sites[j*w+i]) bits|=1u<<((j-y+2)*5+i-x+2);
        return bits;
    }

    // Each 4x4 patch is one native pixel in an actual HD counter sprite.
    // Its surrounding LEDs determine whether it contains a bright core, violet
    // glass between two cores, the outer glow of a stroke, or unlit backing.
    struct Ink
    {
        uint32_t neighborhood;
        SDL_Color color[texels][texels];
    };

    std::vector<Ink> counterSamples()
    {
        std::vector<Ink> samples;
        if (!pb::MainTable || !pb::MainTable->CurScoreStruct || pb::FullTiltMode) return samples;
        auto base=SDL_GetBasePath();
        const auto dir=std::string(base ? base : "")+"HD/";
        SDL_free(base);
        const int first=pb::record_table->record_labeled("font1");
        if (first<0) return samples;
        for (int digit=0;digit<10;++digit)
        {
            auto bmp=pb::MainTable->CurScoreStruct->CharBmp[digit];
            if (!bmp || !bmp->IndexedBmpPtr) continue;
            auto file=SDL_LoadBMP((dir+std::to_string(first+digit)+".bmp").c_str());
            if (!file) continue;
            auto atlas=file->w==texels*bmp->Width && file->h==texels*bmp->Height ?
                SDL_ConvertSurfaceFormat(file,SDL_PIXELFORMAT_RGBA32,0) : nullptr;
            SDL_FreeSurface(file);
            if (!atlas) continue;
            std::vector<uint8_t> sites(bmp->Width*bmp->Height);
            for (int y=0;y<bmp->Height;++y)
                for (int x=0;x<bmp->Width;++x)
                    sites[y*bmp->Width+x]=uint8_t(bmp->IndexedBmpPtr[(bmp->Height-1-y)*bmp->IndexedStride+x])==100;
            for (int y=0;y<bmp->Height;++y)
                for (int x=0;x<bmp->Width;++x)
                {
                    const auto mask=neighborhood(sites,bmp->Width,bmp->Height,x,y);
                    if (!mask) continue;
                    Ink sample{mask};
                    for (int j=0;j<texels;++j)
                    {
                        const auto* row=reinterpret_cast<const SDL_Color*>(static_cast<const Uint8*>(atlas->pixels)+(y*texels+j)*atlas->pitch);
                        std::copy_n(row+x*texels,texels,sample.color[j]);
                    }
                    samples.push_back(sample);
                }
            SDL_FreeSurface(atlas);
        }
        return samples;
    }

    using Matches=std::vector<const Ink*>;
    Matches matchCounterInk(uint32_t mask,const std::vector<Ink>& samples)
    {
        Matches matches;
        size_t best=SIZE_MAX;
        for (const auto& sample : samples)
        {
            const auto diff=mask^sample.neighborhood;
            const auto cost=64*bool(diff&coreBit)+8*std::bitset<32>(diff&nearBits).count()+std::bitset<32>(diff).count();
            if (cost>best) continue;
            if (cost<best) { best=cost; matches.clear(); }
            matches.push_back(&sample);
        }
        return matches;
    }

    constexpr int pageGuard = 2; // separate the filtered edges of neighboring pages
    SDL_Texture* rasterize(SDL_Renderer* renderer, const char* text, int width, int panelHeight, bool hd, int& pageCount)
    {
        static const auto samples=counterSamples();
        const auto paragraphs=readWords(text);
        constexpr int marginX=2, leading=22;
        const auto lines=wrap(paragraphs,std::max(1,width-2*marginX));
        const int rowsPerPage=std::max(1,panelHeight/leading);
        const int pages=(int(lines.size())+rowsPerPage-1)/rowsPerPage;
        const int pageStride=panelHeight+2*pageGuard;
        const int height=pages*pageStride;
        std::vector<uint8_t> sites(width*height);
        for (size_t i=0;i<lines.size();++i)
            for (const auto& at : lines[i])
                for (const auto& p : at.letter->sites)
                {
                    const int x=marginX+at.x+p.x;
                    const int rowY=1+(int(i)%rowsPerPage)*leading+p.y;
                    const int y=(int(i)/rowsPerPage)*pageStride+pageGuard+rowY;
                    if (x<width && rowY<panelHeight) sites[y*width+x]=1;
                }
        const int tw=width*texels,th=height*texels;
        std::vector<SDL_Color> pixels(tw*th,{0,0,0,0});
        std::unordered_map<uint32_t,Matches> patterns;
        for (int y=0;y<height;++y)
            for (int x=0;x<width;++x)
            {
                const auto mask=neighborhood(sites,width,height,x,y);
                if (!mask) continue;
                const Ink* ink=nullptr;
                if (hd && !samples.empty())
                {
                    auto result=patterns.emplace(mask,Matches{});
                    auto& matches=result.first->second;
                    if (result.second) matches=matchCounterInk(mask,samples);
                    const uint32_t variation=uint32_t(x)*2654435761u+uint32_t(y%pageStride)*2246822519u;
                    ink=matches[variation%matches.size()];
                }
                constexpr uint32_t adjacent=(1u<<7)|(1u<<11)|(1u<<13)|(1u<<17);
                const SDL_Color native=mask&coreBit ? SDL_Color{148,204,244,255} :
                    mask&adjacent ? SDL_Color{120,88,220,255} : SDL_Color{0,0,0,0};
                for (int j=0;j<texels;++j)
                {
                    auto out=pixels.data()+(y*texels+j)*tw+x*texels;
                    if (ink) std::copy_n(ink->color[j],texels,out);
                    else std::fill_n(out,texels,native);
                }
            }
        auto texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STATIC,tw,th);
        if (!texture) return nullptr;
        if (SDL_UpdateTexture(texture,nullptr,pixels.data(),tw*sizeof(SDL_Color))!=0)
        {
            SDL_DestroyTexture(texture);
            return nullptr;
        }
        SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(texture,SDL_ScaleModeLinear);
        pageCount=pages;
        return texture;
    }
}

bool CabinetDisplay::SupportsLanguage()
{
    const auto lang = translations::GetCurrentLanguage()->Language;
    return lang == Lang::Russian || lang == Lang::English;
}

void CabinetDisplay::DrawNumber(const std::string& digits, float right, float centerY, float pitch, Uint32 color)
{
    if (digits.empty()) return;
    auto list = ImGui::GetWindowDrawList();
    float left = right - (digits.size() * 6 - 1) * pitch;
    for (const char digit : digits)
    {
        const auto face = glyph(digit).face;
        for (int y = 0; y < 7; ++y)
            for (int x = 0; x < 5; ++x)
                if (face->rows[y] & (16 >> x))
                    list->AddCircleFilled({left + (x + .5f) * pitch, centerY + (y - 3) * pitch},
                                          pitch * .44f, color, 8);
        left += 6 * pitch;
    }
}

CabinetDisplay::~CabinetDisplay()
{
    if (texture) SDL_DestroyTexture(texture);
}

void CabinetDisplay::BeginMessage(int ticks)
{
    startTicks=ticks;
    pageDwellMs=0;
}

void CabinetDisplay::Draw(SDL_Renderer* renderer, SDL_Rect rect, int nativeWidth, int nativeHeight,
                          const char* text, int endTicks)
{
    if (rect.w < 1 || rect.h < 1 || nativeWidth < 1 || nativeHeight < 1 || !text || !*text) return;
    const bool hd=render::EnhancedGraphics && render::HdAssetCount>0;
    if (!texture || cachedText != text || width != nativeWidth || height != nativeHeight || cachedHD!=hd)
    {
        auto replacement = rasterize(renderer, text, nativeWidth, nativeHeight, hd, pages);
        if (!replacement) return;
        if (cachedText != text) BeginMessage(pb::time_ticks);
        if (texture) SDL_DestroyTexture(texture);
        texture = replacement;
        cachedText = text;
        width = nativeWidth; height = nativeHeight; cachedHD=hd;
    }
    const ImVec2 scale = ImGui::GetIO().DisplayFramebufferScale;
    if (scale.x <= 0 || scale.y <= 0) return;
    const int elapsed=std::max(0, pb::time_ticks-startTicks);
    if (!pageDwellMs)
    {
        // A queued notice can get a final 250 ms on screen after its own
        // deadline. Keep that first page steady. Refreshing the same notice
        // later must not change the pace or rewind the displayed page.
        pageDwellMs=1800;
        if (endTicks>=0)
        {
            const int share=(endTicks-startTicks)/pages;
            pageDwellMs=share>=450 ? std::min(1800,share) : INT_MAX;
        }
    }
    const int page=(elapsed/pageDwellMs)%pages;
    const int stride=height+2*pageGuard;
    const float top=float(page*stride+pageGuard)/(pages*stride);
    const float bottom=float(page*stride+pageGuard+height)/(pages*stride);
    ImGui::GetBackgroundDrawList()->AddImage(texture, ImVec2(rect.x / scale.x, rect.y / scale.y),
        ImVec2((rect.x + rect.w) / scale.x, (rect.y + rect.h) / scale.y), {0,top}, {1,bottom});
}
