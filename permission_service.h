// SPDX-License-Identifier: MIT
#pragma once
#include "permission.h"
#include <aclapi.h>
#include <shlobj.h>
#include <filesystem>
namespace permission {
inline HANDLE stopEvent=nullptr;
inline SERVICE_STATUS_HANDLE statusHandle=nullptr;
inline DWORD (*applyArrow)(bool)=nullptr;
inline void Report(DWORD state,DWORD error=0){SERVICE_STATUS status{};status.dwServiceType=SERVICE_WIN32_OWN_PROCESS;status.dwCurrentState=state;status.dwControlsAccepted=state==SERVICE_RUNNING?SERVICE_ACCEPT_STOP|SERVICE_ACCEPT_SHUTDOWN:0;status.dwWin32ExitCode=error;SetServiceStatus(statusHandle,&status);}
inline DWORD WINAPI Control(DWORD code,DWORD,LPVOID,LPVOID){if((code==SERVICE_CONTROL_STOP||code==SERVICE_CONTROL_SHUTDOWN)&&stopEvent)SetEvent(stopEvent);return ERROR_SUCCESS;}
inline DWORD DeleteRegistration(){ServiceHandle manager(OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT));if(!manager.value)return GetLastError();ServiceHandle service(OpenServiceW(manager.value,Service,DELETE));if(!service.value)return GetLastError()==ERROR_SERVICE_DOES_NOT_EXIST?ERROR_SUCCESS:GetLastError();if(!DeleteService(service.value)&&GetLastError()!=ERROR_SERVICE_MARKED_FOR_DELETE)return GetLastError();return ERROR_SUCCESS;}
inline bool Authorized(HANDLE pipe,const std::wstring& owner){if(!ImpersonateNamedPipeClient(pipe))return false;HANDLE raw=nullptr;std::wstring sid;if(OpenThreadToken(GetCurrentThread(),TOKEN_QUERY,TRUE,&raw)){Handle token(raw);sid=TokenSid(token.value);}if(!RevertToSelf())ExitProcess(ERROR_ACCESS_DENIED);return !owner.empty()&&owner==sid;}
inline void WINAPI ServiceMain(DWORD,LPWSTR*){
 statusHandle=RegisterServiceCtrlHandlerExW(Service,Control,nullptr);if(!statusHandle)return;
 Handle stop(CreateEventW(nullptr,TRUE,FALSE,nullptr));stopEvent=stop.value;if(!stopEvent){Report(SERVICE_STOPPED,GetLastError());return;}
 auto owner=OwnerSid();PSID parsed=nullptr;if(owner.empty()||!ConvertStringSidToSidW(owner.c_str(),&parsed)){Report(SERVICE_STOPPED,ERROR_INVALID_OWNER);stopEvent=nullptr;return;}LocalFree(parsed);
 // Do not give the client FILE_CREATE_PIPE_INSTANCE (contained in GENERIC_WRITE).
 auto sddl=L"D:P(A;;GA;;;SY)(A;;0x0012019B;;;"+owner+L")";PSECURITY_DESCRIPTOR sd=nullptr;if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),SDDL_REVISION_1,&sd,nullptr)){Report(SERVICE_STOPPED,GetLastError());stopEvent=nullptr;return;}
 SECURITY_ATTRIBUTES sa{sizeof(sa),sd,FALSE};Handle pipe(CreateNamedPipeW(Pipe,PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,PIPE_TYPE_MESSAGE|PIPE_READMODE_MESSAGE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,sizeof(Reply),sizeof(Request),0,&sa));LocalFree(sd);if(pipe.value==INVALID_HANDLE_VALUE){Report(SERVICE_STOPPED,GetLastError());stopEvent=nullptr;return;}Report(SERVICE_RUNNING);
 while(WaitForSingleObject(stopEvent,0)==WAIT_TIMEOUT){Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));if(!event.value)break;OVERLAPPED ov{};ov.hEvent=event.value;DWORD count=0;BOOL connected=ConnectNamedPipe(pipe.value,&ov);auto error=connected?ERROR_SUCCESS:GetLastError();if(!connected&&error==ERROR_IO_PENDING)connected=Complete(pipe.value,ov,count,INFINITE,stopEvent);else if(error==ERROR_PIPE_CONNECTED)connected=TRUE;if(!connected)break;
  Request request;Reply reply;bool disable=false;if(Transfer(pipe.value,&request,sizeof(request),false,stopEvent)){
   if(!Authorized(pipe.value,owner))reply.result=ERROR_ACCESS_DENIED;
   else if(!Valid(request))reply.result=ERROR_INVALID_PARAMETER;
   else if(request.operation==Operation::Ping)reply.result=ERROR_SUCCESS;
   else if(request.operation==Operation::Disable){reply.result=DeleteRegistration();disable=reply.result==ERROR_SUCCESS;}
   else reply.result=applyArrow(request.operation==Operation::Restore);
   if(Transfer(pipe.value,&reply,sizeof(reply),true,stopEvent)){DWORD ack=0;Transfer(pipe.value,&ack,sizeof(ack),false,stopEvent);}
  }
  DisconnectNamedPipe(pipe.value);if(disable)SetEvent(stopEvent);
 }
 Report(SERVICE_STOPPED);stopEvent=nullptr;
}
inline DWORD Run(DWORD (*apply)(bool)){applyArrow=apply;SERVICE_TABLE_ENTRYW table[]={{const_cast<LPWSTR>(Service),ServiceMain},{nullptr,nullptr}};return StartServiceCtrlDispatcherW(table)?ERROR_SUCCESS:GetLastError();}
inline std::filesystem::path SecureDirectory(){PWSTR path=nullptr;if(FAILED(SHGetKnownFolderPath(FOLDERID_ProgramFiles,0,nullptr,&path)))return {};std::filesystem::path dir=std::filesystem::path(path)/L"TidyDesk System Settings";CoTaskMemFree(path);return dir;}
inline bool ProtectedDirectory(const std::filesystem::path& dir){
 PSECURITY_DESCRIPTOR expected=nullptr;const wchar_t* descriptor=L"O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;0x1200a9;;;BU)";
 if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(descriptor,SDDL_REVISION_1,&expected,nullptr))return false;
 SECURITY_ATTRIBUTES sa{sizeof(sa),expected,FALSE};bool ok=CreateDirectoryW(dir.c_str(),&sa)!=FALSE;if(!ok&&GetLastError()==ERROR_ALREADY_EXISTS){auto attr=GetFileAttributesW(dir.c_str());if(attr!=INVALID_FILE_ATTRIBUTES&&(attr&FILE_ATTRIBUTE_DIRECTORY)&&!(attr&FILE_ATTRIBUTE_REPARSE_POINT)){PSECURITY_DESCRIPTOR actual=nullptr;PSID owner=nullptr;PACL acl=nullptr;if(GetNamedSecurityInfoW(dir.c_str(),SE_FILE_OBJECT,OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,&owner,nullptr,&acl,nullptr,&actual)==ERROR_SUCCESS){PSID wanted=nullptr;PACL wantedAcl=nullptr;BOOL ignored=FALSE,present=FALSE;GetSecurityDescriptorOwner(expected,&wanted,&ignored);GetSecurityDescriptorDacl(expected,&present,&wantedAcl,&ignored);ok=owner&&acl&&EqualSid(owner,wanted)&&acl->AclSize==wantedAcl->AclSize&&memcmp(acl,wantedAcl,acl->AclSize)==0;LocalFree(actual);}}}
 LocalFree(expected);return ok;
}
inline DWORD CopyProtectedExecutable(const wchar_t* source,const std::filesystem::path& target){
 PSECURITY_DESCRIPTOR sd=nullptr;if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;0x1200a9;;;BU)",SDDL_REVISION_1,&sd,nullptr))return GetLastError();
 auto temp=target;temp+=L".new-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
 DWORD result=ERROR_SUCCESS;
 {SECURITY_ATTRIBUTES sa{sizeof(sa),sd,FALSE};Handle input(CreateFileW(source,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr));if(input.value==INVALID_HANDLE_VALUE){result=GetLastError();LocalFree(sd);return result;}Handle output(CreateFileW(temp.c_str(),GENERIC_WRITE,0,&sa,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));if(output.value==INVALID_HANDLE_VALUE)result=GetLastError();LocalFree(sd);
  if(output.value==INVALID_HANDLE_VALUE)return result;
  else{std::vector<BYTE> buffer(65536);DWORD read=0,written=0;for(;;){if(!ReadFile(input.value,buffer.data(),static_cast<DWORD>(buffer.size()),&read,nullptr)){result=GetLastError();break;}if(!read)break;if(!WriteFile(output.value,buffer.data(),read,&written,nullptr)||written!=read){result=ERROR_WRITE_FAULT;break;}}if(!result&&!FlushFileBuffers(output.value))result=GetLastError();}
 }
 if(!result&&!MoveFileExW(temp.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))result=GetLastError();
 if(result)DeleteFileW(temp.c_str());return result;
}
inline DWORD Install(const std::wstring& owner){
 if(!IsUserAnAdmin())return ERROR_ELEVATION_REQUIRED;PSID parsed=nullptr;if(owner.size()>184||!ConvertStringSidToSidW(owner.c_str(),&parsed))return ERROR_INVALID_SID;bool valid=IsValidSid(parsed)!=FALSE;LocalFree(parsed);if(!valid)return ERROR_INVALID_SID;
 ServiceHandle manager(OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CREATE_SERVICE));if(!manager.value)return GetLastError();
 ServiceHandle existing(OpenServiceW(manager.value,Service,SERVICE_QUERY_STATUS|SERVICE_START));
 if(existing.value){if(OwnerSid()!=owner)return ERROR_ACCESS_DENIED;if(!StartServiceW(existing.value,0,nullptr)&&GetLastError()!=ERROR_SERVICE_ALREADY_RUNNING)return GetLastError();return ERROR_SUCCESS;}
 if(GetLastError()!=ERROR_SERVICE_DOES_NOT_EXIST)return GetLastError();
 auto dir=SecureDirectory();if(dir.empty()||!ProtectedDirectory(dir))return ERROR_ACCESS_DENIED;
 wchar_t self[32768]{};if(!GetModuleFileNameW(nullptr,self,32768))return GetLastError();auto target=dir/L"TidyDeskSystem.exe";
 // A protected directory prevents replacement by a non-elevated process.
 auto copyResult=CopyProtectedExecutable(self,target);if(copyResult)return copyResult;
 HKEY key=nullptr;auto error=RegCreateKeyExW(HKEY_LOCAL_MACHINE,Key,0,nullptr,0,KEY_WRITE|KEY_WOW64_64KEY,nullptr,&key,nullptr);if(error)return static_cast<DWORD>(error);error=RegSetValueExW(key,L"Owner",0,REG_SZ,reinterpret_cast<const BYTE*>(owner.c_str()),static_cast<DWORD>((owner.size()+1)*sizeof(wchar_t)));RegCloseKey(key);if(error)return static_cast<DWORD>(error);
 auto command=L"\""+target.wstring()+L"\" --service";ServiceHandle service(CreateServiceW(manager.value,Service,L"TidyDesk 系统设置助手",SERVICE_START|DELETE,SERVICE_WIN32_OWN_PROCESS,SERVICE_AUTO_START,SERVICE_ERROR_NORMAL,command.c_str(),nullptr,nullptr,nullptr,nullptr,nullptr));if(!service.value)return GetLastError();if(!StartServiceW(service.value,0,nullptr)){auto result=GetLastError();DeleteService(service.value);return result;}return ERROR_SUCCESS;
}
inline DWORD RemoveElevated(){if(!IsUserAnAdmin())return ERROR_ELEVATION_REQUIRED;ServiceHandle manager(OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT));if(!manager.value)return GetLastError();ServiceHandle service(OpenServiceW(manager.value,Service,SERVICE_STOP|SERVICE_QUERY_STATUS|DELETE));if(!service.value)return GetLastError()==ERROR_SERVICE_DOES_NOT_EXIST?ERROR_SUCCESS:GetLastError();SERVICE_STATUS status{};if(!ControlService(service.value,SERVICE_CONTROL_STOP,&status)&&GetLastError()!=ERROR_SERVICE_NOT_ACTIVE)return GetLastError();if(!DeleteService(service.value)&&GetLastError()!=ERROR_SERVICE_MARKED_FOR_DELETE)return GetLastError();return ERROR_SUCCESS;}
}
