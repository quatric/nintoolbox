#ifndef LIB_NACP_H
#define LIB_NACP_H

#include "lib-nintendo.h"

// Nintendo Switch Application Control Property (.nacp / control.nacp)
// Official format defined in NintendoSDK (Siglo) ApplicationControlPropertyModel.
// Fixed size: 0x4000 (16384 bytes).

#define NACP_SIZE 0x4000
#define NACP_LANG_COUNT 16

typedef struct nacp_title_t
{
	char name[512];
	char publisher[256];
} nacp_title_t;

typedef struct nacp_t
{
	nacp_title_t titles[NACP_LANG_COUNT];
	char isbn[38];
	u8 startup_user_account;
	u8 addon_content_registration_type;
	u32 attribute_flag;
	u32 supported_language_flag;
	u32 parental_control_flag;
	u8 screenshot;
	u8 video_capture;
	u8 data_loss_confirmation;
	u8 play_log_policy;
	u64 presence_group_id;
	s8 rating_age[32];
	char display_version[17];
	u64 addon_content_base_id;
	u64 save_data_owner_id;
	s64 user_account_save_data_size;
	s64 user_account_save_data_journal_size;
	s64 device_save_data_size;
	s64 device_save_data_journal_size;
	s64 bcat_delivery_cache_storage_size;
	char application_error_code_category[9];
	u64 local_communication_id[8];
	u8 logo_type;
	u8 logo_handling;
	u8 runtime_addon_content_install;
	u8 crash_report;
	u8 hdcp;
	u64 seed_for_pseudo_device_id;
	char bcat_passphrase[66];
	s64 user_account_save_data_size_max;
	s64 user_account_save_data_journal_size_max;
	s64 device_save_data_size_max;
	s64 device_save_data_journal_size_max;
	s64 temporary_storage_size;
	s64 cache_storage_size;
	s64 cache_storage_journal_size;
	s64 cache_storage_data_and_journal_size_max;
	u16 cache_storage_index_max;
	u8 play_log_query_capability;
	u8 repair_flag;
	u8 program_index;
	u8 required_network_service_license_on_launch_flag;
} nacp_t;

bool IsNACP (const u8 *data, uint size);
enumError ParseNACP (nacp_t *nacp, const u8 *data, uint size);
enumError SaveTextNACP (const nacp_t *nacp, ccp dest_path);

#endif // LIB_NACP_H
