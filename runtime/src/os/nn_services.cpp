// System services the title queries during start-up: background daemons
// (nn_ndm), network connection (nn_ac), user account (nn_act), friends
// (nn_fp) and network time (nn_acp). The port is an offline console with a
// single local user: network functions report "no connection" and the
// account is a non-network account, both states every title must handle.

#include "kernel.h"

#include "cafe/export.h"
#include "host/platform.h"

#include <chrono>
#include <cstring>
#include <ctime>

namespace cafe::os {
namespace {

using Result = uint32_t; // nn::Result: bits 29-31 level, 20-28 module

constexpr Result kSuccess = 0;
// Level "status" (non-fatal failure) in the given module.
constexpr Result failure(uint32_t module, uint32_t description) {
    return (5u << 29) | (module << 20) | description;
}
constexpr uint32_t kModuleAc = 0x0C;
constexpr uint32_t kModuleAct = 0x07;
constexpr uint32_t kModuleFp = 0x0B;
constexpr uint32_t kModuleAcp = 0x0E;
constexpr Result kNotConnected = failure(kModuleAc, 0x2580); // no network configured
constexpr Result kNoNetworkAccount = failure(kModuleAct, 0x1100);
constexpr Result kFpOffline = failure(kModuleFp, 0x300);
constexpr Result kNoNetworkTime = failure(kModuleAcp, 0x100);

// ---------------------------------------------------------------- nn_ndm
Result ndm_ok() { return kSuccess; }
Result ndm_daemons(uint32_t) { return kSuccess; }
Result ndm_home_button(int32_t) { return kSuccess; }

// ----------------------------------------------------------------- nn_ac
constexpr uint32_t kStatusFailed = 0xFFFFFFFF;
Result ac_ok() { return kSuccess; }
Result ac_connect_async() { return kNotConnected; }
Result ac_get_status(be<uint32_t>* status) {
    if (status) *status = kStatusFailed;
    return kSuccess;
}
Result ac_is_application_connected(uint8_t* connected) {
    if (connected) *connected = 0;
    return kSuccess;
}
Result ac_get_address(be<uint32_t>* address) {
    if (address) *address = 0;
    return kNotConnected;
}
Result ac_get_last_error_code(be<uint32_t>* code) {
    if (code) *code = 0;
    return kSuccess;
}

// ---------------------------------------------------------------- nn_act
constexpr uint8_t kSlot = 1;
constexpr uint32_t kPersistentId = 0x80000001;
Result act_ok() { return kSuccess; }
uint8_t act_slot() { return kSlot; }
uint8_t act_num_accounts() { return 1; }
bool act_false() { return false; }
uint32_t act_persistent_id() { return kPersistentId; }
uint32_t act_principal_id() { return 0; }
Result act_principal_id_ex(be<uint32_t>* id, uint8_t) {
    if (id) *id = 0;
    return kNoNetworkAccount;
}
Result act_account_id(char* out) {
    if (out) std::strcpy(out, "TTT2Player");
    return kSuccess;
}
Result act_country(char* out) {
    if (out) std::strcpy(out, "GB");
    return kSuccess;
}
// Offset of local time from UTC in microseconds.
int64_t act_utc_offset() {
    return host::utc_offset(std::time(nullptr)) * 1000000;
}
Result act_parental_slot(uint8_t* out, uint8_t) {
    if (out) *out = kSlot;
    return kSuccess;
}
Result act_nfs_password(char*, uint8_t) { return kNoNetworkAccount; }
Result act_host_servers(uint32_t, uint32_t, uint32_t, uint8_t) { return kNoNetworkAccount; }
Result act_nex_token(uint32_t, uint32_t) { return kNoNetworkAccount; }
// Error codes shown to the user look like 102-xxxx for nn_act.
uint32_t act_error_code(be<uint32_t>* result) { return 1020000 + ((result ? uint32_t{*result} : 0) & 0xFFFF); }

// ----------------------------------------------------------------- nn_fp
Result fp_ok() { return kSuccess; }
bool fp_is_initialized() { return true; }
Result fp_add_recent_play(uint32_t, uint32_t) { return kSuccess; }
Result fp_register_account_async(uint32_t, uint32_t) { return kFpOffline; }
uint32_t fp_result_to_error_code(uint32_t result) { return 1210000 + (result & 0xFFFF); }

// ---------------------------------------------------------------- nn_acp
// Network time is kept in timer ticks, like OSTime.
Result ACPConvertNetworkTimeToOSCalendarTime(int64_t time, GuestAddress calendar) {
    ticks_to_calendar_time(time, calendar.value);
    return 0;
}

Result ACPGetNetworkTime(be<int64_t>* time, be<uint32_t>* unknown) {
    if (time) *time = 0;
    if (unknown) *unknown = 0;
    return kNoNetworkTime;
}

} // namespace

CAFE_EXPORT(nn_ndm, Initialize__Q2_2nn3ndmFv, ndm_ok);
CAFE_EXPORT(nn_ndm, Finalize__Q2_2nn3ndmFv, ndm_ok);
CAFE_EXPORT(nn_ndm, SuspendDaemons__Q2_2nn3ndmFUi, ndm_daemons);
CAFE_EXPORT(nn_ndm, ResumeDaemons__Q2_2nn3ndmFUi, ndm_daemons);
CAFE_EXPORT(nn_ndm, EnableHomeButtonMenuAndBgProcess__Q2_2nn3ndmFi, ndm_home_button);

CAFE_EXPORT(nn_ac, Initialize__Q2_2nn2acFv, ac_ok);
CAFE_EXPORT(nn_ac, Finalize__Q2_2nn2acFv, ac_ok);
CAFE_EXPORT(nn_ac, Close__Q2_2nn2acFv, ac_ok);
CAFE_EXPORT(nn_ac, ConnectAsync__Q2_2nn2acFv, ac_connect_async);
CAFE_EXPORT(nn_ac, GetStatus__Q2_2nn2acFPQ3_2nn2ac6Status, ac_get_status);
CAFE_EXPORT(nn_ac, IsApplicationConnected__Q2_2nn2acFPb, ac_is_application_connected);
CAFE_EXPORT(nn_ac, GetAssignedAddress__Q2_2nn2acFPUl, ac_get_address);
CAFE_EXPORT(nn_ac, GetAssignedSubnet__Q2_2nn2acFPUl, ac_get_address);
CAFE_EXPORT(nn_ac, GetLastErrorCode__Q2_2nn2acFPUi, ac_get_last_error_code);

CAFE_EXPORT(nn_act, Initialize__Q2_2nn3actFv, act_ok);
CAFE_EXPORT(nn_act, Finalize__Q2_2nn3actFv, act_ok);
CAFE_EXPORT(nn_act, GetSlotNo__Q2_2nn3actFv, act_slot);
CAFE_EXPORT(nn_act, GetNumOfAccounts__Q2_2nn3actFv, act_num_accounts);
CAFE_EXPORT(nn_act, IsNetworkAccount__Q2_2nn3actFv, act_false);
CAFE_EXPORT(nn_act, HasNfsAccount__Q2_2nn3actFv, act_false);
CAFE_EXPORT(nn_act, GetPersistentId__Q2_2nn3actFv, act_persistent_id);
CAFE_EXPORT(nn_act, GetPrincipalId__Q2_2nn3actFv, act_principal_id);
CAFE_EXPORT(nn_act, GetPrincipalIdEx__Q2_2nn3actFPUiUc, act_principal_id_ex);
CAFE_EXPORT(nn_act, GetAccountId__Q2_2nn3actFPc, act_account_id);
CAFE_EXPORT(nn_act, GetCountry__Q2_2nn3actFPc, act_country);
CAFE_EXPORT(nn_act, GetUtcOffset__Q2_2nn3actFv, act_utc_offset);
CAFE_EXPORT(nn_act, GetParentalControlSlotNoEx__Q2_2nn3actFPUcUc, act_parental_slot);
CAFE_EXPORT(nn_act, GetNfsPasswordEx__Q2_2nn3actFPcUc, act_nfs_password);
CAFE_EXPORT(nn_act, GetHostServerSettings__Q2_2nn3actFP11ACTNnasTypeP10ACTNfsTypePUcUc, act_host_servers);
CAFE_EXPORT(nn_act, AcquireNexServiceToken__Q2_2nn3actFP26ACTNexAuthenticationResultUi, act_nex_token);
CAFE_EXPORT(nn_act, GetErrorCode__Q2_2nn3actFRCQ2_2nn6Result, act_error_code);

CAFE_EXPORT(nn_fp, Initialize__Q2_2nn2fpFv, fp_ok);
CAFE_EXPORT(nn_fp, Finalize__Q2_2nn2fpFv, fp_ok);
CAFE_EXPORT(nn_fp, IsInitialized__Q2_2nn2fpFv, fp_is_initialized);
CAFE_EXPORT(nn_fp, AddRecentPlayRecord__Q2_2nn2fpFPCQ3_2nn2fp16RecentPlayRecordUi, fp_add_recent_play);
CAFE_EXPORT(nn_fp, RegisterAccountAsync__Q2_2nn2fpFPFQ2_2nn6ResultPv_vPv, fp_register_account_async);
CAFE_EXPORT(nn_fp, ResultToErrorCode__Q2_2nn2fpFQ2_2nn6Result, fp_result_to_error_code);

CAFE_EXPORT(nn_acp, ACPGetNetworkTime, ACPGetNetworkTime);
CAFE_EXPORT(nn_acp, ACPConvertNetworkTimeToOSCalendarTime, ACPConvertNetworkTimeToOSCalendarTime);

} // namespace cafe::os
