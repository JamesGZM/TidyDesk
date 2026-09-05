// SPDX-License-Identifier: MIT
#include "desktop_model.h"
#include "desktop_geometry.h"
#include <commctrl.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <gdiplus.h>
#include <wrl/client.h>
#include <thread>
#include <atomic>
#include <memory>
#include <set>
#include <algorithm>
using Microsoft::WRL::ComPtr;
using namespace Gdiplus;
namespace {
constexpr UINT Refresh=WM_APP+2, Loaded=WM_APP+3, NewCollection=WM_APP+5;
constexpr wchar_t FrameClass[]=L"TidyDesk.Desktop.Frame";
struct Item {std::wstring path,name;HICON icon=nullptr;};
struct Batch {std::vector<Item> items;~Batch(){for(auto& i:items)if(i.icon)DestroyIcon(i.icon);}};
struct Frame;
std::vector<std::unique_ptr<Frame>> frames;
HWND controller=nullptr,desktop=nullptr,shade=nullptr;
Frame* expanded=nullptr;
UINT shellMessage=0;
bool rebuilding=false;
unsigned interaction=0;
struct Interact {Interact(){++interaction;}~Interact(){--interaction;}};
void Paint(Frame* f);
void Expand(Frame* f,bool value);
void Persist();
void Rebuild();
struct Frame {
 desk::Box box;HWND window=nullptr;std::unique_ptr<Batch> content=std::make_unique<Batch>();
 std::set<int> selected;int scroll=0;bool hover=false,buttonHover=false,dropHover=false,large=false,pressed=false,dragging=false;
 POINT press{};RECT saved{};ULONGLONG entered=0;int wheel=0;
 ComPtr<IContextMenu2> menu2;ComPtr<IContextMenu3> menu3;
 HANDLE cancel=CreateEventW(nullptr,TRUE,FALSE,nullptr);std::thread watcher,loader;std::atomic<bool> busy{false};bool again=false;
 ~Frame(){SetEvent(cancel);if(watcher.joinable())watcher.join();if(loader.joinable())loader.join();
  if(window){MSG m{};while(PeekMessageW(&m,window,Loaded,Loaded,PM_REMOVE))delete reinterpret_cast<Batch*>(m.lParam);if(IsWindow(window))DestroyWindow(window);}CloseHandle(cancel);}
 int D(int v)const{return MulDiv(v,static_cast<int>(GetDpiForWindow(window)),96);}
 desk::Grid Grid()const{RECT r{};GetClientRect(window,&r);auto g=desk::Grid::Make(MulDiv(r.right,96,static_cast<int>(GetDpiForWindow(window))),MulDiv(r.bottom,96,static_cast<int>(GetDpiForWindow(window))),static_cast<int>(box.size));g.cellWidth=D(g.cellWidth);g.cellHeight=D(g.cellHeight);g.top=D(g.top);g.left=D(g.left);return g;}
 int Hit(POINT p)const{return box.collapsed?-1:Grid().Hit(p.x,p.y,scroll,static_cast<int>(content->items.size()));}
 RECT Button()const{RECT r{};GetClientRect(window,&r);return {r.right-D(42),D(16),r.right-D(10),D(44)};}
 void Scan(){if(busy.exchange(true)){again=true;return;}if(loader.joinable())loader.join();auto path=box.path;auto target=window;auto stop=cancel;
  loader=std::thread([path,target,stop](){CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);auto batch=std::make_unique<Batch>();std::error_code ec;
   for(const auto& entry:std::filesystem::directory_iterator(path,ec)){if(WaitForSingleObject(stop,0)==WAIT_OBJECT_0)break;auto attrs=GetFileAttributesW(entry.path().c_str());if(attrs==INVALID_FILE_ATTRIBUTES||(attrs&(FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_SYSTEM)))continue;
    Item i;i.path=entry.path().wstring();i.name=entry.path().filename().wstring();SHFILEINFOW info{};
    if(batch->items.size()<512&&SHGetFileInfoW(i.path.c_str(),0,&info,sizeof(info),SHGFI_ICON|SHGFI_LARGEICON))i.icon=info.hIcon;
    batch->items.push_back(std::move(i));}
   if(WaitForSingleObject(stop,0)!=WAIT_OBJECT_0&&PostMessageW(target,Loaded,0,reinterpret_cast<LPARAM>(batch.get())))batch.release();CoUninitialize();});}
 void Watch(){auto path=box.path;watcher=std::thread([this,path](){HANDLE change=FindFirstChangeNotificationW(path.c_str(),FALSE,FILE_NOTIFY_CHANGE_FILE_NAME|FILE_NOTIFY_CHANGE_DIR_NAME|FILE_NOTIFY_CHANGE_LAST_WRITE|FILE_NOTIFY_CHANGE_ATTRIBUTES);if(change==INVALID_HANDLE_VALUE)return;HANDLE handles[]={cancel,change};while(WaitForMultipleObjects(2,handles,FALSE,INFINITE)==WAIT_OBJECT_0+1){PostMessageW(window,Refresh,0,0);if(!FindNextChangeNotification(change))break;}FindCloseChangeNotification(change);});}
};
void Persist(){if(rebuilding)return;std::vector<desk::Box> boxes;for(auto& f:frames)boxes.push_back(f->box);if(!desk::Save(boxes))MessageBoxW(controller,L"布局保存失败。文件仍保留在原文件夹。",L"TidyDesk",MB_OK|MB_ICONWARNING);}
void Rounded(GraphicsPath& p,REAL x,REAL y,REAL w,REAL h,REAL radius){REAL d=radius*2;p.AddArc(x,y,d,d,180,90);p.AddArc(x+w-d,y,d,d,270,90);p.AddArc(x+w-d,y+h-d,d,d,0,90);p.AddArc(x,y+h-d,d,d,90,90);p.CloseFigure();}
void Text(Graphics& g,const std::wstring& text,RectF r,REAL size,bool symbol=false){
 FontFamily family(symbol?L"Segoe MDL2 Assets":L"Microsoft YaHei UI");StringFormat format;format.SetAlignment(StringAlignmentCenter);format.SetLineAlignment(StringAlignmentCenter);format.SetTrimming(StringTrimmingEllipsisCharacter);
 GraphicsPath path;path.AddString(text.c_str(),-1,&family,FontStyleRegular,size,r,&format);
 Pen outline(Color(150,15,20,30),2.0f);outline.SetLineJoin(LineJoinRound);SolidBrush ink(Color(245,245,247,250));g.DrawPath(&outline,&path);g.FillPath(&ink,&path);
}
void Paint(Frame* f){if(!f->window||!IsWindow(f->window))return;RECT r{};GetClientRect(f->window,&r);int width=r.right,height=r.bottom;if(width<1||height<1)return;
 HDC screen=GetDC(nullptr),dc=CreateCompatibleDC(screen);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;void* pixels=nullptr;
 HBITMAP dib=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&pixels,nullptr,0);if(!dib){DeleteDC(dc);ReleaseDC(nullptr,screen);return;}auto old=SelectObject(dc,dib);
 {Bitmap bitmap(width,height,width*4,PixelFormat32bppPARGB,static_cast<BYTE*>(pixels));Graphics g(&bitmap);g.Clear(Color(0,0,0,0));g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
  REAL scale=static_cast<REAL>(f->D(100))/100.0f;GraphicsPath border;Rounded(border,3*scale,13*scale,static_cast<REAL>(width)-6*scale,static_cast<REAL>(height)-16*scale,16*scale);
  // A 1/255 alpha hit surface keeps empty space droppable without an opaque panel.
  SolidBrush hit(Color(1,0,0,0));g.FillPath(&hit,&border);
  REAL titleWidth=(std::min)(static_cast<REAL>(width)-110*scale,static_cast<REAL>(f->box.name.size())*17*scale+24*scale);
  auto clip=g.Save();g.ExcludeClip(RectF((static_cast<REAL>(width)-titleWidth)/2,0,titleWidth,25*scale));Pen line(Color(f->dropHover?200:f->hover||f->large?135:100,220,230,239),scale);g.DrawPath(&line,&border);g.Restore(clip);
  Text(g,f->box.name,RectF((static_cast<REAL>(width)-titleWidth)/2,0,titleWidth,26*scale),16*scale);
  if(f->hover||f->large||f->box.collapsed){RECT b=f->Button();POINT cursor{};GetCursorPos(&cursor);ScreenToClient(f->window,&cursor);if(PtInRect(&b,cursor)){GraphicsPath button;Rounded(button,static_cast<REAL>(b.left),static_cast<REAL>(b.top),static_cast<REAL>(b.right-b.left),static_cast<REAL>(b.bottom-b.top),6*scale);SolidBrush fill(Color(40,240,245,250));g.FillPath(&fill,&button);}Text(g,f->large?L"\xE73F":L"\xE740",RectF(static_cast<REAL>(b.left),static_cast<REAL>(b.top),static_cast<REAL>(b.right-b.left),static_cast<REAL>(b.bottom-b.top)),16*scale,true);}
  if(!f->box.collapsed){auto grid=f->Grid();f->scroll=std::clamp(f->scroll,0,grid.MaxScroll(static_cast<int>(f->content->items.size())));g.SetClip(Rect(8,f->D(46),width-16,(std::max)(1,height-f->D(54))));
   for(int row=0;row<grid.rows;++row)for(int col=0;col<grid.columns;++col){int index=(row+f->scroll)*grid.columns+col;if(index>=static_cast<int>(f->content->items.size()))break;auto& item=f->content->items[static_cast<size_t>(index)];int x=grid.left+col*grid.cellWidth,y=grid.top+row*grid.cellHeight;
    if(f->selected.count(index)){GraphicsPath selection;Rounded(selection,static_cast<REAL>(x),static_cast<REAL>(y-2),static_cast<REAL>(grid.cellWidth-4),static_cast<REAL>(grid.cellHeight-5),5*scale);SolidBrush fill(Color(55,190,215,240));g.FillPath(&fill,&selection);}
    if(item.icon){Bitmap icon(item.icon);int size=f->D(static_cast<int>(f->box.size));g.DrawImage(&icon,x+(grid.cellWidth-size)/2,y,size,size);}
    Text(g,item.name,RectF(static_cast<REAL>(x+2),static_cast<REAL>(y+f->D(static_cast<int>(f->box.size))+4),static_cast<REAL>(grid.cellWidth-8),34*scale),12*scale);
   }
  }
 }
 POINT origin{};SIZE size{width,height};BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};UpdateLayeredWindow(f->window,screen,nullptr,&size,dc,&origin,0,&blend,ULW_ALPHA);SelectObject(dc,old);DeleteObject(dib);DeleteDC(dc);ReleaseDC(nullptr,screen);
}
void HideShade(){if(shade){DestroyWindow(shade);shade=nullptr;}}
LRESULT CALLBACK ShadeProc(HWND w,UINT m,WPARAM wp,LPARAM lp){if(m==WM_LBUTTONDOWN||m==WM_RBUTTONDOWN||m==WM_KEYDOWN){if(expanded)Expand(expanded,false);return 0;}if(m==WM_ERASEBKGND){RECT r{};GetClientRect(w,&r);FillRect(reinterpret_cast<HDC>(wp),&r,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));return 1;}return DefWindowProcW(w,m,wp,lp);}
void Expand(Frame* f,bool value){if(value==f->large)return;if(value&&expanded)Expand(expanded,false);
 if(value){GetWindowRect(f->window,&f->saved);f->large=true;expanded=f;f->box.collapsed=false;MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(f->window,MONITOR_DEFAULTTONEAREST),&monitor);auto area=monitor.rcWork;
  int width=(std::min)(f->D(940),static_cast<int>(area.right-area.left)-f->D(48)),height=(std::min)(f->D(540),static_cast<int>(area.bottom-area.top)-f->D(64));int x=area.left+(area.right-area.left-width)/2,y=area.top+(area.bottom-area.top-height)/2;
  shade=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_LAYERED,L"TidyDesk.Desktop.Shade",L"",WS_POPUP,area.left,area.top,area.right-area.left,area.bottom-area.top,controller,nullptr,GetModuleHandleW(nullptr),nullptr);
  if(shade){SetLayeredWindowAttributes(shade,0,60,LWA_ALPHA);HRGN region=CreateRectRgn(0,0,area.right-area.left,area.bottom-area.top);HRGN hole=CreateRoundRectRgn(x-area.left,y-area.top,x-area.left+width,y-area.top+height,f->D(32),f->D(32));CombineRgn(region,region,hole,RGN_DIFF);DeleteObject(hole);if(!SetWindowRgn(shade,region,FALSE))DeleteObject(region);ShowWindow(shade,SW_SHOWNOACTIVATE);}
  ShowWindow(f->window,SW_HIDE);SetParent(f->window,nullptr);SetWindowLongPtrW(f->window,GWL_STYLE,WS_POPUP|WS_VISIBLE);SetWindowLongPtrW(f->window,GWLP_HWNDPARENT,reinterpret_cast<LONG_PTR>(controller));SetWindowPos(f->window,HWND_TOP,x,y,width,height,SWP_FRAMECHANGED|SWP_SHOWWINDOW);SetForegroundWindow(f->window);SetFocus(f->window);
 }else{HideShade();expanded=nullptr;f->large=false;ShowWindow(f->window,SW_HIDE);SetWindowLongPtrW(f->window,GWLP_HWNDPARENT,0);SetWindowLongPtrW(f->window,GWL_STYLE,WS_CHILD|WS_VISIBLE|WS_THICKFRAME);SetParent(f->window,desktop);POINT p{f->box.x,f->box.y};ScreenToClient(desktop,&p);SetWindowPos(f->window,HWND_TOP,p.x,p.y,f->box.w,f->box.h,SWP_NOACTIVATE|SWP_FRAMECHANGED|SWP_SHOWWINDOW);}
 Paint(f);
}
std::vector<std::wstring> DropPaths(IDataObject* data){std::vector<std::wstring> paths;FORMATETC fmt{CF_HDROP,nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};STGMEDIUM medium{};if(SUCCEEDED(data->GetData(&fmt,&medium))){auto drop=static_cast<HDROP>(medium.hGlobal);UINT count=DragQueryFileW(drop,0xffffffff,nullptr,0);for(UINT i=0;i<count;++i){UINT len=DragQueryFileW(drop,i,nullptr,0);std::wstring p(len+1,L'\0');DragQueryFileW(drop,i,p.data(),len+1);p.resize(len);paths.push_back(p);}ReleaseStgMedium(&medium);}return paths;}
void Failure(HWND w,HRESULT hr){if(FAILED(hr)&&hr!=HRESULT_FROM_WIN32(ERROR_CANCELLED)){wchar_t text[160]{};swprintf_s(text,L"操作未完成（0x%08X）。文件未被静默覆盖。",static_cast<unsigned>(hr));MessageBoxW(w,text,L"TidyDesk",MB_OK|MB_ICONWARNING);}}
class DropTarget final:public IDropTarget,public IDropSource {
 LONG refs=1;Frame* f;bool accepted=false,linkOnly=false,enteredInteraction=false;
 void Leave(){f->dropHover=false;f->entered=0;if(enteredInteraction){--interaction;enteredInteraction=false;}Paint(f);}
public:
 explicit DropTarget(Frame* value):f(value){}
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override{if(!out)return E_POINTER;*out=nullptr;if(id==IID_IUnknown||id==IID_IDropTarget)*out=static_cast<IDropTarget*>(this);else if(id==IID_IDropSource)*out=static_cast<IDropSource*>(this);else return E_NOINTERFACE;AddRef();return S_OK;}
 ULONG STDMETHODCALLTYPE AddRef() override{return static_cast<ULONG>(InterlockedIncrement(&refs));}
 ULONG STDMETHODCALLTYPE Release() override{auto r=InterlockedDecrement(&refs);if(!r)delete this;return static_cast<ULONG>(r);}
 HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* d,DWORD,POINTL,DWORD* effect) override{auto paths=DropPaths(d);accepted=!paths.empty();linkOnly=accepted;for(auto& p:paths)if(_wcsicmp(std::filesystem::path(p).extension().c_str(),L".exe"))linkOnly=false;if(!enteredInteraction){++interaction;enteredInteraction=true;}f->dropHover=accepted;f->entered=GetTickCount64();*effect=accepted?(linkOnly?DROPEFFECT_LINK:DROPEFFECT_MOVE):DROPEFFECT_NONE;Paint(f);return S_OK;}
 HRESULT STDMETHODCALLTYPE DragOver(DWORD,POINTL,DWORD* effect) override{*effect=accepted?(linkOnly?DROPEFFECT_LINK:DROPEFFECT_MOVE):DROPEFFECT_NONE;if(accepted&&!f->large&&!f->dragging&&GetTickCount64()-f->entered>650)Expand(f,true);return S_OK;}
 HRESULT STDMETHODCALLTYPE DragLeave() override{Leave();return S_OK;}
 HRESULT STDMETHODCALLTYPE Drop(IDataObject* data,DWORD,POINTL point,DWORD* effect) override{Interact guard;auto paths=DropPaths(data);*effect=DROPEFFECT_NONE;bool local=!paths.empty();for(auto& p:paths)if(_wcsicmp(std::filesystem::path(p).parent_path().c_str(),f->box.path.c_str()))local=false;
  if(local){POINT pos{point.x,point.y};ScreenToClient(f->window,&pos);int index=f->Hit(pos);std::vector<std::wstring> order;for(auto& item:f->content->items)order.push_back(item.path);if(index<0)index=static_cast<int>(order.size());for(auto& p:paths){auto it=std::find(order.begin(),order.end(),p);if(it!=order.end()){if(it-order.begin()<index)--index;order.erase(it);}}index=std::clamp(index,0,static_cast<int>(order.size()));order.insert(order.begin()+index,paths.begin(),paths.end());f->box.order.clear();for(auto& p:order)f->box.order.push_back(std::filesystem::path(p).filename().wstring());Persist();}
  else Failure(f->window,desk::Transfer(f->window,paths,f->box.path));Leave();f->Scan();return S_OK;}
 HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escape,DWORD keys) override{return escape?DRAGDROP_S_CANCEL:!(keys&MK_LBUTTON)?DRAGDROP_S_DROP:S_OK;}
 HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override{return DRAGDROP_S_USEDEFAULTCURSORS;}
};
std::vector<std::wstring> Selected(Frame* f){std::vector<std::wstring> paths;for(int i:f->selected)if(i>=0&&i<static_cast<int>(f->content->items.size()))paths.push_back(f->content->items[static_cast<size_t>(i)].path);return paths;}
void DragOut(Frame* f){Interact guard;auto selected=Selected(f);if(selected.empty())return;f->dragging=true;HideShade();
 auto parent=ILCreateFromPathW(f->box.path.c_str());std::vector<PIDLIST_ABSOLUTE> full;std::vector<PCUITEMID_CHILD> children;for(auto& p:selected){auto id=ILCreateFromPathW(p.c_str());if(id){full.push_back(id);children.push_back(ILFindLastID(id));}}
 ComPtr<IDataObject> data;if(parent&&!children.empty()&&SUCCEEDED(SHCreateDataObject(parent,static_cast<UINT>(children.size()),children.data(),nullptr,IID_PPV_ARGS(&data)))){auto source=new DropTarget(f);DWORD effect=0;DoDragDrop(data.Get(),source,DROPEFFECT_MOVE|DROPEFFECT_COPY|DROPEFFECT_LINK,&effect);source->Release();}for(auto p:full)ILFree(p);ILFree(parent);f->dragging=false;if(f->large)Expand(f,false);f->Scan();}
void FileMenu(Frame* f,POINT point){Interact guard;auto paths=Selected(f);if(paths.empty())return;ComPtr<IShellFolder> folder;PCUITEMID_CHILD child=nullptr;auto first=ILCreateFromPathW(paths[0].c_str());if(!first)return;
 if(SUCCEEDED(SHBindToParent(first,IID_PPV_ARGS(&folder),&child))){std::vector<PIDLIST_ABSOLUTE> full;std::vector<PCUITEMID_CHILD> ids;for(auto& path:paths){auto id=ILCreateFromPathW(path.c_str());if(id){full.push_back(id);ids.push_back(ILFindLastID(id));}}ComPtr<IContextMenu> menu;
  if(!ids.empty()&&SUCCEEDED(folder->GetUIObjectOf(f->window,static_cast<UINT>(ids.size()),ids.data(),IID_IContextMenu,nullptr,reinterpret_cast<void**>(menu.GetAddressOf())))){menu.As(&f->menu2);menu.As(&f->menu3);auto popup=CreatePopupMenu();menu->QueryContextMenu(popup,0,1,0x7fff,CMF_NORMAL);int cmd=TrackPopupMenu(popup,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,f->window,nullptr);if(cmd){CMINVOKECOMMANDINFOEX invoke{};invoke.cbSize=sizeof(invoke);invoke.hwnd=f->window;invoke.fMask=CMIC_MASK_UNICODE;invoke.lpVerb=MAKEINTRESOURCEA(cmd-1);invoke.lpVerbW=MAKEINTRESOURCEW(cmd-1);invoke.nShow=SW_SHOWNORMAL;Failure(f->window,menu->InvokeCommand(reinterpret_cast<CMINVOKECOMMANDINFO*>(&invoke)));}DestroyMenu(popup);f->menu2.Reset();f->menu3.Reset();}
  for(auto id:full)ILFree(id);
 }ILFree(first);f->Scan();}
struct NameDialog {HWND edit=nullptr;bool done=false,ok=false;std::wstring text;};
LRESULT CALLBACK NameProc(HWND w,UINT m,WPARAM wp,LPARAM lp){auto state=reinterpret_cast<NameDialog*>(GetWindowLongPtrW(w,GWLP_USERDATA));if(m==WM_NCCREATE){state=static_cast<NameDialog*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(state));}if(!state)return DefWindowProcW(w,m,wp,lp);if(m==WM_COMMAND&&(LOWORD(wp)==IDOK||LOWORD(wp)==IDCANCEL)){if(LOWORD(wp)==IDOK){wchar_t text[128]{};GetWindowTextW(state->edit,text,128);if(!*text)return 0;state->text=text;state->ok=true;}state->done=true;return 0;}if(m==WM_CLOSE){state->done=true;return 0;}return DefWindowProcW(w,m,wp,lp);}
bool Rename(HWND owner,std::wstring& name){Interact guard;NameDialog state;state.text=name;RECT r{};GetWindowRect(owner,&r);HWND w=CreateWindowExW(WS_EX_DLGMODALFRAME,L"TidyDesk.Desktop.Name",L"收纳框名称",WS_POPUP|WS_CAPTION|WS_SYSMENU,r.left+20,r.top+20,340,145,owner,nullptr,GetModuleHandleW(nullptr),&state);if(!w)return false;
 state.edit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",name.c_str(),WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,16,16,292,28,w,reinterpret_cast<HMENU>(10),nullptr,nullptr);SendMessageW(state.edit,EM_SETLIMITTEXT,80,0);SendMessageW(state.edit,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);CreateWindowW(L"BUTTON",L"确定",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON,144,58,78,28,w,reinterpret_cast<HMENU>(IDOK),nullptr,nullptr);CreateWindowW(L"BUTTON",L"取消",WS_CHILD|WS_VISIBLE|WS_TABSTOP,230,58,78,28,w,reinterpret_cast<HMENU>(IDCANCEL),nullptr,nullptr);EnableWindow(owner,FALSE);ShowWindow(w,SW_SHOW);SetForegroundWindow(w);SetFocus(state.edit);SendMessageW(state.edit,EM_SETSEL,0,-1);
 MSG msg{};while(!state.done&&GetMessageW(&msg,nullptr,0,0)>0){if(msg.message==WM_KEYDOWN&&(msg.wParam==VK_RETURN||msg.wParam==VK_ESCAPE)){SendMessageW(w,WM_COMMAND,msg.wParam==VK_RETURN?IDOK:IDCANCEL,0);continue;}if(!IsDialogMessageW(w,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}EnableWindow(owner,TRUE);DestroyWindow(w);if(state.ok)name=state.text;return state.ok;}
void Dissolve(Frame* f){TASKDIALOG_BUTTON buttons[]={{100,L"将内容移回桌面"},{101,L"保留在原文件夹"}};TASKDIALOGCONFIG config{sizeof(config)};config.hwndParent=f->window;config.dwFlags=TDF_USE_COMMAND_LINKS;config.dwCommonButtons=TDCBF_CANCEL_BUTTON;config.pszWindowTitle=L"解散收纳框";config.pszMainInstruction=L"如何处理收纳框中的文件？";config.pszContent=L"不会删除文件。移回桌面时，重名和取消由 Windows 处理。";config.cButtons=2;config.pButtons=buttons;int choice=0;if(FAILED(TaskDialogIndirect(&config,&choice,nullptr,nullptr))||choice==IDCANCEL)return;
 if(choice==100){PWSTR path=nullptr;if(FAILED(SHGetKnownFolderPath(FOLDERID_Desktop,0,nullptr,&path)))return;std::vector<std::wstring> all;std::error_code ec;for(auto& entry:std::filesystem::directory_iterator(f->box.path,ec))all.push_back(entry.path().wstring());if(ec){CoTaskMemFree(path);return;}auto hr=desk::Transfer(f->window,all,path);CoTaskMemFree(path);if(FAILED(hr)){Failure(f->window,hr);f->Scan();return;}}
 if(f->large)Expand(f,false);auto boxes=desk::Load();boxes.erase(std::remove_if(boxes.begin(),boxes.end(),[&](const desk::Box& box){return box.id==f->box.id;}),boxes.end());if(desk::Save(boxes))desk::Notify();}
void Menu(Frame* f,POINT p){Interact guard;auto menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,1,L"重命名");AppendMenuW(menu,MF_STRING,2,f->large?L"收起":L"展开");AppendMenuW(menu,MF_STRING,3,f->box.locked?L"解锁位置":L"锁定位置");auto sizes=CreatePopupMenu();for(int i=0;i<3;++i)AppendMenuW(sizes,MF_STRING|(f->box.size==static_cast<unsigned>(32+i*16)?MF_CHECKED:0),static_cast<UINT_PTR>(20+i),i==0?L"小":i==1?L"中":L"大");AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(sizes),L"图标大小");AppendMenuW(menu,MF_STRING,4,L"打开文件夹");AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,5,L"解散收纳框…");int cmd=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,f->window,nullptr);DestroyMenu(menu);
 if(cmd==1){if(Rename(f->window,f->box.name)){Persist();Paint(f);}}else if(cmd==2)Expand(f,!f->large);else if(cmd==3){f->box.locked=!f->box.locked;Persist();}else if(cmd==4)ShellExecuteW(f->window,L"open",f->box.path.c_str(),nullptr,nullptr,SW_SHOWNORMAL);else if(cmd==5)Dissolve(f);else if(cmd>=20&&cmd<=22){f->box.size=static_cast<unsigned>(32+(cmd-20)*16);Persist();Paint(f);}}
LRESULT CALLBACK FrameProc(HWND w,UINT msg,WPARAM wp,LPARAM lp){auto f=reinterpret_cast<Frame*>(GetWindowLongPtrW(w,GWLP_USERDATA));if(msg==WM_NCCREATE){f=static_cast<Frame*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);f->window=w;SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(f));}if(!f)return DefWindowProcW(w,msg,wp,lp);
 if(f->menu3&&(msg==WM_INITMENUPOPUP||msg==WM_DRAWITEM||msg==WM_MEASUREITEM||msg==WM_MENUCHAR)){LRESULT result=0;if(SUCCEEDED(f->menu3->HandleMenuMsg2(msg,wp,lp,&result)))return result;}else if(f->menu2&&(msg==WM_INITMENUPOPUP||msg==WM_DRAWITEM||msg==WM_MEASUREITEM)){f->menu2->HandleMenuMsg(msg,wp,lp);return 0;}
 switch(msg){case WM_CREATE:{auto target=new DropTarget(f);RegisterDragDrop(w,target);target->Release();return 0;}
 case WM_ERASEBKGND:return 1;
 case WM_PAINT:{PAINTSTRUCT ps{};BeginPaint(w,&ps);EndPaint(w,&ps);Paint(f);return 0;}
 case WM_NCHITTEST:{POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(w,&p);RECT r{};GetClientRect(w,&r);auto button=f->Button();if(PtInRect(&button,p))return HTCLIENT;if(!f->large&&!f->box.locked){int edge=f->D(9);bool left=p.x<edge,right=p.x>r.right-edge,top=p.y<f->D(20),bottom=p.y>r.bottom-edge;if(top&&left)return HTTOPLEFT;if(top&&right)return HTTOPRIGHT;if(bottom&&left)return HTBOTTOMLEFT;if(bottom&&right)return HTBOTTOMRIGHT;if(left)return HTLEFT;if(right)return HTRIGHT;if(bottom)return HTBOTTOM;if(top&&p.y>f->D(8)&&p.x<r.right/3)return HTTOP;if(p.y<f->D(40))return HTCAPTION;}return HTCLIENT;}
 case WM_NCCALCSIZE:return 0;
 case WM_GETMINMAXINFO:{auto info=reinterpret_cast<MINMAXINFO*>(lp);info->ptMinTrackSize={f->D(220),f->D(160)};return 0;}
 case WM_ENTERSIZEMOVE:++interaction;return 0;
 case WM_EXITSIZEMOVE:{if(interaction)--interaction;if(!f->large){RECT r{};GetWindowRect(w,&r);f->box.x=r.left;f->box.y=r.top;f->box.w=r.right-r.left;f->box.h=r.bottom-r.top;Persist();}return 0;}
 case WM_SIZE:Paint(f);return 0;
 case WM_DPICHANGED:if(!f->large){auto r=reinterpret_cast<RECT*>(lp);SetWindowPos(w,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOACTIVATE|SWP_NOZORDER);}Paint(f);return 0;
 case WM_MOUSEMOVE:{POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};auto b=f->Button();bool button=PtInRect(&b,p)!=FALSE;if(button!=f->buttonHover){f->buttonHover=button;Paint(f);}if(!f->hover){f->hover=true;TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,w,0};TrackMouseEvent(&track);Paint(f);}if(f->pressed&&(abs(p.x-f->press.x)>GetSystemMetrics(SM_CXDRAG)||abs(p.y-f->press.y)>GetSystemMetrics(SM_CYDRAG))){f->pressed=false;ReleaseCapture();DragOut(f);}return 0;}
 case WM_MOUSELEAVE:f->hover=false;Paint(f);return 0;
 case WM_LBUTTONDOWN:{SetFocus(w);POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};auto button=f->Button();if(PtInRect(&button,p)){Expand(f,!f->large);return 0;}int index=f->Hit(p);if(index>=0){if(!(GetKeyState(VK_CONTROL)&0x8000)&&!f->selected.count(index))f->selected.clear();if((GetKeyState(VK_CONTROL)&0x8000)&&f->selected.count(index))f->selected.erase(index);else f->selected.insert(index);f->press=p;f->pressed=true;SetCapture(w);}else f->selected.clear();Paint(f);return 0;}
 case WM_LBUTTONUP:f->pressed=false;if(GetCapture()==w)ReleaseCapture();return 0;
 case WM_CAPTURECHANGED:f->pressed=false;return 0;
 case WM_LBUTTONDBLCLK:{int index=f->Hit({GET_X_LPARAM(lp),GET_Y_LPARAM(lp)});if(index>=0)ShellExecuteW(w,L"open",f->content->items[static_cast<size_t>(index)].path.c_str(),nullptr,nullptr,SW_SHOWNORMAL);return 0;}
 case WM_MOUSEWHEEL:{f->wheel+=GET_WHEEL_DELTA_WPARAM(wp);int steps=f->wheel/WHEEL_DELTA;f->wheel%=WHEEL_DELTA;f->scroll=std::clamp(f->scroll-steps,0,f->Grid().MaxScroll(static_cast<int>(f->content->items.size())));Paint(f);return 0;}
 case WM_KEYDOWN:{if(wp==VK_ESCAPE){if(f->large)Expand(f,false);else{f->selected.clear();Paint(f);}}else if(wp==VK_F2){if(Rename(w,f->box.name)){Persist();Paint(f);}}else if(wp==VK_RETURN){auto paths=Selected(f);if(!paths.empty())ShellExecuteW(w,L"open",paths[0].c_str(),nullptr,nullptr,SW_SHOWNORMAL);else Expand(f,!f->large);}else if(wp=='A'&&(GetKeyState(VK_CONTROL)&0x8000)){for(int i=0;i<static_cast<int>(f->content->items.size());++i)f->selected.insert(i);Paint(f);}else if(wp==VK_LEFT||wp==VK_RIGHT||wp==VK_UP||wp==VK_DOWN){int i=f->selected.empty()?0:*f->selected.begin();i+=wp==VK_LEFT?-1:wp==VK_RIGHT?1:wp==VK_UP?-f->Grid().columns:f->Grid().columns;if(!f->content->items.empty()){i=std::clamp(i,0,static_cast<int>(f->content->items.size())-1);f->selected={i};auto grid=f->Grid();int row=i/grid.columns;if(row<f->scroll)f->scroll=row;if(row>=f->scroll+grid.rows)f->scroll=row-grid.rows+1;Paint(f);}}return 0;}
 case WM_ACTIVATE:if(LOWORD(wp)==WA_INACTIVE&&f->large&&!interaction)SetTimer(w,3,80,nullptr);return 0;
 case WM_CONTEXTMENU:{POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};if(p.x==-1){GetCursorPos(&p);if(!f->selected.empty()){FileMenu(f,p);return 0;}}POINT local=p;ScreenToClient(w,&local);int index=f->Hit(local);if(index>=0){if(!f->selected.count(index))f->selected={index};Paint(f);FileMenu(f,p);}else Menu(f,p);return 0;}
 case Refresh:SetTimer(w,1,180,nullptr);return 0;
 case WM_TIMER:KillTimer(w,wp);if(wp==1)f->Scan();else if(wp==3&&f->large&&!interaction&&GetForegroundWindow()!=w&&GetForegroundWindow()!=shade)Expand(f,false);return 0;
 case Loaded:{std::unique_ptr<Batch> batch(reinterpret_cast<Batch*>(lp));if(f->loader.joinable())f->loader.join();f->busy=false;auto rank=[&](const Item& i){auto it=std::find(f->box.order.begin(),f->box.order.end(),i.name);return static_cast<size_t>(it-f->box.order.begin());};std::stable_sort(batch->items.begin(),batch->items.end(),[&](const Item&a,const Item&b){return rank(a)<rank(b);});auto selected=Selected(f);f->selected.clear();f->content=std::move(batch);for(size_t i=0;i<f->content->items.size();++i)if(std::find(selected.begin(),selected.end(),f->content->items[i].path)!=selected.end())f->selected.insert(static_cast<int>(i));Paint(f);if(f->again){f->again=false;f->Scan();}return 0;}
 case WM_CLOSE:if(f->large)Expand(f,false);return 0;
 case WM_DESTROY:RevokeDragDrop(w);return 0;
 }return DefWindowProcW(w,msg,wp,lp);}
BOOL CALLBACK FindDesktop(HWND w,LPARAM p){if(FindWindowExW(w,nullptr,L"SHELLDLL_DefView",nullptr)){*reinterpret_cast<HWND*>(p)=w;return FALSE;}return TRUE;}
void Rebuild(){if(interaction){SetTimer(controller,1,250,nullptr);return;}rebuilding=true;HideShade();expanded=nullptr;frames.clear();desktop=nullptr;EnumWindows(FindDesktop,reinterpret_cast<LPARAM>(&desktop));if(!desktop){rebuilding=false;return;}
 for(auto& box:desk::Load()){auto f=std::make_unique<Frame>();f->box=box;f->box.collapsed=false;RECT r{box.x,box.y,box.x+box.w,box.y+box.h};MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromRect(&r,MONITOR_DEFAULTTONEAREST),&monitor);f->box.x=std::clamp<LONG>(box.x,monitor.rcWork.left,(std::max)(monitor.rcWork.left,monitor.rcWork.right-box.w));f->box.y=std::clamp<LONG>(box.y,monitor.rcWork.top,(std::max)(monitor.rcWork.top,monitor.rcWork.bottom-box.h));POINT p{f->box.x,f->box.y};ScreenToClient(desktop,&p);
  CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_LAYERED,FrameClass,box.name.c_str(),WS_CHILD|WS_VISIBLE|WS_THICKFRAME,p.x,p.y,box.w,box.h,desktop,nullptr,GetModuleHandleW(nullptr),f.get());if(f->window){Paint(f.get());f->Scan();f->Watch();frames.push_back(std::move(f));}}
 rebuilding=false;}
void NewBox(){try{auto boxes=desk::Load();if(boxes.size()>=64)return;desk::Box box;box.name=L"新建收纳框";POINT cursor{};GetCursorPos(&cursor);box.x=cursor.x;box.y=cursor.y;box.w=380;box.h=290;GUID id{};CoCreateGuid(&id);wchar_t text[40]{};StringFromGUID2(id,text,40);box.id=text;
 auto root=desk::CollectionsDir();std::filesystem::create_directories(root);auto path=root/box.name;for(int n=2;std::filesystem::exists(path);++n)path=root/(box.name+L" "+std::to_wstring(n));std::filesystem::create_directory(path);box.path=path.wstring();box.name=path.filename().wstring();boxes.push_back(box);if(!desk::Save(boxes))return;
 HKEY key=nullptr;if(RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\TidyDesk",0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)==ERROR_SUCCESS){DWORD enabled=1;RegSetValueExW(key,L"Desktop",0,REG_DWORD,reinterpret_cast<BYTE*>(&enabled),sizeof(enabled));RegCloseKey(key);}Rebuild();for(auto& f:frames)if(f->box.id==box.id){if(Rename(f->window,f->box.name)){Persist();Paint(f.get());}break;}
 }catch(...){MessageBoxW(controller,L"无法创建收纳文件夹，请检查文档目录的权限。",L"TidyDesk",MB_OK|MB_ICONWARNING);}}
LRESULT CALLBACK Controller(HWND w,UINT m,WPARAM wp,LPARAM lp){if(m==shellMessage&&shellMessage){SetTimer(w,1,1500,nullptr);return 0;}switch(m){case WM_APP+1:Rebuild();return 0;case WM_APP+4:return wp<frames.size()?static_cast<LRESULT>(frames[wp]->content->items.size()+1):0;case WM_APP+6:if(wp<frames.size())Expand(frames[wp].get(),true);return 0;case NewCollection:NewBox();return 0;case WM_DISPLAYCHANGE:case WM_DPICHANGED:SetTimer(w,1,400,nullptr);return 0;case WM_TIMER:if(wp==2){if(!interaction){KillTimer(w,2);DestroyWindow(w);}return 0;}KillTimer(w,1);Rebuild();return 0;case WM_CLOSE:if(interaction){SetTimer(w,2,250,nullptr);return 0;}DestroyWindow(w);return 0;case WM_DESTROY:rebuilding=true;HideShade();expanded=nullptr;frames.clear();PostQuitMessage(0);return 0;}return DefWindowProcW(w,m,wp,lp);}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR command,int){
 if(wcscmp(command,L"--test-model")==0)return desk::ModelTest();if(wcscmp(command,L"--test-transfer")==0)return desk::TransferTest();if(wcscmp(command,L"--test-geometry")==0)return desk::GeometryTest();bool create=wcscmp(command,L"--new")==0;if(*command&&!create)return 2;
 HANDLE single=CreateMutexW(nullptr,FALSE,L"Local\\TidyDesk.Desktop.Singleton");if(!single)return 3;if(GetLastError()==ERROR_ALREADY_EXISTS){if(create){auto existing=FindWindowW(L"TidyDesk.Desktop.Controller",nullptr);if(existing)PostMessageW(existing,NewCollection,0,0);}CloseHandle(single);return 0;}
 if(FAILED(OleInitialize(nullptr))){CloseHandle(single);return 4;}ULONG_PTR token=0;GdiplusStartupInput input;if(GdiplusStartup(&token,&input,nullptr)!=Ok){OleUninitialize();CloseHandle(single);return 5;}INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
 WNDCLASSW wc{};wc.hInstance=instance;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.style=CS_DBLCLKS;wc.lpfnWndProc=FrameProc;wc.lpszClassName=FrameClass;RegisterClassW(&wc);wc.lpfnWndProc=ShadeProc;wc.lpszClassName=L"TidyDesk.Desktop.Shade";RegisterClassW(&wc);wc.lpfnWndProc=NameProc;wc.lpszClassName=L"TidyDesk.Desktop.Name";wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);RegisterClassW(&wc);wc.lpfnWndProc=Controller;wc.lpszClassName=L"TidyDesk.Desktop.Controller";RegisterClassW(&wc);
 controller=CreateWindowExW(WS_EX_TOOLWINDOW,wc.lpszClassName,L"TidyDesk desktop",WS_POPUP,0,0,0,0,nullptr,nullptr,instance,nullptr);shellMessage=RegisterWindowMessageW(L"TaskbarCreated");Rebuild();if(create)PostMessageW(controller,NewCollection,0,0);MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}GdiplusShutdown(token);OleUninitialize();CloseHandle(single);return 0;
}
