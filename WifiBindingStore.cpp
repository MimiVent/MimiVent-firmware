#include "WifiBindingStore.h"
#if MIMIVENT_ENABLE_WIFI && MIMIVENT_WIFI_READ_CONTROL
#include "WifiBindingCore.h"
#include "WifiControlLease.h"
#include "WifiStorageBudget.h"
#include "OtaCheckpointStore.h"
#include <WiFi.h>
#include <esp_random.h>
#include <esp_mac.h>
#include <mbedtls/sha256.h>
#include <nvs.h>
#include <freertos/queue.h>
#include <atomic>
namespace {
using namespace WifiBindings;
static_assert(sizeof(OtaCheckpoint)==704,"reassess NVS reserve when checkpoint layout changes");
Core core;
nvs_handle_t storage;
bool storageOpen=false;std::atomic<bool> ready{false};
const char* storeState="UNINITIALIZED";
portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
Record authSnapshot{};
char hardwareDeviceId[kIdLength]{};
void publish() {
 portENTER_CRITICAL(&mux); authSnapshot=core.active; portEXIT_CRITICAL(&mux);
}
String encode(cJSON* value) {char* raw=cJSON_PrintUnformatted(value);String s=raw?raw:"{\"error\":\"MEMORY\"}";cJSON_free(raw);cJSON_Delete(value);return s;}
String error(const char* text) {auto* v=cJSON_CreateObject();cJSON_AddStringToObject(v,"error",text);return encode(v);}
const char* field(cJSON* r,const char* name) {auto* v=cJSON_GetObjectItemCaseSensitive(r,name);return cJSON_IsString(v)?v->valuestring:"";}
bool label(const char* s,size_t max) {size_t n=strlen(s);if(!n||n>max)return false;for(size_t i=0;i<n;++i)if(!isalnum((unsigned char)s[i])&&s[i]!='-'&&s[i]!='_')return false;return true;}
bool uuidValid(const char* s){return uuidText(s);}
// 硬件派生 deviceId：SHA256("mimivent-c3:" + 出厂 MAC) 前 16 字节。
// 不直接暴露 MAC，擦除 flash / 升级 / 清配置后保持稳定；Gateway 用它关联历史。
void deriveHardwareDeviceId() {
 uint8_t mac[6]{};
 if(esp_read_mac(mac, ESP_MAC_WIFI_STA)!=ESP_OK) { hardwareDeviceId[0]=0; return; }
 const char* prefix="mimivent-c3:";
 char material[24]{};
 strncpy(material,prefix,sizeof(material)-1);
 size_t offset=strlen(prefix);
 for(size_t i=0;i<6&&offset+2<sizeof(material);++i){snprintf(material+offset,3,"%02x",mac[i]);offset+=2;}
 uint8_t digest[32]{};
 mbedtls_sha256(reinterpret_cast<const uint8_t*>(material),offset,digest,0);
 constexpr char hex[]="0123456789abcdef";
 for(size_t i=0;i<16;++i){hardwareDeviceId[i*2]=hex[digest[i]>>4];hardwareDeviceId[i*2+1]=hex[digest[i]&0x0f];}
 hardwareDeviceId[32]=0;
}
bool writeSlot(const char* slot,const Record& r) {
 if(nvs_set_blob(storage,slot,&r,sizeof(r))!=ESP_OK||nvs_commit(storage)!=ESP_OK)return false;
 Record check{};size_t n=sizeof(check);
 return nvs_get_blob(storage,slot,&check,&n)==ESP_OK&&n==sizeof(check)&&valid(check)&&!memcmp(&check,&r,sizeof(r));
}
bool save(const Record& r,void*) {
 const char* first=(r.sequence&1)?"svc-b":"svc-a";
 const char* second=(r.sequence&1)?"svc-a":"svc-b";
 if(writeSlot(first,r)&&writeSlot(second,r)){
  nvs_stats_t stats{};if(nvs_get_stats(nullptr,&stats)==ESP_OK)
   Serial.printf("[wifi-svc] sequence=%lu nvs-used=%u nvs-free=%u\n",(unsigned long)r.sequence,stats.used_entries,stats.free_entries);
  return true;
 }
 storeState="STORE_UNCONFIRMED";ready=false;return false;
}
bool load(const char* key,Record& r,bool& exists) {
 size_t n=0;const auto rc=nvs_get_blob(storage,key,nullptr,&n);exists=rc!=ESP_ERR_NVS_NOT_FOUND;
 if(!exists)return true;
 if(rc!=ESP_OK)return false;
 if(n!=sizeof(r))return false;
 if(nvs_get_blob(storage,key,&r,&n)!=ESP_OK||n!=sizeof(r))return false;
 return true;
}
void randomHex(char* output,size_t bytes) {uint8_t raw[16];esp_fill_random(raw,bytes);for(size_t i=0;i<bytes;++i)snprintf(output+i*2,3,"%02x",raw[i]);}
void randomToken(char token[44]) {
 constexpr char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
 uint8_t raw[32];esp_fill_random(raw,sizeof(raw));uint32_t bits=0;int count=0;size_t out=0;
 for(uint8_t b:raw){bits=(bits<<8)|b;count+=8;while(count>=6){count-=6;token[out++]=alphabet[(bits>>count)&63];}}
 if(count)token[out++]=alphabet[(bits<<(6-count))&63];token[out]=0;
}
void hashToken(const char* token,uint8_t out[32]) {mbedtls_sha256(reinterpret_cast<const uint8_t*>(token),strlen(token),out,0);}
const char* entryStageText(const ServiceEntry& e) {
 if(!e.used) return "none";
 if(e.used==kEntryActive) return "active";
 return e.stage==kStageActivated ? "activated" : "pending";
}
}
void wifiBindingBegin() {
 deriveHardwareDeviceId();
 nvs_iterator_t iterator=nullptr;char seen[24][16]{};size_t seenCount=0;
 esp_err_t iteratorResult=nvs_entry_find("nvs",nullptr,NVS_TYPE_ANY,&iterator);
 while(iteratorResult==ESP_OK){
  nvs_entry_info_t info{};nvs_entry_info(iterator,&info);bool known=false;
  for(size_t i=0;i<seenCount;++i)if(!strcmp(seen[i],info.namespace_name))known=true;
  if(!known&&seenCount<24){
   strncpy(seen[seenCount++],info.namespace_name,15);nvs_handle_t handle;
   if(nvs_open(info.namespace_name,NVS_READONLY,&handle)==ESP_OK){size_t used=0;nvs_get_used_entry_count(handle,&used);nvs_close(handle);
    Serial.printf("[nvs-budget] namespace=%s entries=%u\n",info.namespace_name,used);}
  }
  iteratorResult=nvs_entry_next(&iterator);
 }
 nvs_release_iterator(iterator);
 if(nvs_open("mv-bindings",NVS_READWRITE,&storage)!=ESP_OK){storeState="STORAGE_UNAVAILABLE";return;}
 storageOpen=true;Record a{},b{};bool ae=false,be=false;
 if(!load("svc-a",a,ae)||!load("svc-b",b,be)){storeState="RECORD_UNREADABLE";return;}
 const bool av=ae&&valid(a),bv=be&&valid(b);
 if((ae&&!av)||(be&&!bv)){storeState="BINDINGS_CORRUPT";return;}
 if(av&&bv&&a.sequence==b.sequence&&memcmp(&a,&b,sizeof(a))){storeState="BINDINGS_CORRUPT";return;}
 if(av||bv)core.active=(!bv||(av&&a.sequence>=b.sequence))?a:b;
 // 重启后只清理未激活候选；已激活候选需留给 Gateway 核验或明确回退。
 if(!core.recoverAfterRestart(save,nullptr)){
  storeState=core.active.sequence==UINT32_MAX?"SEQUENCE_EXHAUSTED":"STORE_UNCONFIRMED";return;
 }
 // 硬件身份必须固定在记录里；首次启动或 MAC 变化时重写。
 if(hardwareDeviceId[0]){
  if(strcmp(core.active.deviceId,hardwareDeviceId)){
   if(core.active.sequence==UINT32_MAX){storeState="SEQUENCE_EXHAUSTED";return;}
   Record next=core.active;strncpy(next.deviceId,hardwareDeviceId,sizeof(next.deviceId)-1);
   ++next.sequence;seal(next);
   if(!save(next,nullptr)){storeState="STORE_UNCONFIRMED";return;}
   core.active=next;
  }
 }
 if(av||bv){
  if((!av||memcmp(&a,&core.active,sizeof(a)))&&!writeSlot("svc-a",core.active)){storeState="STORE_UNCONFIRMED";return;}
  if((!bv||memcmp(&b,&core.active,sizeof(b)))&&!writeSlot("svc-b",core.active)){storeState="STORE_UNCONFIRMED";return;}
 }
 ready=true;storeState="READY";publish();
 nvs_stats_t stats{};if(nvs_get_stats(nullptr,&stats)==ESP_OK)
  Serial.printf("[wifi-svc] ready sequence=%lu nvs-used=%u nvs-free=%u\n",(unsigned long)core.active.sequence,stats.used_entries,stats.free_entries);
}
void wifiBindingUpdate(){
 if(!ready)return;core.tick(millis());publish();
}
bool wifiBindingReady(){return ready;}
const char* wifiBindingState(){return storeState;}
const char* wifiBindingDeviceId(){return hardwareDeviceId;}
WifiAuthorization wifiBindingAuthorize(const char* token,uint32_t now){
 WifiAuthorization result{};if(!ready||!token||strlen(token)!=43)return result;
 for(size_t i=0;i<43;++i)if(!isalnum((unsigned char)token[i])&&token[i]!='-'&&token[i]!='_')return result;
 uint8_t hash[32];hashToken(token,hash);
 portENTER_CRITICAL(&mux);
 if(authSnapshot.active.used==kEntryActive&&equalHash(hash,authSnapshot.active.credentialHash)){
  result.valid=true;strcpy(result.tokenId,authSnapshot.active.credentialId);strcpy(result.serviceId,authSnapshot.active.serviceId);
  result.authRevision=authSnapshot.active.authRevision;
 }
 if(!result.valid&&authSnapshot.candidate.used==kEntryCandidate&&equalHash(hash,authSnapshot.candidate.credentialHash)){
  result.valid=true;result.pending=true;strcpy(result.tokenId,authSnapshot.candidate.credentialId);strcpy(result.serviceId,authSnapshot.candidate.serviceId);
  result.authRevision=authSnapshot.candidate.authRevision;
 }
 portEXIT_CRITICAL(&mux);return result;
}
bool wifiBindingOwnerAllowed(const char* owner){
 // BLE 管理通道已由 BluetoothManager 强制 encrypted+bonded；此处不再区分 owner。
 return ready&&owner&&owner[0];
}
bool wifiBindingRevokeAllowed(const char*,const char*){return ready;}
bool wifiBindingAddAuthTokens(cJSON* state){
 if(!state||!ready)return false;
 Record snapshot{};char device[kIdLength]{};
 portENTER_CRITICAL(&mux);snapshot=authSnapshot;strncpy(device,hardwareDeviceId,sizeof(device)-1);portEXIT_CRITICAL(&mux);
 // 单服务授权身份：Gateway 用它核验设备端已激活哪个服务凭据。
 // 不再输出每手机 token 摘要（三槽模型已下线）。
 auto* service=cJSON_AddObjectToObject(state,"service");
 if(!service)return false;
 cJSON_AddStringToObject(service,"deviceId",device);
 cJSON_AddStringToObject(service,"serviceId",snapshot.active.serviceId);
 cJSON_AddStringToObject(service,"credentialId",snapshot.active.credentialId);
 cJSON_AddNumberToObject(service,"authRevision",snapshot.active.authRevision);
 cJSON_AddStringToObject(service,"state",snapshot.active.used?entryStageText(snapshot.active):"unconfigured");
 if(snapshot.candidate.used){
  cJSON_AddStringToObject(service,"candidateTransactionId",snapshot.candidate.transactionId);
  cJSON_AddStringToObject(service,"candidateServiceId",snapshot.candidate.serviceId);
  cJSON_AddStringToObject(service,"candidateCredentialId",snapshot.candidate.credentialId);
  cJSON_AddNumberToObject(service,"candidateAuthRevision",snapshot.candidate.authRevision);
  cJSON_AddStringToObject(service,"candidateState",entryStageText(snapshot.candidate));
 }
 return true;
}
String wifiBindingRequest(cJSON* root,const char* owner){
 if(!ready)return error(storeState);
 if(!owner||!owner[0])return error("PAIRING_REQUIRED");
 wifiBindingUpdate();const char* op=field(root,"op");
 auto* out=cJSON_CreateObject();const char* failure=nullptr;const auto now=millis();
 if(!strcmp(op,"wifi.service.status")){
  cJSON_AddStringToObject(out,"deviceId",hardwareDeviceId);
  cJSON_AddStringToObject(out,"state",core.active.active.used?entryStageText(core.active.active):"unconfigured");
  if(core.active.active.used){
   cJSON_AddStringToObject(out,"serviceId",core.active.active.serviceId);
   cJSON_AddStringToObject(out,"credentialId",core.active.active.credentialId);
   cJSON_AddNumberToObject(out,"authRevision",core.active.active.authRevision);
  }
  if(core.active.candidate.used){
   cJSON_AddStringToObject(out,"candidateTransactionId",core.active.candidate.transactionId);
   cJSON_AddStringToObject(out,"candidateServiceId",core.active.candidate.serviceId);
   cJSON_AddStringToObject(out,"candidateCredentialId",core.active.candidate.credentialId);
   cJSON_AddNumberToObject(out,"candidateAuthRevision",core.active.candidate.authRevision);
   cJSON_AddStringToObject(out,"candidateState",entryStageText(core.active.candidate));
  }
 }else if(!strcmp(op,"wifi.service.begin")){
  const char* transactionId=field(root,"transactionId");
  const char* serviceId=field(root,"serviceId");
  const char* providedCredential=field(root,"credentialId");
  const auto revision=cJSON_GetObjectItemCaseSensitive(root,"authRevision");
  if(WiFi.status()!=WL_CONNECTED)failure="WIFI_UNAVAILABLE";
  else if(!uuidValid(transactionId)||!hexId(serviceId))failure="INVALID_REQUEST";
  else{
   // BLE 管理通道已强制 encrypted+bonded；begin 即开启一次管理窗口。
   if(!core.window||strcmp(core.windowOwner,owner))failure=core.open(owner,now);
   if(!failure){
    char credential[kIdLength]{};char token[44]{};
    if(hexId(providedCredential))strncpy(credential,providedCredential,sizeof(credential)-1);
    else randomHex(credential,16);
    randomToken(token);
    uint8_t hash[32];hashToken(token,hash);
    const uint32_t authRevision=cJSON_IsNumber(revision)?static_cast<uint32_t>(revision->valuedouble):(core.active.active.authRevision+1);
    failure=core.begin(owner,transactionId,serviceId,credential,hash,authRevision,now,save,nullptr);
    if(!failure){
     cJSON_AddStringToObject(out,"deviceId",hardwareDeviceId);
     cJSON_AddStringToObject(out,"serviceId",serviceId);
     cJSON_AddStringToObject(out,"credentialId",credential);
     cJSON_AddStringToObject(out,"token",token);
     cJSON_AddNumberToObject(out,"authRevision",authRevision);
     cJSON_AddStringToObject(out,"transactionId",transactionId);
     cJSON_AddStringToObject(out,"state","pending");
     cJSON_AddNumberToObject(out,"expiresInSec",(kTtlMs-uint32_t(now-core.windowAt))/1000);
    }
    memset(token,0,sizeof(token));
   }
  } }else if(!strcmp(op,"wifi.service.activate")){
  const char* transactionId=field(root,"transactionId");
  if(!uuidValid(transactionId))failure="INVALID_REQUEST";
  else failure=core.activate(owner,transactionId,now,save,nullptr);
  if(!failure){cJSON_AddStringToObject(out,"state","active");}
 }else if(!strcmp(op,"wifi.service.cancel")){
  failure=core.cancel(owner,field(root,"transactionId"),save,nullptr);
  if(!failure)cJSON_AddStringToObject(out,"state","cancelled");
 }else if(!strcmp(op,"wifi.service.confirm")){
  failure=core.confirm(field(root,"transactionId"),save,nullptr);
  if(!failure)cJSON_AddStringToObject(out,"state","confirmed");
 }else if(!strcmp(op,"wifi.service.clear")){
  if(strcmp(field(root,"confirm"),"CLEAR_SERVICE"))failure="CONFIRMATION_REQUIRED";
  else{
   failure=core.reset(save,nullptr);
   if(!failure){wifiControlLeaseReset();cJSON_AddStringToObject(out,"state","cleared");}
  }
 }else if(!strcmp(op,"wifi.service.verify")){
  // 无副作用只读核验：Gateway 用有效凭据调用，确认设备端授权与网络已就绪。
  cJSON_AddStringToObject(out,"deviceId",hardwareDeviceId);
  cJSON_AddStringToObject(out,"serviceId",core.active.active.serviceId);
  cJSON_AddStringToObject(out,"credentialId",core.active.active.credentialId);
  cJSON_AddNumberToObject(out,"authRevision",core.active.active.authRevision);
  cJSON_AddBoolToObject(out,"ready",core.active.active.used==kEntryActive);
  cJSON_AddBoolToObject(out,"wifiConnected",WiFi.status()==WL_CONNECTED);
 }else if(!strcmp(op,"wifi.bind.open")||!strcmp(op,"wifi.bind.begin")||!strcmp(op,"wifi.bind.verify")||
          !strcmp(op,"wifi.bind.commit")||!strcmp(op,"wifi.bind.status")||!strcmp(op,"wifi.bind.cancel")||
          !strcmp(op,"wifi.bind.reset")||!strcmp(op,"wifi.revoke")||!strcmp(op,"wifi.clients")){
  // 旧三槽协议已下线；明确引导重新配置而不是返回模糊失败。
  failure="SERVICE_RECONFIG_REQUIRED";
 }else failure="UNSUPPORTED_OPERATION";
 publish();if(failure){cJSON_Delete(out);return error(failure);}return encode(out);
}
#endif
