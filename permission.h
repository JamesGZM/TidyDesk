// SPDX-License-Identifier: MIT
#pragma once
#include <windows.h>
#include <sddl.h>
#include <string>
#include <vector>
namespace permission {
inline constexpr wchar_t Service[]=L"TidyDesk.SystemSettings";
inline constexpr wchar_t Pipe[]=L"\\\\.\\pipe\\TidyDesk.SystemSettings.v1";
inline constexpr wchar_t Key[]=L"SOFTWARE\\TidyDesk\\SystemSettings";
enum class Operation : DWORD { Ping=1, Hide=2, Restore=3, Disable=4 };
struct Request { DWORD magic=0x54445331; Operation operation=Operation::Ping; };
struct Reply { DWORD magic=0x54445331; DWORD result=ERROR_INVALID_DATA; };
inline bool Valid(const Request& r){return r.magic==0x54445331&&r.operation>=Operation::Ping&&r.operation<=Operation::Disable;}
struct Handle { HANDLE value=nullptr; explicit Handle(HANDLE h=nullptr):value(h){} ~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);} Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete; };
struct ServiceHandle { SC_HANDLE value=nullptr; explicit ServiceHandle(SC_HANDLE h):value(h){} ~ServiceHandle(){if(value)CloseServiceHandle(value);} ServiceHandle(const ServiceHandle&)=delete;ServiceHandle& operator=(const ServiceHandle&)=delete; };
inline std::wstring TokenSid(HANDLE token){DWORD size=0;GetTokenInformation(token,TokenUser,nullptr,0,&size);if(!size)return {};std::vector<BYTE> bytes(size);if(!GetTokenInformation(token,TokenUser,bytes.data(),size,&size))return {};LPWSTR text=nullptr;if(!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(bytes.data())->User.Sid,&text))return {};std::wstring result=text;LocalFree(text);return result;}
inline std::wstring CurrentSid(){HANDLE raw=nullptr;if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&raw))return {};Handle token(raw);return TokenSid(token.value);}
inline std::wstring OwnerSid(){wchar_t text[256]{};DWORD size=sizeof(text);if(RegGetValueW(HKEY_LOCAL_MACHINE,Key,L"Owner",RRF_RT_REG_SZ|RRF_SUBKEY_WOW6464KEY,nullptr,text,&size))return {};return text;}
enum class State { Off, Ready, Unavailable, OtherUser, Unknown };
inline State ReadState(){ServiceHandle manager(OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT));if(!manager.value)return State::Unknown;ServiceHandle service(OpenServiceW(manager.value,Service,SERVICE_QUERY_STATUS));if(!service.value)return GetLastError()==ERROR_SERVICE_DOES_NOT_EXIST?State::Off:State::Unknown;auto owner=OwnerSid(),current=CurrentSid();if(owner.empty()||current.empty())return State::Unknown;if(owner!=current)return State::OtherUser;SERVICE_STATUS_PROCESS status{};DWORD bytes=0;if(!QueryServiceStatusEx(service.value,SC_STATUS_PROCESS_INFO,reinterpret_cast<BYTE*>(&status),sizeof(status),&bytes))return State::Unknown;return status.dwCurrentState==SERVICE_RUNNING?State::Ready:State::Unavailable;}
inline DWORD WaitGone(){for(int i=0;i<100;++i){if(ReadState()==State::Off)return ERROR_SUCCESS;Sleep(100);}return ERROR_SERVICE_REQUEST_TIMEOUT;}
// All waits are bounded; cancellation is drained before OVERLAPPED storage dies.
inline bool Complete(HANDLE pipe,OVERLAPPED& ov,DWORD& count,DWORD timeout,HANDLE stop=nullptr){HANDLE events[]={ov.hEvent,stop};auto wait=WaitForMultipleObjects(stop?2:1,events,FALSE,timeout);if(wait!=WAIT_OBJECT_0){CancelIoEx(pipe,&ov);GetOverlappedResult(pipe,&ov,&count,TRUE);SetLastError(wait==WAIT_TIMEOUT?ERROR_TIMEOUT:ERROR_OPERATION_ABORTED);return false;}return GetOverlappedResult(pipe,&ov,&count,FALSE)!=FALSE;}
inline bool Transfer(HANDLE pipe,void* data,DWORD bytes,bool write,HANDLE stop=nullptr){Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));if(!event.value)return false;OVERLAPPED ov{};ov.hEvent=event.value;DWORD count=0;BOOL ok=write?WriteFile(pipe,data,bytes,&count,&ov):ReadFile(pipe,data,bytes,&count,&ov);if(!ok&&(GetLastError()!=ERROR_IO_PENDING||!Complete(pipe,ov,count,10000,stop)))return false;if(count!=bytes){SetLastError(ERROR_INVALID_DATA);return false;}return true;}
inline DWORD Call(Operation operation){ServiceHandle manager(OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT));if(!manager.value)return GetLastError();ServiceHandle service(OpenServiceW(manager.value,Service,SERVICE_QUERY_STATUS));if(!service.value)return GetLastError();if(!WaitNamedPipeW(Pipe,3000))return GetLastError();Handle pipe(CreateFileW(Pipe,FILE_GENERIC_READ|FILE_WRITE_DATA|FILE_WRITE_ATTRIBUTES,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED|SECURITY_SQOS_PRESENT|SECURITY_IDENTIFICATION,nullptr));if(pipe.value==INVALID_HANDLE_VALUE)return GetLastError();SERVICE_STATUS_PROCESS status{};DWORD bytes=0;ULONG pid=0;if(!GetNamedPipeServerProcessId(pipe.value,&pid)||!QueryServiceStatusEx(service.value,SC_STATUS_PROCESS_INFO,reinterpret_cast<BYTE*>(&status),sizeof(status),&bytes)||status.dwCurrentState!=SERVICE_RUNNING||!pid||pid!=status.dwProcessId)return ERROR_ACCESS_DENIED;Request request;request.operation=operation;Reply reply;if(!Transfer(pipe.value,&request,sizeof(request),true)||!Transfer(pipe.value,&reply,sizeof(reply),false))return GetLastError();DWORD ack=reply.magic;Transfer(pipe.value,&ack,sizeof(ack),true);return reply.magic==request.magic?reply.result:ERROR_INVALID_DATA;}
}
