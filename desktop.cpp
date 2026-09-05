// SPDX-License-Identifier: MIT
#include "desktop_model.h"
#include <commctrl.h>
#include <commdlg.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wrl/client.h>
#include <thread>
#include <atomic>
#include <memory>
#include <algorithm>
#include <fstream>
using Microsoft::WRL::ComPtr;
namespace {
constexpr UINT Refresh=WM_APP+2, Loaded=WM_APP+3;
struct Item {std::wstring path,name;HICON icon=nullptr;};
struct Batch {std::vector<Item> items;~Batch(){for(auto& i:items)if(i.icon)DestroyIcon(i.icon);}};
struct Frame;
std::vector<std::unique_ptr<Frame>> frames;
HWND controller=nullptr,desktop=nullptr;
UINT shellMessage=0;
bool rebuilding=false;
unsigned interaction=0; struct Interact { Interact(){++interaction;} ~Interact(){--interaction;} };
COLORREF Ink(COLORREF c){return GetRValue(c)*299+GetGValue(c)*587+GetBValue(c)*114>145000?RGB(20,30,35):RGB(240,245,248);}
void Persist();
struct Frame {
 desk::Box box;HWND window=nullptr,list=nullptr;HIMAGELIST images=nullptr;std::vector<std::wstring> paths;
 HANDLE cancel=CreateEventW(nullptr,TRUE,FALSE,nullptr);std::thread watcher,loader;std::atomic<bool> busy{false};bool again=false;unsigned generation=0;
 ~Frame(){SetEvent(cancel);if(watcher.joinable())watcher.join();if(loader.joinable())loader.join();MSG m{};while(PeekMessageW(&m,window,Loaded,Loaded,PM_REMOVE))delete reinterpret_cast<Batch*>(m.lParam);if(window)DestroyWindow(window);if(images)ImageList_Destroy(images);CloseHandle(cancel);}
 void Scan(){if(busy.exchange(true)){again=true;return;}if(loader.joinable())loader.join();auto path=box.path;auto target=window;auto stop=cancel;
 loader=std::thread([this,path,target,stop](){CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);auto batch=std::make_unique<Batch>();std::error_code ec;
 for(const auto& entry:std::filesystem::directory_iterator(path,ec)){if(WaitForSingleObject(stop,0)==WAIT_OBJECT_0)break;auto attrs=GetFileAttributesW(entry.path().c_str());if(attrs==INVALID_FILE_ATTRIBUTES||(attrs&(FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_SYSTEM)))continue;
 Item i;i.path=entry.path().wstring();i.name=entry.path().filename().wstring();SHFILEINFOW info{};
 // Bounded icon cache; very large folders retain names and generic icons.
 if(batch->items.size()<512&&SHGetFileInfoW(i.path.c_str(),0,&info,sizeof(info),SHGFI_ICON|SHGFI_LARGEICON))i.icon=info.hIcon;
 batch->items.push_back(std::move(i));}
 if(WaitForSingleObject(stop,0)!=WAIT_OBJECT_0&&PostMessageW(target,Loaded,0,reinterpret_cast<LPARAM>(batch.get())))batch.release();CoUninitialize();});}
 void Watch(){auto path=box.path;watcher=std::thread([this,path](){HANDLE change=FindFirstChangeNotificationW(path.c_str(),FALSE,FILE_NOTIFY_CHANGE_FILE_NAME|FILE_NOTIFY_CHANGE_DIR_NAME|FILE_NOTIFY_CHANGE_LAST_WRITE|FILE_NOTIFY_CHANGE_ATTRIBUTES);if(change==INVALID_HANDLE_VALUE)return;HANDLE handles[]={cancel,change};while(WaitForMultipleObjects(2,handles,FALSE,INFINITE)==WAIT_OBJECT_0+1){PostMessageW(window,Refresh,0,0);if(!FindNextChangeNotification(change))break;}FindCloseChangeNotification(change);});}
};
void Persist(){if(rebuilding)return;std::vector<desk::Box> boxes;for(auto& f:frames)boxes.push_back(f->box);if(!desk::Save(boxes))MessageBoxW(controller,L"布局保存失败，文件未被删除。请检查数据目录。",L"TidyDesk",MB_OK|MB_ICONWARNING);}
std::vector<std::wstring> DropPaths(IDataObject* data){std::vector<std::wstring> paths;FORMATETC fmt{CF_HDROP,nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};STGMEDIUM medium{};if(SUCCEEDED(data->GetData(&fmt,&medium))){auto drop=static_cast<HDROP>(medium.hGlobal);UINT count=DragQueryFileW(drop,0xffffffff,nullptr,0);for(UINT i=0;i<count;++i){UINT len=DragQueryFileW(drop,i,nullptr,0);std::wstring p(len+1,L'\0');DragQueryFileW(drop,i,p.data(),len+1);p.resize(len);paths.push_back(p);}ReleaseStgMedium(&medium);}return paths;}
class DropTarget final:public IDropTarget,public IDropSource {
 LONG refs=1;Frame* f;bool accepted=false,linkOnly=false;
public:
 explicit DropTarget(Frame* value):f(value){}
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override {if(!out)return E_POINTER;*out=nullptr;if(id==IID_IUnknown||id==IID_IDropTarget)*out=static_cast<IDropTarget*>(this);else if(id==IID_IDropSource)*out=static_cast<IDropSource*>(this);else return E_NOINTERFACE;AddRef();return S_OK;}
 ULONG STDMETHODCALLTYPE AddRef() override{return static_cast<ULONG>(InterlockedIncrement(&refs));}
 ULONG STDMETHODCALLTYPE Release() override{auto r=InterlockedDecrement(&refs);if(!r)delete this;return static_cast<ULONG>(r);}
 HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* d,DWORD,POINTL,DWORD* effect) override{auto p=DropPaths(d);accepted=!p.empty();linkOnly=accepted;for(auto& path:p)if(_wcsicmp(std::filesystem::path(path).extension().c_str(),L".exe")!=0)linkOnly=false;*effect=accepted?(*effect&(linkOnly?DROPEFFECT_LINK:DROPEFFECT_MOVE)):DROPEFFECT_NONE;SetWindowTextW(f->window,linkOnly?L"创建快捷方式":L"移动到分类文件夹");return S_OK;}
 HRESULT STDMETHODCALLTYPE DragOver(DWORD,POINTL,DWORD* effect) override{*effect=accepted?(*effect&(linkOnly?DROPEFFECT_LINK:DROPEFFECT_MOVE)):DROPEFFECT_NONE;return S_OK;}
 HRESULT STDMETHODCALLTYPE DragLeave() override{SetWindowTextW(f->window,f->box.name.c_str());return S_OK;}
 HRESULT STDMETHODCALLTYPE Drop(IDataObject* data,DWORD,POINTL point,DWORD* effect) override{
 Interact guard;auto paths=DropPaths(data);*effect=DROPEFFECT_NONE;bool local=!paths.empty();for(auto& p:paths)if(_wcsicmp(std::filesystem::path(p).parent_path().c_str(),f->box.path.c_str()))local=false;
 if(local){POINT pos{point.x,point.y};ScreenToClient(f->list,&pos);LVHITTESTINFO hit{};hit.pt=pos;int index=ListView_HitTest(f->list,&hit);if(index<0)index=static_cast<int>(f->paths.size());auto order=f->paths;for(const auto& p:paths){auto it=std::find(order.begin(),order.end(),p);if(it!=order.end())order.erase(it);}index=(std::min)(index,static_cast<int>(order.size()));order.insert(order.begin()+index,paths.begin(),paths.end());f->box.order.clear();for(auto& p:order)f->box.order.push_back(std::filesystem::path(p).filename().wstring());Persist();f->Scan();}
 else{HRESULT hr=desk::Transfer(f->window,paths,f->box.path);if(FAILED(hr)&&hr!=HRESULT_FROM_WIN32(ERROR_CANCELLED)){wchar_t text[160]{};swprintf_s(text,L"操作未完成（0x%08X）。请检查文件权限或占用状态。",static_cast<unsigned>(hr));MessageBoxW(f->window,text,L"TidyDesk",MB_OK|MB_ICONWARNING);}f->Scan();}
 // Operations are performed here, so the source must never delete data again.
 DragLeave();return S_OK;}
 HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escape,DWORD keys) override{return escape?DRAGDROP_S_CANCEL:!(keys&MK_LBUTTON)?DRAGDROP_S_DROP:S_OK;}
 HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override{return DRAGDROP_S_USEDEFAULTCURSORS;}
};
void DragOut(Frame* f){Interact guard;std::vector<std::wstring> selected;for(int i=ListView_GetNextItem(f->list,-1,LVNI_SELECTED);i>=0;i=ListView_GetNextItem(f->list,i,LVNI_SELECTED))if(static_cast<size_t>(i)<f->paths.size())selected.push_back(f->paths[static_cast<size_t>(i)]);if(selected.empty())return;
 PIDLIST_ABSOLUTE parent=ILCreateFromPathW(f->box.path.c_str());std::vector<PIDLIST_ABSOLUTE> full;std::vector<PCUITEMID_CHILD> children;for(auto& p:selected){auto id=ILCreateFromPathW(p.c_str());if(id){full.push_back(id);children.push_back(ILFindLastID(id));}}
 ComPtr<IDataObject> data;if(parent&&!children.empty()&&SUCCEEDED(SHCreateDataObject(parent,static_cast<UINT>(children.size()),children.data(),nullptr,IID_PPV_ARGS(&data)))){auto source=new DropTarget(f);DWORD effect=0;DoDragDrop(data.Get(),source,DROPEFFECT_MOVE|DROPEFFECT_COPY|DROPEFFECT_LINK,&effect);source->Release();}for(auto p:full)ILFree(p);ILFree(parent);f->Scan();}
void Menu(Frame* f,POINT p){Interact guard;int selected=ListView_GetNextItem(f->list,-1,LVNI_SELECTED);HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,1,L"打开分类文件夹");if(selected>=0){AppendMenuW(menu,MF_STRING,10,L"打开所选项目");AppendMenuW(menu,MF_STRING,11,L"移回桌面");}
 AppendMenuW(menu,MF_STRING,2,f->box.collapsed?L"展开":L"折叠");AppendMenuW(menu,MF_STRING,3,f->box.locked?L"解锁位置":L"锁定位置");AppendMenuW(menu,MF_STRING,4,L"图标：小 / 中 / 大");AppendMenuW(menu,MF_STRING,5,L"背景：浅 / 中 / 深");AppendMenuW(menu,MF_STRING,6,L"移除收纳框（保留文件）");AppendMenuW(menu,MF_STRING,7,L"背景颜色…");SetForegroundWindow(f->window);int cmd=TrackPopupMenu(menu,TPM_RETURNCMD,p.x,p.y,0,f->window,nullptr);DestroyMenu(menu);
 if(cmd==1)ShellExecuteW(f->window,L"open",f->box.path.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
 else if(cmd==10&&selected>=0)ShellExecuteW(f->window,L"open",f->paths[static_cast<size_t>(selected)].c_str(),nullptr,nullptr,SW_SHOWNORMAL);
 else if(cmd==11){PWSTR dest=nullptr;if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop,0,nullptr,&dest))){std::vector<std::wstring> paths;for(int i=ListView_GetNextItem(f->list,-1,LVNI_SELECTED);i>=0;i=ListView_GetNextItem(f->list,i,LVNI_SELECTED))paths.push_back(f->paths[static_cast<size_t>(i)]);auto hr=desk::Transfer(f->window,paths,dest);CoTaskMemFree(dest);if(FAILED(hr)&&hr!=HRESULT_FROM_WIN32(ERROR_CANCELLED))MessageBoxW(f->window,L"移回桌面未完成。请检查权限或文件占用。",L"TidyDesk",MB_OK);}}
 else if(cmd==2){f->box.collapsed=!f->box.collapsed;SetWindowPos(f->window,nullptr,0,0,f->box.w,f->box.collapsed?40:f->box.h,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);Persist();}
 else if(cmd==3){f->box.locked=!f->box.locked;Persist();}
 else if(cmd==4){f->box.size=f->box.size==32?48:f->box.size==48?64:32;Persist();f->Scan();}
 else if(cmd==5){f->box.alpha=f->box.alpha==235?190:f->box.alpha==190?255:235;SetLayeredWindowAttributes(f->window,0,static_cast<BYTE>(f->box.alpha),LWA_ALPHA);Persist();}
 else if(cmd==7){COLORREF custom[16]{};CHOOSECOLORW choose{sizeof(choose)};choose.hwndOwner=f->window;choose.rgbResult=f->box.color;choose.lpCustColors=custom;choose.Flags=CC_FULLOPEN|CC_RGBINIT;if(ChooseColorW(&choose)){f->box.color=choose.rgbResult;ListView_SetBkColor(f->list,f->box.color);ListView_SetTextColor(f->list,Ink(f->box.color));InvalidateRect(f->window,nullptr,TRUE);Persist();}}else if(cmd==6){auto boxes=desk::Load();boxes.erase(std::remove_if(boxes.begin(),boxes.end(),[&](const desk::Box& b){return b.id==f->box.id;}),boxes.end());if(desk::Save(boxes))desk::Notify();}}
LRESULT CALLBACK ListProc(HWND w,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data){auto f=reinterpret_cast<Frame*>(data);if(msg==WM_CONTEXTMENU){POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};if(p.x==-1)GetCursorPos(&p);Menu(f,p);return 0;}return DefSubclassProc(w,msg,wp,lp);}
LRESULT CALLBACK FrameProc(HWND w,UINT msg,WPARAM wp,LPARAM lp){auto f=reinterpret_cast<Frame*>(GetWindowLongPtrW(w,GWLP_USERDATA));if(msg==WM_NCCREATE){f=static_cast<Frame*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);f->window=w;SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(f));}if(!f)return DefWindowProcW(w,msg,wp,lp);
 switch(msg){case WM_CREATE:{f->list=CreateWindowExW(0,WC_LISTVIEWW,L"分类文件",WS_CHILD|WS_VISIBLE|WS_TABSTOP|LVS_ICON|LVS_AUTOARRANGE|LVS_SHOWSELALWAYS,8,36,f->box.w-16,f->box.h-44,w,reinterpret_cast<HMENU>(1),GetModuleHandleW(nullptr),nullptr);ListView_SetBkColor(f->list,f->box.color);ListView_SetTextBkColor(f->list,CLR_NONE);ListView_SetTextColor(f->list,Ink(f->box.color));ListView_SetExtendedListViewStyle(f->list,LVS_EX_DOUBLEBUFFER);SetWindowSubclass(f->list,ListProc,1,reinterpret_cast<DWORD_PTR>(f));auto target=new DropTarget(f);RegisterDragDrop(f->list,target);RegisterDragDrop(w,target);target->Release();SetLayeredWindowAttributes(w,0,static_cast<BYTE>(f->box.alpha),LWA_ALPHA);return 0;}
 case WM_ERASEBKGND:{RECT r{};GetClientRect(w,&r);auto brush=CreateSolidBrush(f->box.color);FillRect(reinterpret_cast<HDC>(wp),&r,brush);DeleteObject(brush);return 1;}
 case WM_PAINT:{PAINTSTRUCT ps{};auto dc=BeginPaint(w,&ps);RECT r{12,6,f->box.w-12,32};SetBkMode(dc,TRANSPARENT);SetTextColor(dc,Ink(f->box.color));auto old=SelectObject(dc,GetStockObject(DEFAULT_GUI_FONT));wchar_t title[256]{};GetWindowTextW(w,title,256);DrawTextW(dc,title,-1,&r,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);SelectObject(dc,old);EndPaint(w,&ps);return 0;}
 case WM_SETTEXT:{auto ret=DefWindowProcW(w,msg,wp,lp);InvalidateRect(w,nullptr,TRUE);return ret;}
 case WM_NCHITTEST:{POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(w,&p);RECT r{};GetClientRect(w,&r);if(!f->box.locked){if(!f->box.collapsed&&p.x>r.right-14&&p.y>r.bottom-14)return HTBOTTOMRIGHT;if(p.y<34)return HTCAPTION;}return HTCLIENT;}
 case WM_GETMINMAXINFO:{auto info=reinterpret_cast<MINMAXINFO*>(lp);info->ptMinTrackSize={220,f->box.collapsed?40:120};return 0;}
 case WM_SIZE:{RECT r{};GetClientRect(w,&r);MoveWindow(f->list,8,36,(std::max)(1L,r.right-16),(std::max)(1L,r.bottom-44),TRUE);ShowWindow(f->list,f->box.collapsed?SW_HIDE:SW_SHOW);return 0;}
 case WM_EXITSIZEMOVE:{RECT r{};GetWindowRect(w,&r);f->box.x=r.left;f->box.y=r.top;f->box.w=r.right-r.left;if(!f->box.collapsed)f->box.h=r.bottom-r.top;Persist();return 0;}
 case WM_CONTEXTMENU:{POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};if(p.x==-1)GetCursorPos(&p);Menu(f,p);return 0;}
 case WM_NOTIFY:{auto n=reinterpret_cast<NMHDR*>(lp);if(n->code==LVN_BEGINDRAG){DragOut(f);return 0;}if(n->code==NM_DBLCLK){auto a=reinterpret_cast<NMITEMACTIVATE*>(lp);if(a->iItem>=0&&static_cast<size_t>(a->iItem)<f->paths.size())ShellExecuteW(w,L"open",f->paths[static_cast<size_t>(a->iItem)].c_str(),nullptr,nullptr,SW_SHOWNORMAL);}return 0;}
 case Refresh:SetTimer(w,1,180,nullptr);return 0;
 case WM_TIMER:KillTimer(w,1);f->Scan();return 0;
 case Loaded:{std::unique_ptr<Batch> batch(reinterpret_cast<Batch*>(lp));if(f->loader.joinable())f->loader.join();f->busy=false;
 auto rank=[&](const Item& i){auto it=std::find(f->box.order.begin(),f->box.order.end(),i.name);return static_cast<size_t>(it-f->box.order.begin());};std::stable_sort(batch->items.begin(),batch->items.end(),[&](const Item&a,const Item&b){return rank(a)<rank(b);});
 SendMessageW(f->list,WM_SETREDRAW,FALSE,0);ListView_DeleteAllItems(f->list);f->paths.clear();auto icons=ImageList_Create(static_cast<int>(f->box.size),static_cast<int>(f->box.size),ILC_COLOR32|ILC_MASK,16,16);ListView_SetImageList(f->list,icons,LVSIL_NORMAL);if(f->images)ImageList_Destroy(f->images);f->images=icons;
 for(auto& i:batch->items){LVITEMW item{};item.mask=LVIF_TEXT|LVIF_IMAGE;item.iItem=static_cast<int>(f->paths.size());item.pszText=i.name.data();item.iImage=i.icon?ImageList_AddIcon(icons,i.icon):-1;ListView_InsertItem(f->list,&item);f->paths.push_back(i.path);}SendMessageW(f->list,WM_SETREDRAW,TRUE,0);InvalidateRect(f->list,nullptr,TRUE);if(f->again){f->again=false;f->Scan();}return 0;}
 case WM_CLOSE:ShowWindow(w,SW_HIDE);return 0;
 case WM_DESTROY:RevokeDragDrop(f->list);RevokeDragDrop(w);return 0;
 }return DefWindowProcW(w,msg,wp,lp);}
BOOL CALLBACK FindDesktop(HWND w,LPARAM p){if(FindWindowExW(w,nullptr,L"SHELLDLL_DefView",nullptr)){*reinterpret_cast<HWND*>(p)=w;return FALSE;}return TRUE;}
void Rebuild(){if(interaction){SetTimer(controller,1,250,nullptr);return;}rebuilding=true;frames.clear();desktop=nullptr;EnumWindows(FindDesktop,reinterpret_cast<LPARAM>(&desktop));if(!desktop){rebuilding=false;return;}
 for(auto& box:desk::Load()){auto f=std::make_unique<Frame>();f->box=box;RECT r{box.x,box.y,box.x+box.w,box.y+box.h};MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromRect(&r,MONITOR_DEFAULTTONEAREST),&monitor);f->box.x=static_cast<int>(std::clamp<LONG>(box.x,monitor.rcWork.left,(std::max)(monitor.rcWork.left,monitor.rcWork.right-box.w)));f->box.y=static_cast<int>(std::clamp<LONG>(box.y,monitor.rcWork.top,(std::max)(monitor.rcWork.top,monitor.rcWork.bottom-box.h)));POINT p{f->box.x,f->box.y};ScreenToClient(desktop,&p);
 CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_LAYERED,L"TidyDesk.Desktop.Frame",box.name.c_str(),WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN,p.x,p.y,box.w,box.collapsed?40:box.h,desktop,nullptr,GetModuleHandleW(nullptr),f.get());if(f->window){f->Scan();f->Watch();frames.push_back(std::move(f));}}
 rebuilding=false;}
LRESULT CALLBACK Controller(HWND w,UINT m,WPARAM wp,LPARAM lp){if(m==shellMessage&&shellMessage){SetTimer(w,1,1500,nullptr);return 0;}switch(m){case WM_APP+1:Rebuild();return 0;case WM_DISPLAYCHANGE:case WM_DPICHANGED:SetTimer(w,1,400,nullptr);return 0;case WM_TIMER:KillTimer(w,1);Rebuild();return 0;case WM_CLOSE:DestroyWindow(w);return 0;case WM_DESTROY:rebuilding=true;frames.clear();PostQuitMessage(0);return 0;}return DefWindowProcW(w,m,wp,lp);}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR command,int){if(wcscmp(command,L"--test-model")==0)return desk::ModelTest();if(wcscmp(command,L"--test-transfer")==0)return desk::TransferTest();if(*command)return 2;HANDLE single=CreateMutexW(nullptr,FALSE,L"Local\\TidyDesk.Desktop.Singleton");if(!single)return 3;if(GetLastError()==ERROR_ALREADY_EXISTS){CloseHandle(single);return 0;}if(FAILED(OleInitialize(nullptr))){CloseHandle(single);return 4;}INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_LISTVIEW_CLASSES};InitCommonControlsEx(&controls);WNDCLASSW wc{};wc.hInstance=instance;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.lpfnWndProc=FrameProc;wc.lpszClassName=L"TidyDesk.Desktop.Frame";RegisterClassW(&wc);wc.lpfnWndProc=Controller;wc.lpszClassName=L"TidyDesk.Desktop.Controller";RegisterClassW(&wc);controller=CreateWindowExW(WS_EX_TOOLWINDOW,wc.lpszClassName,L"TidyDesk desktop",WS_POPUP,0,0,0,0,nullptr,nullptr,instance,nullptr);shellMessage=RegisterWindowMessageW(L"TaskbarCreated");Rebuild();MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}OleUninitialize();CloseHandle(single);return 0;}
