///////////////		commands WC24DECRYPT/WC24ENCRYPT/BMS	///////////////
///////////////////////////////////////////////////////////////////////////////

static enumError cmd_wc24 (bool encrypt)
{
	if (n_param < 3)
		return ERROR0 (ERR_SYNTAX,
			encrypt ? "Command WC24ENCRYPT needs: source dest key rsa-key [iv]\n"
					: "Command WC24DECRYPT needs: source dest key\n");

	ParamList_t *param = first_param;
	ccp source = param->arg;
	param = param->next;
	ccp dest = param->arg;
	param = param->next;
	ccp key = param->arg;
	param = param->next;

	if (!encrypt)
		return WC24DecryptFile (source, dest, key);

	if (!param)
		return ERROR0 (ERR_SYNTAX, "Command WC24ENCRYPT needs an RSA private key PEM\n");
	ccp rsa = param->arg;
	param = param->next;
	ccp iv = param ? param->arg : 0;
	return WC24EncryptFile (source, dest, key, rsa, iv);
}

static enumError cmd_bms (void)
{
	if (n_param < 3)
		return ERROR0 (ERR_SYNTAX, "Command BMS needs: script source dest-dir\n");

	ParamList_t *param = first_param;
	ccp script = param->arg;
	param = param->next;
	ccp source = param->arg;
	param = param->next;
	ccp outdir = param->arg;
	return RunBmsScript (script, source, outdir);
}
