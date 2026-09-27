// SPDX-License-Identifier: GPL-2.0+
#include "lib-nacp.h"
#include <string.h>

static const char *const nacp_languages[NACP_LANG_COUNT] = {
	"AmericanEnglish",
	"BritishEnglish",
	"Japanese",
	"French",
	"German",
	"LatinAmericanSpanish",
	"Spanish",
	"Italian",
	"Dutch",
	"CanadianFrench",
	"Portuguese",
	"Russian",
	"Korean",
	"TraditionalChinese",
	"SimplifiedChinese",
	"BrazilianPortuguese"
};

static const char *const nacp_rating_orgs[12] = {
	"CERO", "GRACGCRB", "GSRMR", "ESRB", "ClassInd", "USK",
	"PEGI", "PEGIPortugal", "PEGIBBFC", "Russian", "ACB", "OFLC"
};

bool IsNACP (const u8 *data, uint size)
{
	if (!data || size != NACP_SIZE)
		return false;

	// At least one title should have valid non-empty ASCII/UTF-8 characters
	bool found_valid_title = false;
	for (int i = 0; i < NACP_LANG_COUNT; i++)
	{
		const u8 *title_ptr = data + i * 0x300;
		if (title_ptr[0] != 0)
		{
			for (int j = 0; j < 512 && title_ptr[j] != 0; j++)
			{
				if (title_ptr[j] >= 0x20 || title_ptr[j] == '\n' || title_ptr[j] == '\r')
				{
					found_valid_title = true;
					break;
				}
			}
		}
	}

	if (!found_valid_title)
		return false;

	// Check display_version at 0x3060: should be null-terminated string
	const char *version = (const char *)(data + 0x3060);
	if (version[0] != 0)
	{
		for (int j = 0; j < 16; j++)
		{
			if (version[j] == 0)
				break;
			if ((u8)version[j] < 0x20 || (u8)version[j] > 0x7e)
				return false;
		}
	}

	return true;
}

enumError ParseNACP (nacp_t *nacp, const u8 *data, uint size)
{
	if (!nacp || !IsNACP (data, size))
		return ERR_INVALID_DATA;

	memset (nacp, 0, sizeof (*nacp));

	for (int i = 0; i < NACP_LANG_COUNT; i++)
	{
		const u8 *ptr = data + i * 0x300;
		memcpy (nacp->titles[i].name, ptr, 512);
		nacp->titles[i].name[511] = 0;
		memcpy (nacp->titles[i].publisher, ptr + 512, 256);
		nacp->titles[i].publisher[255] = 0;
	}

	memcpy (nacp->isbn, data + 0x3000, 37);
	nacp->isbn[37] = 0;

	nacp->startup_user_account = data[0x3025];
	nacp->addon_content_registration_type = data[0x3027];
	nacp->attribute_flag = rd_le32 (data + 0x3028);
	nacp->supported_language_flag = rd_le32 (data + 0x302c);
	nacp->parental_control_flag = rd_le32 (data + 0x3030);
	nacp->screenshot = data[0x3034];
	nacp->video_capture = data[0x3035];
	nacp->data_loss_confirmation = data[0x3036];
	nacp->play_log_policy = data[0x3037];
	nacp->presence_group_id = rd_le64 (data + 0x3038);

	for (int i = 0; i < 32; i++)
		nacp->rating_age[i] = (s8)data[0x3040 + i];

	memcpy (nacp->display_version, data + 0x3060, 16);
	nacp->display_version[16] = 0;

	nacp->addon_content_base_id = rd_le64 (data + 0x3070);
	nacp->save_data_owner_id = rd_le64 (data + 0x3078);
	nacp->user_account_save_data_size = (s64)rd_le64 (data + 0x3080);
	nacp->user_account_save_data_journal_size = (s64)rd_le64 (data + 0x3088);
	nacp->device_save_data_size = (s64)rd_le64 (data + 0x3090);
	nacp->device_save_data_journal_size = (s64)rd_le64 (data + 0x3098);
	nacp->bcat_delivery_cache_storage_size = (s64)rd_le64 (data + 0x30a0);

	memcpy (nacp->application_error_code_category, data + 0x30a8, 8);
	nacp->application_error_code_category[8] = 0;

	for (int i = 0; i < 8; i++)
		nacp->local_communication_id[i] = rd_le64 (data + 0x30b0 + i * 8);

	nacp->logo_type = data[0x30f0];
	nacp->logo_handling = data[0x30f1];
	nacp->runtime_addon_content_install = data[0x30f2];
	nacp->crash_report = data[0x30f6];
	nacp->hdcp = data[0x30f7];
	nacp->seed_for_pseudo_device_id = rd_le64 (data + 0x30f8);

	memcpy (nacp->bcat_passphrase, data + 0x3100, 65);
	nacp->bcat_passphrase[65] = 0;

	nacp->user_account_save_data_size_max = (s64)rd_le64 (data + 0x3148);
	nacp->user_account_save_data_journal_size_max = (s64)rd_le64 (data + 0x3150);
	nacp->device_save_data_size_max = (s64)rd_le64 (data + 0x3158);
	nacp->device_save_data_journal_size_max = (s64)rd_le64 (data + 0x3160);
	nacp->temporary_storage_size = (s64)rd_le64 (data + 0x3168);
	nacp->cache_storage_size = (s64)rd_le64 (data + 0x3170);
	nacp->cache_storage_journal_size = (s64)rd_le64 (data + 0x3178);
	nacp->cache_storage_data_and_journal_size_max = (s64)rd_le64 (data + 0x3180);
	nacp->cache_storage_index_max = rd_le16 (data + 0x3188);

	nacp->play_log_query_capability = data[0x3210];
	nacp->repair_flag = data[0x3211];
	nacp->program_index = data[0x3212];
	nacp->required_network_service_license_on_launch_flag = data[0x3213];

	return ERR_OK;
}

enumError SaveTextNACP (const nacp_t *nacp, ccp dest_path)
{
	if (!nacp || !dest_path)
		return ERR_INVALID_DATA;

	FILE *fp = fopen (dest_path, "w");
	if (!fp)
		return ERR_CANT_CREATE;

	fprintf (fp, "# Nintendo Switch Application Control Property (NACP)\n");
	fprintf (fp, "DisplayVersion:       %s\n", nacp->display_version);
	fprintf (fp, "PresenceGroupId:      0x%016llx\n", (unsigned long long)nacp->presence_group_id);
	fprintf (fp, "AddOnContentBaseId:   0x%016llx\n", (unsigned long long)nacp->addon_content_base_id);
	fprintf (fp, "SaveDataOwnerId:      0x%016llx\n", (unsigned long long)nacp->save_data_owner_id);
	fprintf (fp, "StartupUserAccount:   %u\n", nacp->startup_user_account);
	fprintf (fp, "Screenshot:           %s\n", nacp->screenshot ? "Deny" : "Allow");
	fprintf (fp, "VideoCapture:         %s\n",
		nacp->video_capture == 2 ? "Enable" : (nacp->video_capture == 1 ? "Manual" : "Disable"));
	fprintf (fp, "UserAccountSaveData:  %lld bytes (Journal: %lld bytes)\n",
		(long long)nacp->user_account_save_data_size, (long long)nacp->user_account_save_data_journal_size);
	fprintf (fp, "DeviceSaveData:       %lld bytes (Journal: %lld bytes)\n",
		(long long)nacp->device_save_data_size, (long long)nacp->device_save_data_journal_size);

	if (nacp->isbn[0])
		fprintf (fp, "ISBN:                 %s\n", nacp->isbn);

	fprintf (fp, "\n# Titles by Language:\n");
	for (int i = 0; i < NACP_LANG_COUNT; i++)
	{
		if (nacp->titles[i].name[0] || nacp->titles[i].publisher[0])
		{
			fprintf (fp, "[%s]\n", nacp_languages[i]);
			fprintf (fp, "  Name:      %s\n", nacp->titles[i].name);
			fprintf (fp, "  Publisher: %s\n", nacp->titles[i].publisher);
		}
	}

	fprintf (fp, "\n# Age Ratings:\n");
	for (int i = 0; i < 12; i++)
	{
		if (nacp->rating_age[i] >= 0)
			fprintf (fp, "  %-12s: %d+\n", nacp_rating_orgs[i], (int)nacp->rating_age[i]);
	}

	fclose (fp);
	return ERR_OK;
}
