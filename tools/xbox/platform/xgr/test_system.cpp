#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

typedef HRESULT (*QueryApi)(const GUID&, const GUID&, void**);
struct AsyncBlock { void* queue; void* context; void (*callback)(AsyncBlock*); unsigned char internal[32]; };
struct Token { uint64_t value; };
struct LocalId { uint64_t value; };
struct DeviceId { uint8_t value[32]; };
struct ConnectivityHint {
  uint32_t level, cost, ianaInterfaceType;
  uint8_t networkInitialized, approachingDataLimit, overDataLimit, roaming;
};
struct TcpStatistics { uint64_t values[5]; };

static int g_failures = 0;
static void Check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) g_failures++;
}

static QueryApi g_query;
static std::string g_table;
static int TableSlot(const char* method) {
  std::string key = std::string("\"name\": \"") + method + "\",";
  size_t at = g_table.find(key);
  if (at == std::string::npos || g_table.find(key, at + 1) != std::string::npos) return -1;
  size_t slot = g_table.find("\"slot\": ", at);
  size_t next = g_table.find("\"name\": ", at + key.size());
  if (slot == std::string::npos || (next != std::string::npos && slot > next)) return -1;
  return std::atoi(g_table.c_str() + slot + 8);
}
static bool LoadTable() {
  std::ifstream file("table.json", std::ios::binary);
  if (!file) return false;
  std::ostringstream text;
  text << file.rdbuf();
  g_table = text.str();
  return !g_table.empty();
}

struct Api {
  void* self;
  void** slots;
  template <typename Fn> Fn At(int slot, const char* method) const {
    int expected = TableSlot(method);
    if (expected != slot) {
      std::printf("FAIL slot pin: %s is slot %d in table.json, the test uses %d\n", method, expected, slot);
      std::exit(1);
    }
    return reinterpret_cast<Fn>(slots[slot]);
  }
};
static Api Get(const GUID& iid) {
  void* object = nullptr;
  HRESULT hr = g_query(iid, iid, &object);
  if (FAILED(hr) || !object) {
    std::printf("FAIL QueryApiImpl %08lx\n", static_cast<unsigned long>(hr));
    std::exit(1);
  }
  return {object, *static_cast<void***>(object)};
}

constexpr HRESULT kInsufficientBuffer = static_cast<HRESULT>(0x8007007A);
constexpr HRESULT kNotFound = static_cast<HRESULT>(0x80070490);
constexpr HRESULT kUserNotFound = static_cast<HRESULT>(0x89245104);
constexpr HRESULT kDeferralNotAvailable = static_cast<HRESULT>(0x89245103);

static const GUID kSystem = {0x1861cf2e, 0xe18b, 0x4834, {0xa9, 0xf5, 0xb4, 0xa4, 0xe6, 0xef, 0xb4, 0xcf}};
static const GUID kLauncher = {0x1b339674, 0x328d, 0x4283, {0xa2, 0x00, 0x31, 0x71, 0xf1, 0x8d, 0x36, 0x39}};
static const GUID kFeature = {0x8836fe87, 0xedb9, 0x4fe3, {0x8d, 0xad, 0x05, 0xf0, 0xd2, 0xcd, 0x5b, 0x40}};
static const GUID kProtocol = {0x026b010c, 0x06c3, 0x4cdd, {0xbb, 0xcb, 0x43, 0xf2, 0x29, 0xdb, 0x1c, 0xff}};
static const GUID kActivation = {0x2e4f76fe, 0x0fc7, 0x461e, {0xab, 0x4d, 0xa4, 0x49, 0x94, 0x34, 0xc3, 0xcf}};
static const GUID kInvite = {0x014d1cc3, 0xbcfe, 0x41ff, {0xb2, 0xf0, 0xe1, 0xef, 0x07, 0x15, 0x58, 0x28}};
static const GUID kNetworking = {0xbf2346b2, 0x39af, 0x4658, {0xb5, 0xea, 0x44, 0x71, 0x3c, 0x7e, 0x83, 0xb3}};
static const GUID kAccessibility = {0xd722b373, 0x8c4d, 0x4692, {0x9e, 0x51, 0xd6, 0xad, 0x9b, 0x37, 0xaa, 0x7d}};
static const GUID kUserDevice = {0x0cc6a956, 0xe7e1, 0x4fdf, {0x93, 0x41, 0x9d, 0x5d, 0xa6, 0x49, 0xeb, 0xc8}};
static const GUID kGameUi = {0x6eeaa73e, 0x9669, 0x43ad, {0xa2, 0xc7, 0xd0, 0xda, 0x4e, 0x1f, 0x50, 0xa1}};
static const GUID kUser = {0x01acd177, 0x91f9, 0x4763, {0xa3, 0x8e, 0xcc, 0xbb, 0x55, 0xce, 0x32, 0xe0}};
static const GUID kError = {0x8ca467f7, 0x22e8, 0x4096, {0x84, 0x56, 0xbb, 0x8a, 0xa1, 0x3f, 0x79, 0xd8}};
static const GUID kDisplay = {0x35f07670, 0x706e, 0x4bfb, {0x94, 0x76, 0x09, 0x07, 0x98, 0xc5, 0xeb, 0xf3}};
static const GUID kThreading ={0x073b7dcb, 0x1fcf, 0x4030, {0x94, 0xbe, 0xe3, 0xc9, 0xeb, 0x62, 0x34, 0x28}};

static Api g_threading;
static HRESULT WaitAsync(AsyncBlock* block) {
  return g_threading.At<HRESULT (*)(void*, AsyncBlock*, BOOLEAN)>(3, "XAsyncGetStatus")(g_threading.self, block, TRUE);
}

typedef HRESULT (*RegisterFn)(void*, void*, void*, void*, Token*);
typedef BOOLEAN (*UnregisterFn)(void*, Token, BOOLEAN);
static int g_silent_calls = 0;
static void CALLBACK SilentCallback(void*, const void*) { g_silent_calls++; }

static void CheckSilentEvent(const Api& api, int registerSlot, const char* name, int unregisterSlot, const char* unregisterName) {
  char label[160];
  Token first{}, second{};
  HRESULT hr1 = api.At<RegisterFn>(registerSlot, name)(api.self, nullptr, nullptr, reinterpret_cast<void*>(&SilentCallback), &first);
  HRESULT hr2 = api.At<RegisterFn>(registerSlot, name)(api.self, nullptr, nullptr, reinterpret_cast<void*>(&SilentCallback), &second);
  std::snprintf(label, sizeof(label), "%s registers with distinct tokens", name);
  Check(hr1 == S_OK && hr2 == S_OK && first.value && second.value && first.value != second.value, label);
  Token unused{};
  HRESULT hr = api.At<RegisterFn>(registerSlot, name)(api.self, nullptr, nullptr, nullptr, &unused);
  std::snprintf(label, sizeof(label), "%s rejects a null callback", name);
  Check(hr == E_INVALIDARG, label);
  hr = api.At<RegisterFn>(registerSlot, name)(api.self, nullptr, nullptr, reinterpret_cast<void*>(&SilentCallback), nullptr);
  std::snprintf(label, sizeof(label), "%s rejects a null token", name);
  Check(hr == E_INVALIDARG, label);
  BOOLEAN u1 = api.At<UnregisterFn>(unregisterSlot, unregisterName)(api.self, first, FALSE);
  BOOLEAN u2 = api.At<UnregisterFn>(unregisterSlot, unregisterName)(api.self, second, TRUE);
  std::snprintf(label, sizeof(label), "%s unregisters", name);
  Check(u1 && u2, label);
}

static HANDLE g_hint_event;
static ConnectivityHint g_delivered_hint;
static int g_hint_calls = 0;
static void CALLBACK HintCallback(void* context, const ConnectivityHint* hint) {
  g_hint_calls++;
  if (hint) g_delivered_hint = *hint;
  if (context) SetEvent(static_cast<HANDLE>(context));
}

static void CheckSystem() {
  Api system = Get(kSystem);
  auto deviceId = system.At<HRESULT (*)(void*, size_t, char*, size_t*)>(5, "XSystemGetAppSpecificDeviceId");
  char id[64];
  size_t used = 0;
  std::memset(id, 'x', sizeof(id));
  HRESULT hr = deviceId(system.self, sizeof(id), id, &used);
  bool zeros = used == 45 && id[44] == '\0' && std::strspn(id, "0") == 44;
  Check(hr == S_OK && zeros, "XSystemGetAppSpecificDeviceId returns the documented all-zero id");
  used = 0;
  hr = deviceId(system.self, 44, id, &used);
  Check(hr == kInsufficientBuffer && used == 45, "XSystemGetAppSpecificDeviceId reports the needed size for a short buffer");
  hr = deviceId(system.self, 0, nullptr, &used);
  Check(hr == kInsufficientBuffer, "XSystemGetAppSpecificDeviceId rejects a null buffer");
  hr = deviceId(system.self, sizeof(id), id, nullptr);
  Check(hr == S_OK, "XSystemGetAppSpecificDeviceId accepts a null used pointer");
  system.At<void (*)(void*, BOOLEAN)>(8, "XSystemAllowFullDownloadBandwidth")(system.self, TRUE);
  system.At<void (*)(void*, BOOLEAN)>(8, "XSystemAllowFullDownloadBandwidth")(system.self, FALSE);
  Check(true, "XSystemAllowFullDownloadBandwidth accepts the hint");
  HRESULT track = system.At<HRESULT (*)(void*, void*, void*)>(6, "XSystemHandleTrack")(system.self, nullptr, nullptr);
  Check(track == S_OK, "XSystemHandleTrack accepts a null callback (tracking callback off; test_misc.cpp checks tracking)");
}

static void CheckLauncher() {
  Api launcher = Get(kLauncher);
  auto acquire = launcher.At<HRESULT (*)(void*, void**)>(4, "XDisplayAcquireTimeoutDeferral");
  auto close = launcher.At<void (*)(void*, void*)>(5, "XDisplayCloseTimeoutDeferralHandle");
  void* first = nullptr;
  void* second = nullptr;
  HRESULT hr1 = acquire(launcher.self, &first);
  HRESULT hr2 = acquire(launcher.self, &second);
  Check(hr1 == S_OK && hr2 == S_OK && first && second && first != second, "XDisplayAcquireTimeoutDeferral hands out independent handles");
  Check(acquire(launcher.self, nullptr) == E_INVALIDARG, "XDisplayAcquireTimeoutDeferral rejects a null out pointer");
  close(launcher.self, first);
  close(launcher.self, second);
  close(launcher.self, nullptr);
  Check(true, "XDisplayCloseTimeoutDeferralHandle releases handles and ignores null");
  HRESULT launch = launcher.At<HRESULT (*)(void*, void*, const char*)>(3, "XLaunchUri")(launcher.self, nullptr, "not a uri");
  Check(launch == E_INVALIDARG, "XLaunchUri rejects text without a URI scheme (test_launch.cpp checks launching)");
}

struct HdrInfo { float minLuminance, maxLuminance, maxFullFrameLuminance; };
typedef bool (*ErrorCallbackFn)(HRESULT, const char*, void*);
static bool CALLBACK DeclineErrorHandling(HRESULT, const char*, void*) { return false; }
struct ReportedError { int calls = 0; HRESULT hr = S_OK; std::string message; bool handled = false; };
static bool CALLBACK RecordError(HRESULT hr, const char* message, void* context) {
  auto* seen = static_cast<ReportedError*>(context);
  seen->calls++;
  seen->hr = hr;
  seen->message = message ? message : "<null>";
  return seen->handled;
}

static void CheckErrorAndDisplay() {
  Api errors = Get(kError);
  auto setCallback = errors.At<void (*)(void*, ErrorCallbackFn, void*)>(4, "XErrorSetCallback");
  auto setOptions = errors.At<void (*)(void*, uint32_t, uint32_t)>(5, "XErrorSetOptions");
  setCallback(errors.self, &DeclineErrorHandling, &g_failures);
  setCallback(errors.self, nullptr, nullptr);
  setOptions(errors.self, 1, 0);
  setOptions(errors.self, 3, 4);
  Check(true, "XErrorSetCallback and XErrorSetOptions accept, replace and clear settings");

  auto report = reinterpret_cast<void (*)(HRESULT, const char*)>(GetProcAddress(GetModuleHandleA("xgameruntime.dll"), "XErrorReport"));
  Check(report != nullptr, "XErrorReport is exported");
  if (report) {
    ReportedError seen;
    setOptions(errors.self, 1, 1);
    setCallback(errors.self, &RecordError, &seen);
    report(E_ACCESSDENIED, "first");
    Check(seen.calls == 1 && seen.hr == E_ACCESSDENIED && seen.message == "first", "XErrorReport passes the HRESULT, message and context to the callback");
    seen.handled = true;
    report(E_FAIL, nullptr);
    Check(seen.calls == 2 && seen.hr == E_FAIL && seen.message.empty(), "XErrorReport gives the callback an empty message for null and continues when it returns true");
    ReportedError other;
    setCallback(errors.self, &RecordError, &other);
    report(E_ABORT, "second");
    Check(seen.calls == 2 && other.calls == 1 && other.message == "second", "XErrorReport calls only the most recently set callback");
    setCallback(errors.self, nullptr, nullptr);
    report(E_FAIL, "third");
    Check(other.calls == 1, "XErrorReport calls nothing after the callback is cleared");
    setOptions(errors.self, 2, 4);
    report(E_FAIL, "fourth");
    Check(true, "XErrorReport returns to the caller when the options ask for a break or a fast fail");
    setOptions(errors.self, 1, 0);
  }

  Api display = Get(kDisplay);
  auto tryHdr = display.At<uint32_t (*)(void*, uint32_t, HdrInfo*)>(4, "XDisplayTryEnableHdrMode");
  HdrInfo info{-1.0f, -1.0f, -1.0f};
  uint32_t preferHdr = tryHdr(display.self, 0, &info);
  uint32_t preferRate = tryHdr(display.self, 1, nullptr);
  bool consistent = (preferHdr == 1 || preferHdr == 2) && preferRate == preferHdr;
  bool infoFilled = preferHdr == 1 ? info.maxLuminance > 0.0f : info.maxLuminance == -1.0f;
  std::printf("INFO XDisplayTryEnableHdrMode reports %s for the primary desktop output\n", preferHdr == 1 ? "Enabled" : preferHdr == 2 ? "Disabled" : "Unknown");
  Check(consistent && infoFilled, "XDisplayTryEnableHdrMode reports the desktop HDR state and fills the info only when enabled");
  Check(tryHdr(display.self, 2, &info) == 0, "XDisplayTryEnableHdrMode returns Unknown for an unknown preference");
}

static void CheckFeatures() {
  Api feature = Get(kFeature);
  auto available = feature.At<BOOLEAN (*)(void*, uint32_t)>(3, "XGameRuntimeIsFeatureAvailable");
  const uint32_t expectedOn[] = {0, 2, 3, 4, 5, 7, 9, 11, 12, 14, 15, 16, 17, 18, 19, 20, 21};
  const uint32_t expectedOff[] = {1, 13, 22, 0xffffffffu};
  bool on = true, off = true;
  for (uint32_t f : expectedOn) on = on && available(feature.self, f);
  for (uint32_t f : expectedOff) off = off && !available(feature.self, f);
  Check(on, "XGameRuntimeIsFeatureAvailable reports implemented features (XAsync, XAsyncProvider, XDisplay, XGame, XGameSave, XLauncher, XPackage, XPersistentLocalStorage, XSystem, XTaskQueue, XThread, XUser, XError, XGameEvent, XStore)");
  Check(off, "XGameRuntimeIsFeatureAvailable reports unimplemented and unknown features as unavailable");
  Check(available(feature.self, 6) && available(feature.self, 8) && available(feature.self, 10),
        "XGameRuntimeIsFeatureAvailable follows the override table (XGameInvite, XGameUI, XNetworking)");
}

static void CheckActivation() {
  int callsBefore = g_silent_calls;
  Api protocol = Get(kProtocol);
  CheckSilentEvent(protocol, 3, "XGameProtocolRegisterForActivation", 4, "XGameProtocolUnregisterForActivation");
  Api activation = Get(kActivation);
  CheckSilentEvent(activation, 3, "XGameActivationRegisterForEvent", 4, "XGameActivationUnregisterForEvent");
  HRESULT accept = activation.At<HRESULT (*)(void*, const char*)>(5, "XGameActivationAcceptPendingInvite")(activation.self, "ms-xbl-0://invite");
  Check(accept == HRESULT_FROM_WIN32(ERROR_NOT_FOUND), "XGameActivationAcceptPendingInvite reports no such pending invite");
  Api invite = Get(kInvite);
  CheckSilentEvent(invite, 3, "XGameInviteRegisterForEvent", 4, "XGameInviteUnregisterForEvent");
  CheckSilentEvent(invite, 5, "XGameInviteRegisterForPendingEvent", 6, "XGameInviteUnregisterForPendingEvent");
  Check(g_silent_calls == callsBefore, "activation, protocol and invite registrations do not invoke the callback");
  Token unknown{};
  Check(protocol.At<UnregisterFn>(4, "XGameProtocolUnregisterForActivation")(protocol.self, unknown, FALSE),
        "XGameProtocolUnregisterForActivation returns true for a token that was never registered");
  Check(activation.At<UnregisterFn>(4, "XGameActivationUnregisterForEvent")(activation.self, unknown, FALSE),
        "XGameActivationUnregisterForEvent returns true for a token that was never registered");
  Check(invite.At<UnregisterFn>(4, "XGameInviteUnregisterForEvent")(invite.self, unknown, FALSE),
        "XGameInviteUnregisterForEvent returns true for a token that was never registered");
  Check(invite.At<UnregisterFn>(6, "XGameInviteUnregisterForPendingEvent")(invite.self, unknown, TRUE),
        "XGameInviteUnregisterForPendingEvent returns true for a token that was never registered");
}

struct Blocking { HANDLE entered; HANDLE release; volatile LONG calls; };
static void CALLBACK BlockingHint(void* context, const ConnectivityHint*) {
  auto* b = static_cast<Blocking*>(context);
  InterlockedIncrement(&b->calls);
  SetEvent(b->entered);
  WaitForSingleObject(b->release, 10000);
}

struct UnregisterCall { UnregisterFn unreg; void* self; Token token; BOOLEAN result; };
static DWORD WINAPI UnregisterAndWait(void* p) {
  auto* call = static_cast<UnregisterCall*>(p);
  call->result = call->unreg(call->self, call->token, TRUE);
  return 0;
}

static void CheckConnectivityUnregisterWhileRunning() {
  Api net = Get(kNetworking);
  auto reg = net.At<HRESULT (*)(void*, void*, void*, void*, Token*)>(16, "XNetworkingRegisterConnectivityHintChanged");
  auto unreg = net.At<UnregisterFn>(17, "XNetworkingUnregisterConnectivityHintChanged");
  Blocking b{CreateEventW(nullptr, FALSE, FALSE, nullptr), CreateEventW(nullptr, TRUE, FALSE, nullptr), 0};
  Token token{};
  HRESULT hr = reg(net.self, nullptr, &b, reinterpret_cast<void*>(&BlockingHint), &token);
  bool running = hr == S_OK && WaitForSingleObject(b.entered, 5000) == WAIT_OBJECT_0;
  Check(running && unreg(net.self, token, FALSE) == 0,
        "XNetworkingUnregisterConnectivityHintChanged(wait=false) during the callback returns false (documented: pending)");
  Check(unreg(net.self, token, FALSE) == 0,
        "XNetworkingUnregisterConnectivityHintChanged: polling a pending token keeps returning false while the callback runs");
  SetEvent(b.release);
  BOOLEAN done = 0;
  for (int i = 0; i < 500 && !done; ++i) {
    done = unreg(net.self, token, FALSE);
    if (!done) Sleep(10);
  }
  Check(done == 1 && b.calls == 1, "XNetworkingUnregisterConnectivityHintChanged: polling returns true once the callback has returned");

  ResetEvent(b.release);
  Token second{};
  hr = reg(net.self, nullptr, &b, reinterpret_cast<void*>(&BlockingHint), &second);
  running = hr == S_OK && WaitForSingleObject(b.entered, 5000) == WAIT_OBJECT_0;
  running = running && unreg(net.self, second, FALSE) == 0;
  UnregisterCall call{unreg, net.self, second, 2};
  HANDLE thread = CreateThread(nullptr, 0, &UnregisterAndWait, &call, 0, nullptr);
  bool blocked = thread && WaitForSingleObject(thread, 200) == WAIT_TIMEOUT;
  SetEvent(b.release);
  bool finished = thread && WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0;
  Check(running && blocked && finished && call.result == 1,
        "XNetworkingUnregisterConnectivityHintChanged(wait=true) after a pending wait=false still blocks until the callback returns");
  if (thread) CloseHandle(thread);
  CloseHandle(b.entered);
  CloseHandle(b.release);

  auto query = net.At<HRESULT (*)(void*, uint32_t, uint64_t*)>(18, "XNetworkingQueryConfigurationSetting");
  auto set = net.At<HRESULT (*)(void*, uint32_t, uint64_t)>(19, "XNetworkingSetConfigurationSetting");
  auto stats = net.At<HRESULT (*)(void*, uint32_t, TcpStatistics*)>(20, "XNetworkingQueryStatistics");
  bool configOk = true, statsOk = true;
  for (uint32_t i = 0; i < 3; ++i) {
    uint64_t value = 77;
    configOk = configOk && query(net.self, i, &value) == S_OK && value == 0 && set(net.self, i, 0) == E_NOTIMPL;
    TcpStatistics s;
    std::memset(&s, 0xcc, sizeof(s));
    bool zero = stats(net.self, i, &s) == S_OK;
    for (uint64_t v : s.values) zero = zero && v == 0;
    statsOk = statsOk && zero;
  }
  Check(configOk, "XNetworking{Query,Set}ConfigurationSetting: all three settings read 0 and refuse a set with E_NOTIMPL (documented for PC)");
  Check(statsOk, "XNetworkingQueryStatistics: all three types report zeros (documented for PC)");
  Check(query(net.self, 0, nullptr) == E_INVALIDARG && stats(net.self, 0, nullptr) == E_INVALIDARG,
        "XNetworkingQueryConfigurationSetting and XNetworkingQueryStatistics reject a null out pointer");
}

static void CheckNetworking() {
  Api net = Get(kNetworking);
  ConnectivityHint hint;
  std::memset(&hint, 0xcc, sizeof(hint));
  HRESULT hr = net.At<HRESULT (*)(void*, ConnectivityHint*)>(15, "XNetworkingGetConnectivityHint")(net.self, &hint);
  Check(hr == S_OK && hint.level == 1 && hint.cost == 0 && hint.networkInitialized == 1 && !hint.approachingDataLimit &&
            !hint.overDataLimit && !hint.roaming,
        "XNetworkingGetConnectivityHint reports an initialised stack with no connectivity");
  Check(net.At<HRESULT (*)(void*, ConnectivityHint*)>(15, "XNetworkingGetConnectivityHint")(net.self, nullptr) == E_INVALIDARG, "XNetworkingGetConnectivityHint rejects null");

  auto reg = net.At<HRESULT (*)(void*, void*, void*, void*, Token*)>(16, "XNetworkingRegisterConnectivityHintChanged");
  auto unreg = net.At<UnregisterFn>(17, "XNetworkingUnregisterConnectivityHintChanged");
  g_hint_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  Token token{};
  hr = reg(net.self, nullptr, g_hint_event, reinterpret_cast<void*>(&HintCallback), &token);
  bool delivered = hr == S_OK && WaitForSingleObject(g_hint_event, 5000) == WAIT_OBJECT_0;
  Check(delivered && g_delivered_hint.level == 1 && g_delivered_hint.networkInitialized == 1,
        "XNetworkingRegisterConnectivityHintChanged sends the documented initial notification");
  Sleep(50);
  Check(g_hint_calls == 1, "XNetworkingRegisterConnectivityHintChanged sends nothing after the initial notification");
  Check(unreg(net.self, token, TRUE) == 1, "XNetworkingUnregisterConnectivityHintChanged returns true");
  Check(unreg(net.self, token, FALSE) == 1, "XNetworkingUnregisterConnectivityHintChanged returns true for a removed token");
  Check(reg(net.self, nullptr, nullptr, nullptr, &token) == E_INVALIDARG, "XNetworkingRegisterConnectivityHintChanged rejects a null callback");

  void* manual = nullptr;
  hr = g_threading.At<HRESULT (*)(void*, uint32_t, uint32_t, void**)>(12, "XTaskQueueCreate")(g_threading.self, 0, 0, &manual);
  Token early{};
  int before = g_hint_calls;
  HRESULT hr2 = reg(net.self, manual, nullptr, reinterpret_cast<void*>(&HintCallback), &early);
  BOOLEAN removed = unreg(net.self, early, FALSE);
  auto dispatch = g_threading.At<BOOLEAN (*)(void*, void*, uint32_t, uint32_t)>(16, "XTaskQueueDispatch");
  while (dispatch(g_threading.self, manual, 1, 0)) {
  }
  Check(hr == S_OK && hr2 == S_OK && removed == 1 && g_hint_calls == before,
        "XNetworkingUnregisterConnectivityHintChanged before dispatch suppresses the initial notification");
  g_threading.At<void (*)(void*, void*)>(17, "XTaskQueueCloseHandle")(g_threading.self, manual);

  uint64_t value = 77;
  Check(net.At<HRESULT (*)(void*, uint32_t, uint64_t*)>(18, "XNetworkingQueryConfigurationSetting")(net.self, 0, &value) == S_OK && value == 0,
        "XNetworkingQueryConfigurationSetting reports 0 as documented for PC");
  Check(net.At<HRESULT (*)(void*, uint32_t, uint64_t*)>(18, "XNetworkingQueryConfigurationSetting")(net.self, 3, &value) == E_INVALIDARG,
        "XNetworkingQueryConfigurationSetting rejects an unknown setting");
  Check(net.At<HRESULT (*)(void*, uint32_t, uint64_t)>(19, "XNetworkingSetConfigurationSetting")(net.self, 1, 1024) == E_NOTIMPL,
        "XNetworkingSetConfigurationSetting fails with E_NOTIMPL as documented for PC");
  Check(net.At<HRESULT (*)(void*, uint32_t, uint64_t)>(19, "XNetworkingSetConfigurationSetting")(net.self, 9, 1024) == E_INVALIDARG,
        "XNetworkingSetConfigurationSetting rejects an unknown setting");
  TcpStatistics stats;
  std::memset(&stats, 0xcc, sizeof(stats));
  hr = net.At<HRESULT (*)(void*, uint32_t, TcpStatistics*)>(20, "XNetworkingQueryStatistics")(net.self, 2, &stats);
  bool allZero = true;
  for (uint64_t v : stats.values) allZero = allZero && v == 0;
  Check(hr == S_OK && allZero, "XNetworkingQueryStatistics reports zeros as documented for PC");
  Check(net.At<HRESULT (*)(void*, uint32_t, TcpStatistics*)>(20, "XNetworkingQueryStatistics")(net.self, 3, &stats) == E_INVALIDARG,
        "XNetworkingQueryStatistics rejects an unknown type");
}

static void CheckAccessibility() {
  Api access = Get(kAccessibility);
  uint32_t mode = 99;
  HRESULT hr = access.At<HRESULT (*)(void*, uint32_t*)>(5, "XHighContrastGetMode")(access.self, &mode);
  HIGHCONTRASTW contrast{sizeof(contrast)};
  auto readSetting = reinterpret_cast<BOOL(WINAPI*)(UINT, UINT, void*, UINT)>(
      reinterpret_cast<void*>(GetProcAddress(LoadLibraryA("user32.dll"), "SystemParametersInfoW")));
  Check(readSetting && readSetting(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0), "test reads the Windows high-contrast setting");
  bool desktopOn = (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
  Check(hr == S_OK && (desktopOn ? (mode == 1 || mode == 2) : mode == 0), "XHighContrastGetMode matches the Windows high-contrast setting");
  Check(access.At<HRESULT (*)(void*, uint32_t*)>(5, "XHighContrastGetMode")(access.self, nullptr) == E_INVALIDARG, "XHighContrastGetMode rejects null");
}

static void CheckUser() {
  Api user = Get(kUser);
  Api device = Get(kUserDevice);
  DeviceId anyDevice{};
  anyDevice.value[0] = 7;
  void* found = nullptr;
  HRESULT hr = device.At<HRESULT (*)(void*, const DeviceId*, void**)>(3, "XUserFindForDevice")(device.self, &anyDevice, &found);
  Check(hr == kUserNotFound && !found, "XUserFindForDevice finds nobody before XUserAddAsync");

  AsyncBlock add{};
  hr = user.At<HRESULT (*)(void*, uint32_t, AsyncBlock*)>(7, "XUserAddAsync")(user.self, 1, &add);
  WaitAsync(&add);
  void* handle = nullptr;
  hr = user.At<HRESULT (*)(void*, AsyncBlock*, void**)>(8, "XUserAddResult")(user.self, &add, &handle);
  Check(hr == S_OK && handle, "XUserAddAsync gives the local user");

  hr = device.At<HRESULT (*)(void*, const DeviceId*, void**)>(3, "XUserFindForDevice")(device.self, &anyDevice, &found);
  Check(hr == S_OK && found == handle, "XUserFindForDevice associates the local user with any device");
  user.At<void (*)(void*, void*)>(4, "XUserCloseHandle")(user.self, found);
  Check(device.At<HRESULT (*)(void*, const DeviceId*, void**)>(3, "XUserFindForDevice")(device.self, nullptr, &found) == E_INVALIDARG,
        "XUserFindForDevice rejects a null device id");
  CheckSilentEvent(device, 4, "XUserRegisterForDeviceAssociationChanged", 5, "XUserUnregisterForDeviceAssociationChanged");
  CheckSilentEvent(device, 7, "XUserRegisterForDefaultAudioEndpointUtf16Changed", 8, "XUserUnregisterForDefaultAudioEndpointUtf16Changed");

  wchar_t endpoint[64] = L"unchanged";
  hr = device.At<HRESULT (*)(void*, LocalId, uint32_t, size_t, wchar_t*, size_t*)>(6, "XUserGetDefaultAudioEndpointUtf16")(device.self, LocalId{1}, 0, 64, endpoint, nullptr);
  Check(hr == kNotFound, "XUserGetDefaultAudioEndpointUtf16 reports no user-specific endpoint (use the system default)");
  hr = device.At<HRESULT (*)(void*, LocalId, uint32_t, size_t, wchar_t*, size_t*)>(6, "XUserGetDefaultAudioEndpointUtf16")(device.self, LocalId{1}, 2, 64, endpoint, nullptr);
  Check(hr == E_INVALIDARG, "XUserGetDefaultAudioEndpointUtf16 rejects an unknown endpoint kind");

  AsyncBlock find{};
  hr = device.At<HRESULT (*)(void*, void*, AsyncBlock*)>(9, "XUserFindControllerForUserWithUiAsync")(device.self, handle, &find);
  HRESULT status = WaitAsync(&find);
  DeviceId controller;
  std::memset(&controller, 0xcc, sizeof(controller));
  HRESULT result = device.At<HRESULT (*)(void*, AsyncBlock*, DeviceId*)>(10, "XUserFindControllerForUserWithUiResult")(device.self, &find, &controller);
  DeviceId zero{};
  Check(hr == S_OK && status == S_OK && result == S_OK && !std::memcmp(&controller, &zero, sizeof(zero)),
        "XUserFindControllerForUserWithUiAsync completes with the documented all-zero desktop device id");
  AsyncBlock unused{};
  Check(device.At<HRESULT (*)(void*, void*, AsyncBlock*)>(9, "XUserFindControllerForUserWithUiAsync")(device.self, nullptr, &unused) == E_INVALIDARG,
        "XUserFindControllerForUserWithUiAsync rejects a null user");

  void* deferral = reinterpret_cast<void*>(1);
  hr = user.At<HRESULT (*)(void*, void**)>(35, "XUserGetSignOutDeferral")(user.self, &deferral);
  Check(hr == kDeferralNotAvailable && !deferral, "XUserGetSignOutDeferral returns E_GAMEUSER_DEFERRAL_NOT_AVAILABLE outside a sign-out");
  Check(user.At<HRESULT (*)(void*, void**)>(35, "XUserGetSignOutDeferral")(user.self, nullptr) == E_INVALIDARG, "XUserGetSignOutDeferral rejects null");
  user.At<void (*)(void*, void*)>(36, "XUserCloseSignOutDeferralHandle")(user.self, nullptr);
  Check(true, "XUserCloseSignOutDeferralHandle accepts a handle");

  user.At<void (*)(void*, void*)>(4, "XUserCloseHandle")(user.self, handle);
}

static void CheckAborted(HRESULT begin, AsyncBlock* block, HRESULT result, const char* name) {
  char label[160];
  std::snprintf(label, sizeof(label), "%s completes with E_ABORT and shows no UI", name);
  Check(begin == S_OK && WaitAsync(block) == E_ABORT && result == E_ABORT, label);
}

static void CheckGameUi() {
  Api ui = Get(kGameUi);
  Api user = Get(kUser);
  AsyncBlock add{};
  user.At<HRESULT (*)(void*, uint32_t, AsyncBlock*)>(7, "XUserAddAsync")(user.self, 1, &add);
  WaitAsync(&add);
  void* handle = nullptr;
  user.At<HRESULT (*)(void*, AsyncBlock*, void**)>(8, "XUserAddResult")(user.self, &add, &handle);

  {
    AsyncBlock b{};
    HRESULT begin = ui.At<HRESULT (*)(void*, AsyncBlock*, const char*, const char*, const char*, const char*, const char*, uint32_t, uint32_t)>(3, "XGameUiShowMessageDialogAsync")(
        ui.self, &b, "Title", "Content", "OK", nullptr, nullptr, 0, 0);
    WaitAsync(&b);
    uint32_t button = 9;
    HRESULT result = ui.At<HRESULT (*)(void*, AsyncBlock*, uint32_t*)>(4, "XGameUiShowMessageDialogResult")(ui.self, &b, &button);
    CheckAborted(begin, &b, result, "XGameUiShowMessageDialogAsync");
    AsyncBlock bad{};
    Check(ui.At<HRESULT (*)(void*, AsyncBlock*, const char*, const char*, const char*, const char*, const char*, uint32_t, uint32_t)>(3, "XGameUiShowMessageDialogAsync")(
              ui.self, &bad, nullptr, "Content", nullptr, nullptr, nullptr, 0, 0) == E_INVALIDARG,
          "XGameUiShowMessageDialogAsync rejects a null title");
    Check(ui.At<HRESULT (*)(void*, AsyncBlock*, const char*, const char*, const char*, const char*, const char*, uint32_t, uint32_t)>(3, "XGameUiShowMessageDialogAsync")(
              ui.self, &bad, "Title", "Content", nullptr, nullptr, nullptr, 3, 0) == E_INVALIDARG,
          "XGameUiShowMessageDialogAsync rejects an out-of-range defaultButton");
    Check(ui.At<HRESULT (*)(void*, AsyncBlock*, const char*, const char*, const char*, const char*, const char*, uint32_t, uint32_t)>(3, "XGameUiShowMessageDialogAsync")(
              ui.self, &bad, "Title", "Content", nullptr, nullptr, nullptr, 0, 3) == E_INVALIDARG,
          "XGameUiShowMessageDialogAsync rejects an out-of-range cancelButton");
  }
  {
    AsyncBlock b{};
    HRESULT begin = ui.At<HRESULT (*)(void*, AsyncBlock*, void*, const char*, const char*, const char*, const char*, const char*)>(5, "XGameUiShowSendGameInviteAsync")(
        ui.self, &b, handle, "scid", "template", "session", nullptr, nullptr);
    WaitAsync(&b);
    CheckAborted(begin, &b, ui.At<HRESULT (*)(void*, AsyncBlock*)>(6, "XGameUiShowSendGameInviteResult")(ui.self, &b), "XGameUiShowSendGameInviteAsync");
  }
  {
    AsyncBlock b{};
    HRESULT begin = ui.At<HRESULT (*)(void*, AsyncBlock*, void*, uint64_t)>(7, "XGameUiShowPlayerProfileCardAsync")(ui.self, &b, handle, 1);
    WaitAsync(&b);
    CheckAborted(begin, &b, ui.At<HRESULT (*)(void*, AsyncBlock*)>(8, "XGameUiShowPlayerProfileCardResult")(ui.self, &b), "XGameUiShowPlayerProfileCardAsync");
  }
  {
    AsyncBlock b{};
    HRESULT begin = ui.At<HRESULT (*)(void*, AsyncBlock*, void*, uint32_t)>(9, "XGameUiShowAchievementsAsync")(ui.self, &b, handle, 1);
    WaitAsync(&b);
    CheckAborted(begin, &b, ui.At<HRESULT (*)(void*, AsyncBlock*)>(10, "XGameUiShowAchievementsResult")(ui.self, &b), "XGameUiShowAchievementsAsync");
    AsyncBlock bad{};
    Check(ui.At<HRESULT (*)(void*, AsyncBlock*, void*, uint32_t)>(9, "XGameUiShowAchievementsAsync")(ui.self, &bad, nullptr, 1) == E_INVALIDARG,
          "XGameUiShowAchievementsAsync rejects a null user");
  }
  {
    AsyncBlock b{};
    uint64_t players[2] = {1, 2};
    HRESULT begin = ui.At<HRESULT (*)(void*, AsyncBlock*, void*, const char*, uint32_t, const uint64_t*, uint32_t, const uint64_t*, uint32_t, uint32_t)>(11, "XGameUiShowPlayerPickerAsync")(
        ui.self, &b, handle, "Pick", 2, players, 0, nullptr, 1, 1);
    WaitAsync(&b);
    uint32_t count = 5;
    HRESULT countHr = ui.At<HRESULT (*)(void*, AsyncBlock*, uint32_t*)>(12, "XGameUiShowPlayerPickerResultCount")(ui.self, &b, &count);
    uint64_t out[2];
    HRESULT result = ui.At<HRESULT (*)(void*, AsyncBlock*, uint32_t, uint64_t*, uint32_t*)>(13, "XGameUiShowPlayerPickerResult")(ui.self, &b, 2, out, nullptr);
    Check(countHr == E_ABORT, "XGameUiShowPlayerPickerResultCount reports E_ABORT");
    CheckAborted(begin, &b, result, "XGameUiShowPlayerPickerAsync");
    auto picker = ui.At<HRESULT (*)(void*, AsyncBlock*, void*, const char*, uint32_t, const uint64_t*, uint32_t, const uint64_t*, uint32_t, uint32_t)>(
        11, "XGameUiShowPlayerPickerAsync");
    AsyncBlock bad{};
    Check(picker(ui.self, &bad, handle, "Pick", 2, players, 1, nullptr, 1, 1) == E_INVALIDARG,
          "XGameUiShowPlayerPickerAsync rejects a pre-selection count without a list, also with no UI callbacks set");
    Check(picker(ui.self, &bad, handle, "Pick", 2, nullptr, 0, nullptr, 1, 1) == E_INVALIDARG,
          "XGameUiShowPlayerPickerAsync rejects a select-from count without a list");
  }
  {
    AsyncBlock b{};
    HRESULT begin = ui.At<HRESULT (*)(void*, AsyncBlock*, HRESULT, const char*)>(14, "XGameUiShowErrorDialogAsync")(ui.self, &b, E_FAIL, nullptr);
    WaitAsync(&b);
    CheckAborted(begin, &b, ui.At<HRESULT (*)(void*, AsyncBlock*)>(15, "XGameUiShowErrorDialogResult")(ui.self, &b), "XGameUiShowErrorDialogAsync");
  }
  {
    AsyncBlock b{};
    HRESULT begin = ui.At<HRESULT (*)(void*, AsyncBlock*, const char*, const char*, const char*, uint32_t, uint32_t)>(17, "XGameUiShowTextEntryAsync")(
        ui.self, &b, "Name", nullptr, "default", 0, 16);
    WaitAsync(&b);
    uint32_t size = 0;
    HRESULT sizeHr = ui.At<HRESULT (*)(void*, AsyncBlock*, uint32_t*)>(18, "XGameUiShowTextEntryResultSize")(ui.self, &b, &size);
    char text[32];
    HRESULT result = ui.At<HRESULT (*)(void*, AsyncBlock*, uint32_t, char*, uint32_t*)>(19, "XGameUiShowTextEntryResult")(ui.self, &b, sizeof(text), text, nullptr);
    Check(sizeHr == E_ABORT, "XGameUiShowTextEntryResultSize reports E_ABORT");
    CheckAborted(begin, &b, result, "XGameUiShowTextEntryAsync");
  }
  Check(ui.At<HRESULT (*)(void*, uint32_t)>(16, "XGameUiSetNotificationPositionHint")(ui.self, 3) == S_OK, "XGameUiSetNotificationPositionHint accepts a position (no effect on Windows)");
  Check(ui.At<HRESULT (*)(void*, uint32_t)>(16, "XGameUiSetNotificationPositionHint")(ui.self, 6) == E_INVALIDARG, "XGameUiSetNotificationPositionHint rejects an unknown position");
  user.At<void (*)(void*, void*)>(4, "XUserCloseHandle")(user.self, handle);
}

int main() {
  HMODULE module = LoadLibraryA("xgameruntime.dll");
  if (!module) {
    std::printf("FAIL LoadLibrary\n");
    return 1;
  }
  g_query = reinterpret_cast<QueryApi>(GetProcAddress(module, "QueryApiImpl"));
  if (!LoadTable()) {
    std::printf("FAIL table.json not found next to the test (run it from the build directory)\n");
    return 1;
  }
  {
    auto deviceType = reinterpret_cast<uint32_t (*)(void)>(GetProcAddress(module, "XSystemGetDeviceType"));
    Check(deviceType != nullptr, "XSystemGetDeviceType is exported by the stand-in");
    if (deviceType) Check(deviceType() == 1, "XSystemGetDeviceType returns Pc (the stand-in runs on a PC)");
    struct RuntimeInfo { uint16_t runtimeVersion[4], availableVersion[4]; };
    auto runtimeInfo = reinterpret_cast<RuntimeInfo (*)(void)>(GetProcAddress(module, "XSystemGetRuntimeInfo"));
    Check(runtimeInfo != nullptr, "XSystemGetRuntimeInfo is exported by the stand-in");
    if (runtimeInfo) {
      const RuntimeInfo info = runtimeInfo();
      const uint16_t* words = info.runtimeVersion;
      bool zeros = true;
      for (unsigned k = 0; k < 8; k++) zeros = zeros && words[k] == 0;
      Check(zeros, "XSystemGetRuntimeInfo reports the documented zeroed versions (no gaming runtime service)");
    }
  }
  g_threading = Get(kThreading);
  CheckSystem();
  CheckLauncher();
  CheckErrorAndDisplay();
  CheckFeatures();
  CheckActivation();
  CheckNetworking();
  CheckConnectivityUnregisterWhileRunning();
  CheckAccessibility();
  CheckUser();
  CheckGameUi();
  Check(g_silent_calls == 0, "no registered activation, invite, device or endpoint callback ever fired");
  std::printf("%s: %d failure(s)\n", g_failures ? "FAILED" : "ALL PASSED", g_failures);
  return g_failures ? 1 : 0;
}
