#include "NrTuningDialog.h"

#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

constexpr wchar_t kClassName[] = L"BetterXcloudDLSS5NrTuning";
constexpr int kIdSave = 9001;
constexpr int kIdReload = 9002;
constexpr int kIdDefaults = 9003;
constexpr int kIdClose = 9004;

enum class ParamKind { Bool, Numeric, Enum, Key };

struct ParamDesc {
    const wchar_t* key;
    const wchar_t* label;
    ParamKind kind;
    bool autoAllowed;
    double minValue;
    double maxValue;
    double step;
    const wchar_t* defaultValue;
    const wchar_t* choices; // semicolon separated label=value
    const wchar_t* group;
};

const ParamDesc kParams[] = {
    {L"Enabled", L"Neural Rendering", ParamKind::Bool, true, 0,0,0, L"auto", nullptr, L"General"},
    {L"RunBeforeSR", L"Run before SR", ParamKind::Bool, true, 0,0,0, L"auto", nullptr, L"General"},
    {L"FinishedPicture", L"Finished picture", ParamKind::Bool, false, 0,0,0, L"false", nullptr, L"General"},
    {L"HdrTransfer", L"HDR transfer", ParamKind::Bool, false, 0,0,0, L"false", nullptr, L"General"},
    {L"DeferredDLSS", L"Deferred DLSS", ParamKind::Bool, true, 0,0,0, L"auto", nullptr, L"General"},
    {L"PrivateUpscaler", L"Private upscaler", ParamKind::Enum, true, 0,0,0, L"auto",
        L"DLSS=0;FSR 2.2=1;FidelityFX FSR=2;XeSS=3", L"General"},
    {L"ResidualAcrossRR", L"Residual across RR", ParamKind::Bool, true, 0,0,0, L"auto", nullptr, L"General"},
    {L"ResidualAcrossRRBlend", L"RR residual history blend", ParamKind::Numeric, true, 0.01,1.0,0.01, L"auto", nullptr, L"General"},

    {L"Passes", L"Model passes", ParamKind::Numeric, true, 1,3,1, L"auto", nullptr, L"Model"},
    {L"UnlockPasses", L"Unlock up to 30 passes", ParamKind::Bool, false, 0,0,0, L"false", nullptr, L"Model"},
    {L"TransferStrength", L"Transfer strength", ParamKind::Numeric, true, 0,2,0.01, L"auto", nullptr, L"Model"},
    {L"ColourStrength", L"Colour strength", ParamKind::Numeric, true, 0,1,0.01, L"auto", nullptr, L"Model"},
    {L"WhitePointScale", L"White point scale", ParamKind::Numeric, true, 0.25,4.0,0.01, L"auto", nullptr, L"Model"},
    {L"MaxRatio", L"Maximum brighten ratio", ParamKind::Numeric, true, 1.0,4.0,0.05, L"auto", nullptr, L"Model"},
    {L"WorkingScale", L"Model working scale", ParamKind::Numeric, true, 0.25,2.0,0.01, L"auto", nullptr, L"Model"},
    {L"ScalingDownscaler", L"Supersampling downscaler", ParamKind::Enum, true, 0,0,0, L"auto",
        L"FSR1=0;Bicubic=1;Catmull-Rom=2;Lanczos2=3;Lanczos3=4;Kaiser2=5;Kaiser3=6;MAGIC=7", L"Model"},
    {L"Preset", L"Preset", ParamKind::Numeric, true, 0,3,1, L"auto", nullptr, L"Model"},
    {L"Style", L"Style", ParamKind::Enum, true, 0,0,0, L"auto",
        L"Standard=0;Natural=1;Cinematic=2", L"Model"},
    {L"Intensity", L"Intensity", ParamKind::Numeric, true, 0,2,0.01, L"auto", nullptr, L"Model"},
    {L"LocalStructure", L"Local structure", ParamKind::Numeric, true, 0,2,0.01, L"auto", nullptr, L"Model"},
    {L"LocalTone", L"Local tone", ParamKind::Numeric, true, 0,2,0.01, L"auto", nullptr, L"Model"},
    {L"SkinStructure", L"Skin structure", ParamKind::Numeric, true, -1,2,0.01, L"auto", nullptr, L"Model"},
    {L"AutoMask", L"NVIDIA AutoMask", ParamKind::Bool, true, 0,0,0, L"auto", nullptr, L"Model"},

    {L"SkinProtection", L"Skin protection", ParamKind::Bool, false, 0,0,0, L"false", nullptr, L"Skin / environment"},
    {L"SkinToneEnabled", L"Skin tone filter", ParamKind::Bool, false, 0,0,0, L"true", nullptr, L"Skin / environment"},
    {L"SkinDetail", L"Skin detail", ParamKind::Numeric, false, 0,1,0.01, L"1.0", nullptr, L"Skin / environment"},
    {L"SkinColour", L"Skin colour", ParamKind::Numeric, false, 0,1,0.01, L"1.0", nullptr, L"Skin / environment"},
    {L"EnvironmentDetail", L"Environment detail", ParamKind::Numeric, false, 0,1,0.01, L"1.0", nullptr, L"Skin / environment"},
    {L"EnvironmentColour", L"Environment colour", ParamKind::Numeric, false, 0,1,0.01, L"1.0", nullptr, L"Skin / environment"},
    {L"ShowSkinMask", L"Show skin mask", ParamKind::Bool, false, 0,0,0, L"false", nullptr, L"Skin / environment"},

    {L"Pass2Preset", L"Pass 2 preset", ParamKind::Numeric, true, 0,3,1, L"auto", nullptr, L"Pass 2"},
    {L"Pass2Style", L"Pass 2 style", ParamKind::Enum, true, 0,0,0, L"auto",
        L"Standard=0;Natural=1;Cinematic=2", L"Pass 2"},
    {L"Pass2Intensity", L"Pass 2 intensity", ParamKind::Numeric, true, 0,2,0.01, L"auto", nullptr, L"Pass 2"},
    {L"Pass2LocalStructure", L"Pass 2 local structure", ParamKind::Numeric, true, 0,2,0.01, L"auto", nullptr, L"Pass 2"},
    {L"Pass2LocalTone", L"Pass 2 local tone", ParamKind::Numeric, true, 0,2,0.01, L"auto", nullptr, L"Pass 2"},
    {L"Pass2SkinStructure", L"Pass 2 skin structure", ParamKind::Numeric, true, -1,2,0.01, L"auto", nullptr, L"Pass 2"},
    {L"Pass2AutoMask", L"Pass 2 AutoMask", ParamKind::Bool, true, 0,0,0, L"auto", nullptr, L"Pass 2"},

    {L"Pass3Preset", L"Pass 3 preset", ParamKind::Numeric, true, 0,3,1, L"auto", nullptr, L"Pass 3"},
    {L"Pass3Style", L"Pass 3 style", ParamKind::Enum, true, 0,0,0, L"auto",
        L"Standard=0;Natural=1;Cinematic=2", L"Pass 3"},
    {L"Pass3Intensity", L"Pass 3 intensity", ParamKind::Numeric, true, 0,2,0.01, L"auto", nullptr, L"Pass 3"},
    {L"Pass3LocalStructure", L"Pass 3 local structure", ParamKind::Numeric, true, 0,2,0.01, L"auto", nullptr, L"Pass 3"},
    {L"Pass3LocalTone", L"Pass 3 local tone", ParamKind::Numeric, true, 0,2,0.01, L"auto", nullptr, L"Pass 3"},
    {L"Pass3SkinStructure", L"Pass 3 skin structure", ParamKind::Numeric, true, -1,2,0.01, L"auto", nullptr, L"Pass 3"},
    {L"Pass3AutoMask", L"Pass 3 AutoMask", ParamKind::Bool, true, 0,0,0, L"auto", nullptr, L"Pass 3"},

    {L"DebugView", L"Debug view", ParamKind::Enum, true, 0,0,0, L"auto",
        L"Off=0;Model input=1;Raw model output=2;Amplified difference=3", L"Debug"},
    {L"AutoCapture", L"Auto capture comparison frames", ParamKind::Bool, true, 0,0,0, L"auto", nullptr, L"Debug"},
    {L"ToggleKey", L"NR toggle virtual key", ParamKind::Key, true, 0,0,0, L"auto", nullptr, L"Debug"},
};

struct Widget {
    const ParamDesc* desc{};
    HWND control{};
    HWND autoCheck{};
    HWND valueLabel{};
    int sliderScale{1};
    std::vector<std::wstring> enumValues;
};

std::wstring ReadIni(const std::filesystem::path& path, const wchar_t* key, const wchar_t* def)
{
    wchar_t buffer[256]{};
    GetPrivateProfileStringW(L"DlssNr", key, def, buffer, 256, path.c_str());
    return buffer;
}

bool IEquals(const std::wstring& a, const wchar_t* b)
{
    return _wcsicmp(a.c_str(), b) == 0;
}

std::wstring FormatNumber(double value, double step)
{
    std::wostringstream out;
    if (step >= 1.0) out << static_cast<int>(std::llround(value));
    else if (step >= 0.1) { out.setf(std::ios::fixed); out.precision(1); out << value; }
    else { out.setf(std::ios::fixed); out.precision(2); out << value; }
    return out.str();
}

class DialogImpl {
public:
    DialogImpl(HWND owner, std::filesystem::path ini)
        : owner_(owner), ini_(std::move(ini)) {}

    void Show()
    {
        INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_BAR_CLASSES};
        InitCommonControlsEx(&icc);

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = WndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kClassName;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        RegisterClassExW(&wc);

        hwnd_ = CreateWindowExW(
            WS_EX_APPWINDOW,
            kClassName,
            L"DLSS 5 Neural Rendering Tuning",
            WS_OVERLAPPEDWINDOW | WS_VSCROLL,
            CW_USEDEFAULT, CW_USEDEFAULT, 930, 820,
            owner_, nullptr, GetModuleHandleW(nullptr), this);
        if (!hwnd_) { delete this; return; }

        font_ = CreateFontW(
            -16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,
            DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");

        Build();
        Reload();
        ShowWindow(hwnd_, SW_SHOW);
        UpdateWindow(hwnd_);
    }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        DialogImpl* self = reinterpret_cast<DialogImpl*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            self = reinterpret_cast<DialogImpl*>(cs->lpCreateParams);
            self->hwnd_ = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (self) return self->Handle(msg, wp, lp);
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    HWND Make(
        const wchar_t* cls, const wchar_t* text, DWORD style,
        int x, int y, int w, int h, int id=0)
    {
        HWND child=CreateWindowExW(
            0,cls,text,WS_CHILD|WS_VISIBLE|style,
            x,y,w,h,hwnd_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            GetModuleHandleW(nullptr),nullptr);
        if(font_) SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font_),TRUE);
        return child;
    }

    void Build()
    {
        contentHeight_=20;
        std::wstring currentGroup;
        widgets_.reserve(std::size(kParams));

        for(size_t i=0;i<std::size(kParams);++i){
            const auto& d=kParams[i];
            if(currentGroup!=d.group){
                currentGroup=d.group;
                HWND heading=Make(L"STATIC",currentGroup.c_str(),SS_LEFT,
                                  20,contentHeight_,840,24);
                HFONT bold=CreateFontW(-18,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,
                    DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
                if(bold){ SendMessageW(heading,WM_SETFONT,reinterpret_cast<WPARAM>(bold),TRUE); groupFonts_.push_back(bold); }
                contentHeight_+=34;
            }

            Make(L"STATIC",d.label,SS_LEFT,30,contentHeight_+5,230,24);

            Widget w{};
            w.desc=&d;
            const int baseId=10000+static_cast<int>(i)*4;

            if(d.kind==ParamKind::Numeric){
                w.control=Make(TRACKBAR_CLASSW,L"",TBS_HORZ|TBS_NOTICKS,
                    270,contentHeight_,390,30,baseId);
                const int scale=static_cast<int>(std::llround(1.0/d.step));
                w.sliderScale=std::max(1,scale);
                SendMessageW(w.control,TBM_SETRANGEMIN,FALSE,
                    static_cast<LPARAM>(std::llround(d.minValue*w.sliderScale)));
                SendMessageW(w.control,TBM_SETRANGEMAX,FALSE,
                    static_cast<LPARAM>(std::llround(d.maxValue*w.sliderScale)));
                w.valueLabel=Make(L"STATIC",L"",SS_CENTER,
                    670,contentHeight_+4,80,24,baseId+1);
                if(d.autoAllowed){
                    w.autoCheck=Make(L"BUTTON",L"Auto",BS_AUTOCHECKBOX,
                        765,contentHeight_+1,80,26,baseId+2);
                }
            } else if(d.kind==ParamKind::Bool){
                w.control=Make(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL,
                    270,contentHeight_,250,180,baseId);
                if(d.autoAllowed) SendMessageW(w.control,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Auto"));
                SendMessageW(w.control,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Off"));
                SendMessageW(w.control,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"On"));
            } else if(d.kind==ParamKind::Enum){
                w.control=Make(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL,
                    270,contentHeight_,300,220,baseId);
                if(d.autoAllowed){
                    SendMessageW(w.control,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Auto"));
                    w.enumValues.push_back(L"auto");
                }
                std::wstring choices=d.choices?d.choices:L"";
                size_t pos=0;
                while(pos<choices.size()){
                    size_t semi=choices.find(L';',pos);
                    std::wstring item=choices.substr(pos,semi==std::wstring::npos?std::wstring::npos:semi-pos);
                    size_t eq=item.find(L'=');
                    std::wstring label=eq==std::wstring::npos?item:item.substr(0,eq);
                    std::wstring value=eq==std::wstring::npos?item:item.substr(eq+1);
                    SendMessageW(w.control,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
                    w.enumValues.push_back(value);
                    if(semi==std::wstring::npos) break;
                    pos=semi+1;
                }
            } else {
                w.control=Make(L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL,
                    270,contentHeight_,180,26,baseId);
                if(d.autoAllowed){
                    w.autoCheck=Make(L"BUTTON",L"Auto",BS_AUTOCHECKBOX,
                        465,contentHeight_+1,80,26,baseId+2);
                }
                Make(L"STATIC",L"Hex or decimal VK code",SS_LEFT,
                    560,contentHeight_+4,210,24);
            }

            widgets_.push_back(std::move(w));
            contentHeight_+=38;
        }

        contentHeight_+=12;
        Make(L"BUTTON",L"Save",BS_DEFPUSHBUTTON,20,contentHeight_,100,32,kIdSave);
        Make(L"BUTTON",L"Reload",BS_PUSHBUTTON,130,contentHeight_,100,32,kIdReload);
        Make(L"BUTTON",L"Pack defaults",BS_PUSHBUTTON,240,contentHeight_,120,32,kIdDefaults);
        Make(L"BUTTON",L"Close",BS_PUSHBUTTON,370,contentHeight_,100,32,kIdClose);
        Make(L"STATIC",
             L"Most settings require restarting the mirror. Sliders write exact INI values; Auto returns control to OptiScaler.",
             SS_LEFT,500,contentHeight_,360,42);
        contentHeight_+=60;

        SCROLLINFO si{sizeof(si),SIF_RANGE|SIF_PAGE|SIF_POS};
        si.nMin=0; si.nMax=contentHeight_; si.nPage=740; si.nPos=0;
        SetScrollInfo(hwnd_,SB_VERT,&si,TRUE);
    }

    void Reload()
    {
        for(auto& w:widgets_){
            const auto& d=*w.desc;
            std::wstring value=ReadIni(ini_,d.key,d.defaultValue);
            const bool isAuto=IEquals(value,L"auto");

            if(d.kind==ParamKind::Numeric){
                if(w.autoCheck) SendMessageW(w.autoCheck,BM_SETCHECK,isAuto?BST_CHECKED:BST_UNCHECKED,0);
                double v=d.minValue;
                if(!isAuto){
                    wchar_t* end=nullptr;
                    v=wcstod(value.c_str(),&end);
                    if(end==value.c_str()) v=d.minValue;
                } else {
                    // Use documented baseline while Auto is checked, so the slider remains meaningful.
                    if(std::wstring(d.key)==L"WorkingScale") v=1.0;
                    else if(std::wstring(d.key)==L"MaxRatio") v=2.0;
                    else if(std::wstring(d.key)==L"Passes") v=1.0;
                    else if(std::wstring(d.key)==L"SkinStructure") v=-1.0;
                    else if(std::wstring(d.key)==L"ResidualAcrossRRBlend") v=0.08;
                    else v=1.0;
                }
                v=std::clamp(v,d.minValue,d.maxValue);
                SendMessageW(w.control,TBM_SETPOS,TRUE,
                    static_cast<LPARAM>(std::llround(v*w.sliderScale)));
                UpdateNumeric(w);
            } else if(d.kind==ParamKind::Bool){
                int sel=0;
                if(d.autoAllowed){
                    sel=isAuto?0:(IEquals(value,L"true")||value==L"1"?2:1);
                } else {
                    sel=(IEquals(value,L"true")||value==L"1")?1:0;
                }
                SendMessageW(w.control,CB_SETCURSEL,sel,0);
            } else if(d.kind==ParamKind::Enum){
                int sel=0;
                for(size_t i=0;i<w.enumValues.size();++i){
                    if(IEquals(value,w.enumValues[i].c_str())){ sel=static_cast<int>(i); break; }
                }
                SendMessageW(w.control,CB_SETCURSEL,sel,0);
            } else {
                if(w.autoCheck) SendMessageW(w.autoCheck,BM_SETCHECK,isAuto?BST_CHECKED:BST_UNCHECKED,0);
                SetWindowTextW(w.control,isAuto?L"0x2D":value.c_str());
            }
        }
    }

    void Defaults()
    {
        for(auto& w:widgets_){
            const auto& d=*w.desc;
            if(d.autoAllowed){
                if(w.autoCheck) SendMessageW(w.autoCheck,BM_SETCHECK,BST_CHECKED,0);
                if(d.kind==ParamKind::Bool||d.kind==ParamKind::Enum)
                    SendMessageW(w.control,CB_SETCURSEL,0,0);
            } else {
                if(d.kind==ParamKind::Bool){
                    SendMessageW(w.control,CB_SETCURSEL,
                        IEquals(d.defaultValue,L"true")?1:0,0);
                } else if(d.kind==ParamKind::Numeric){
                    double v=wcstod(d.defaultValue,nullptr);
                    SendMessageW(w.control,TBM_SETPOS,TRUE,
                        static_cast<LPARAM>(std::llround(v*w.sliderScale)));
                    UpdateNumeric(w);
                }
            }
        }
    }

    void UpdateNumeric(Widget& w)
    {
        const int pos=static_cast<int>(SendMessageW(w.control,TBM_GETPOS,0,0));
        const double v=static_cast<double>(pos)/w.sliderScale;
        const std::wstring value=FormatNumber(v,w.desc->step);
        if(w.valueLabel) SetWindowTextW(w.valueLabel,value.c_str());
        if(w.autoCheck) SendMessageW(w.autoCheck,BM_SETCHECK,BST_UNCHECKED,0);
    }

    void Save()
    {
        for(auto& w:widgets_){
            const auto& d=*w.desc;
            std::wstring value;

            if(d.kind==ParamKind::Numeric){
                if(w.autoCheck&&SendMessageW(w.autoCheck,BM_GETCHECK,0,0)==BST_CHECKED){
                    value=L"auto";
                } else {
                    const int pos=static_cast<int>(SendMessageW(w.control,TBM_GETPOS,0,0));
                    value=FormatNumber(static_cast<double>(pos)/w.sliderScale,d.step);
                }
            } else if(d.kind==ParamKind::Bool){
                const int sel=static_cast<int>(SendMessageW(w.control,CB_GETCURSEL,0,0));
                if(d.autoAllowed) value=sel==0?L"auto":(sel==2?L"true":L"false");
                else value=sel==1?L"true":L"false";
            } else if(d.kind==ParamKind::Enum){
                const int sel=static_cast<int>(SendMessageW(w.control,CB_GETCURSEL,0,0));
                if(sel>=0&&static_cast<size_t>(sel)<w.enumValues.size()) value=w.enumValues[sel];
                else value=d.defaultValue;
            } else {
                if(w.autoCheck&&SendMessageW(w.autoCheck,BM_GETCHECK,0,0)==BST_CHECKED){
                    value=L"auto";
                } else {
                    wchar_t buffer[64]{};
                    GetWindowTextW(w.control,buffer,64);
                    value=buffer;
                }
            }

            WritePrivateProfileStringW(L"DlssNr",d.key,value.c_str(),ini_.c_str());
        }

        MessageBoxW(
            hwnd_,
            L"DLSS-NR settings saved. Restart the mirror for settings that rebuild the neural feature.\n\n"
            L"Changing Preset, Style, WorkingScale or pass count can significantly affect performance.",
            L"Neural Rendering Tuning",
            MB_OK|MB_ICONINFORMATION);
    }

    void ScrollTo(int pos)
    {
        SCROLLINFO si{sizeof(si),SIF_ALL};
        GetScrollInfo(hwnd_,SB_VERT,&si);
        pos=std::clamp(pos,si.nMin,std::max(si.nMin,si.nMax-static_cast<int>(si.nPage)+1));
        const int delta=scrollPos_-pos;
        if(delta){
            ScrollWindowEx(hwnd_,0,delta,nullptr,nullptr,nullptr,nullptr,SW_SCROLLCHILDREN|SW_INVALIDATE);
            scrollPos_=pos;
            si.fMask=SIF_POS; si.nPos=pos;
            SetScrollInfo(hwnd_,SB_VERT,&si,TRUE);
        }
    }

    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp)
    {
        switch(msg){
            case WM_HSCROLL: {
                HWND control=reinterpret_cast<HWND>(lp);
                for(auto& w:widgets_){
                    if(w.control==control&&w.desc->kind==ParamKind::Numeric){
                        UpdateNumeric(w);
                        return 0;
                    }
                }
                break;
            }
            case WM_COMMAND:
                switch(LOWORD(wp)){
                    case kIdSave: Save(); return 0;
                    case kIdReload: Reload(); return 0;
                    case kIdDefaults: Defaults(); return 0;
                    case kIdClose: DestroyWindow(hwnd_); return 0;
                    default: break;
                }
                break;
            case WM_VSCROLL: {
                SCROLLINFO si{sizeof(si),SIF_ALL};
                GetScrollInfo(hwnd_,SB_VERT,&si);
                int pos=scrollPos_;
                switch(LOWORD(wp)){
                    case SB_LINEUP: pos-=30; break;
                    case SB_LINEDOWN: pos+=30; break;
                    case SB_PAGEUP: pos-=static_cast<int>(si.nPage); break;
                    case SB_PAGEDOWN: pos+=static_cast<int>(si.nPage); break;
                    case SB_THUMBTRACK: pos=si.nTrackPos; break;
                    case SB_TOP: pos=si.nMin; break;
                    case SB_BOTTOM: pos=si.nMax; break;
                    default: break;
                }
                ScrollTo(pos); return 0;
            }
            case WM_MOUSEWHEEL:
                ScrollTo(scrollPos_-GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA*90);
                return 0;
            case WM_CLOSE:
                DestroyWindow(hwnd_); return 0;
            case WM_NCDESTROY:
                hwnd_=nullptr;
                if(font_) DeleteObject(font_);
                for(HFONT f:groupFonts_) DeleteObject(f);
                delete this;
                return 0;
        }
        return DefWindowProcW(hwnd_,msg,wp,lp);
    }

    HWND owner_{};
    HWND hwnd_{};
    HFONT font_{};
    std::filesystem::path ini_;
    std::vector<Widget> widgets_;
    std::vector<HFONT> groupFonts_;
    int contentHeight_{};
    int scrollPos_{};
};

} // namespace

void NrTuningDialog::Show(HWND owner, const std::filesystem::path& iniPath)
{
    (new DialogImpl(owner,iniPath))->Show();
}
