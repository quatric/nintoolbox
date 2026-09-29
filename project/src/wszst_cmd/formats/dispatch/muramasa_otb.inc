	// Muramasa (Wii) ".otb"/".nsi" tables have loose, magic-less-looking
	// internal count/offset fields that a generic archive/table sniffer
	// further down this function can mistake for its own format (observed:
	// ".otb" spuriously matched as an unknown/generic member archive). Give
	// the extension-gated Muramasa probes first refusal, before any of the
	// generic sniffers below get a chance to misfire on them.
	enumError err = extract_muramasa_otb_manifest (arg);
	if (err != ERR_NOTHING_TO_DO)
		return err;
