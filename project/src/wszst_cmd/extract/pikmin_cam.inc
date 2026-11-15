// Pikmin 1 "camera/camepara.bin" camera parameters (PcamCameraParameters in the decompilation): BE {s32 version,
// s32 CAMERA_SWITCH, then the float parameters in declaration order (55 in the current version, the older files have
// 54)}. Written as "<name>.bin.json" with the decompilation's parameter names.
static const char *const pcam_names[55] = {
	"NEAR_LOW_DISTANCE",
	"NEAR_LOW_ANGLE",
	"NEAR_LOW_FOV",
	"NEAR_LOW_WATCH_ADJUSTMENT",
	"NEAR_LOW_NAVI_WATCH_WEIGHT",
	"NEAR_LOW_BLUR",
	"MIDDLE_LOW_DISTANCE",
	"MIDDLE_LOW_ANGLE",
	"MIDDLE_LOW_FOV",
	"MIDDLE_LOW_WATCH_ADJUSTMENT",
	"MIDDLE_LOW_NAVI_WATCH_WEIGHT",
	"MIDDLE_LOW_BLUR",
	"FAR_LOW_DISTANCE",
	"FAR_LOW_ANGLE",
	"FAR_LOW_FOV",
	"FAR_LOW_WATCH_ADJUSTMENT",
	"FAR_LOW_NAVI_WATCH_WEIGHT",
	"FAR_LOW_BLUR",
	"NEAR_HIGH_DISTANCE",
	"NEAR_HIGH_ANGLE",
	"NEAR_HIGH_FOV",
	"NEAR_HIGH_WATCH_ADJUSTMENT",
	"NEAR_HIGH_NAVI_WATCH_WEIGHT",
	"NEAR_HIGH_BLUR",
	"MIDDLE_HIGH_DISTANCE",
	"MIDDLE_HIGH_ANGLE",
	"MIDDLE_HIGH_FOV",
	"MIDDLE_HIGH_WATCH_ADJUSTMENT",
	"MIDDLE_HIGH_NAVI_WATCH_WEIGHT",
	"MIDDLE_HIGH_BLUR",
	"FAR_HIGH_DISTANCE",
	"FAR_HIGH_ANGLE",
	"FAR_HIGH_FOV",
	"FAR_HIGH_WATCH_ADJUSTMENT",
	"FAR_HIGH_NAVI_WATCH_WEIGHT",
	"FAR_HIGH_BLUR",
	"NAVI_ATTENTION_DISTANCE",
	"NAVI_ATTENTION_ANGLE",
	"NAVI_ATTENTION_FOV",
	"NAVI_ATTENTION_WATCH_ADJUSTMENT",
	"NAVI_ATTENTION_NAVI_WATCH_WEIGHT",
	"NAVI_ATTENTION_BLUR",
	"HOMING_SPEED",
	"FOV_HOMING_SPEED",
	"ROTATION_SPEED",
	"TRANSLATION_SPEED",
	"ATTENTION_HOMING_SPEED",
	"ATTENTION_PERIOD",
	"CHANGING_MOTION_PERIOD",
	"ROTATION_BUTTON_THRESHOLD",
	"CAMERA_RESERVED_0",
	"CAMERA_RESERVED_1",
	"IGNORING_PIKI_DISTANCE",
	"VIBRATION_DISTANCE",
	"ATTENTION_ANGLE_MAX_SPEED" };

static enumError extract_pikmin_cam_file (ccp arg, ccp basedir, uint depth)
{
	(void)basedir;
	(void)depth;
	if (!is_ext (arg, ".bin") || !strstr (arg, "camera/"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t sz = 0;
	if (LoadFileAlloc (arg, 0, 0, &raw, &sz, 0, 0, 0, false) || !raw)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	const uint nf = sz >= 8 ? (uint)((sz - 8) / 4) : 0;
	const int ver = sz >= 4 ? (int)rd_be32 (raw) : 0;
	if (sz % 4 || nf < 40 || nf > 55 || ver < 1 || ver > 20)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}
	pcr_ctx_t c;
	memset (&c, 0, sizeof (c));
	c.d = raw;
	c.sz = sz;
	c.ok = true;
	pcr_emit (&c, "{\n  \"version\": %d,\n  \"CAMERA_SWITCH\": %d", ver, (int)(s32)rd_be32 (raw + 4));
	for (uint i = 0; i < nf; i++)
	{
		const u32 w = rd_be32 (raw + 8 + 4 * i);
		float f;
		memcpy (&f, &w, 4);
		pcr_emit (&c, ",\n  \"%s\": %.9g", pcam_names[i], (double)f);
	}
	pcr_emit (&c, "\n}\n");
	char dest[PATH_MAX];
	snprintf (dest, sizeof (dest), "%s.json", arg);
	enumError err = ERR_OK;
	if (testmode)
		fprintf (stdlog, "WOULD EXTRACT CAMERA:%s -> JSON:%s\n", arg, dest);
	else
	{
		File_t F;
		err = CreateFileOpt (&F, true, dest, false, arg);
		if (F.f && fwrite (c.out, 1, c.len, F.f) != c.len)
			err = FILEERROR1 (&F, ERR_WRITE_FAILED, "Writing %u bytes failed: %s\n", (uint)c.len, dest);
		ResetFile (&F, opt_preserve);
		if (verbose >= 0 && !err)
			fprintf (stdlog, "%sEXTRACT CAMERA:%s -> JSON:%s\n", verbose > 0 ? "\n" : "", arg, dest);
	}
	FREE (c.out);
	FREE (raw);
	return err;
}
