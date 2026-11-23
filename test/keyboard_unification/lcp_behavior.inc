// Genuine pinned wolfSSL SHA-256 is REACHED. Enrollment rejects the neutral
// un-enrolled/no-release-key state via the exact production gate. Every later
// transport/key-store operation is a fail-fast UNREACHED tripwire, never success.
#include <WolfsslCrypto.h>
#include <LcpLicense.h>
static unsigned unreachedCalls=0;
[[noreturn]] static void unreached(){++unreachedCalls;std::abort();}
namespace UnreachedTransport {template<class... T>bool postForm(T&&...){unreached();}}
namespace UnreachedBookKey {template<class... T>bool write(T&&...){unreached();}}
namespace UnreachedBase64 {template<class... T>std::string encode(T&&...){unreached();}}
namespace boundaryLcpDevice {
struct NeutralDevice {std::string deviceId,privHex;bool valid(){return false;}bool saveToFile(){unreached();}};
static NeutralDevice device;
static void loadOnce(){} // isolated empty device state; never read owner storage
static void forget(){unreached();}
template<class... T>bool unwrapContentKey(T&&...){unreached();}
static bool parseEnrollResponse(const std::string&,std::string*){unreached();}
}
#define FREEINK_LCP_RELEASE_KEY ""
#define LCP_DEVICE device
static constexpr const char* LCP_ENROLL_URL="unreached";
struct LcpBoundary:CallerBoundary {
 enum class State{AUTH};enum class LcpUnlock{Ok,WrongPassphrase,Failed};State state=State::AUTH;
 freeink::content::LcpLicense pendingLcpLicense;std::string statusMessage,pendingLcpLicenseText,pendingLcpPath;
 struct Wrapped {std::string epk,iv,ct,tag;};using LcpWrappedKey=Wrapped;
 struct Passphrases {void put(const std::string&,const char*){unreached();}}passphrases;
 StrId failure=StrId::STR_NONE_OPT;unsigned downloads=0;
 void requestUpdate(bool=false){}void fail(StrId id){failure=id;}void downloadFinished(bool){++downloads;}
 bool parseUnlockResponse(const std::string&,Wrapped*,int64_t*){unreached();}
 void promptLcpPassphrase(bool);LcpUnlock requestLcpUnlock(const std::string&);
};
#define LCP_PASSPHRASES passphrases
#include "lcp_extracted.inc"
#undef LCP_DEVICE
#undef LCP_PASSPHRASES
#undef FREEINK_LCP_RELEASE_KEY
static void lcpBehavior(){
 LcpBoundary c;c.pendingLcpLicense.hint="Neutral passphrase";c.promptLcpPassphrase(false);
 auto& owner=*static_cast<KeyboardEntryActivity*>(c.child.get());assert(owner.title=="Neutral passphrase" && owner.text.empty() && owner.inputType==InputType::Text && owner.maxLength==0 && owner.minLength==0);
 owner.onEnter();owner.render(RenderLock{});captureNative("native.opds.lcp-passphrase");finished=false;tapValue(owner,'1');tapValue(owner,fui::QWERTY_KEY_ENTER);assert(finished && !owner.result.isCancelled);c.callback(owner.result);
 assert(c.failure==StrId::STR_LCP_OFFICIAL_BUILD && c.downloads==0 && unreachedCalls==0);
 // Same crypto implementation independently checked against the known SHA256('1').
 freeink::content::WolfsslCrypto crypto;uint8_t userKey[32];freeink::content::lcpUserKey(crypto,"1",1,userKey);char hex[65];for(int i=0;i<32;++i)snprintf(hex+i*2,3,"%02x",userKey[i]);
 assert(std::string(hex)=="6b86b273ff34fce19d6b804eff5a3f5747ada4eaa22f1d49c01e52ddb7875b4b");
 c.promptLcpPassphrase(true);auto& cancel=*static_cast<KeyboardEntryActivity*>(c.child.get());assert(cancel.title==tr(STR_LCP_WRONG_PASSPHRASE));cancel.onEnter();finished=false;key(cancel,SDL_SCANCODE_ESCAPE,true);key(cancel,SDL_SCANCODE_ESCAPE,false);assert(finished && cancel.result.isCancelled);c.callback(cancel.result);assert(c.downloads==1 && unreachedCalls==0);
 puts("CALLER_PASS native.opds.lcp-passphrase title_type_limits_default actual_HAL_submit real_pinned_wolfSSL_SHA256 production_unsigned_enrollment_rejection cancel no_network_or_crypto_success_stub");
}
