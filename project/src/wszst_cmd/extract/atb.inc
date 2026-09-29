static enumError extract_atb_file (ccp arg, ccp basedir, uint depth)
{
	// Grip .res animation sections share the header shape of the magic-less ATB
	if (is_ext (arg, ".banm"))
		return ERR_NOTHING_TO_DO;
	u8 *raw = 0;
	size_t raw_size = 0;
	enumError err = LoadFileAlloc (arg, 0, 0, &raw, &raw_size, 0, 0, 0, false);
	if (err || raw_size < 20 || raw_size > UINT_MAX || !IsATB (raw, (uint)raw_size))
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	atb_archive_t atb;
	err = ScanATB (&atb, raw, (uint)raw_size);
	if (err)
	{
		FREE (raw);
		return ERR_NOTHING_TO_DO;
	}

	char dest[PATH_MAX];
	beside_source_dest (dest, sizeof (dest), arg);
	if (verbose >= 0 || testmode)
		fprintf (stdlog, "%s%sEXTRACT ATB:%s -> %s/ (%u textures, %u patterns, %u banks)\n",
			verbose > 0 ? "\n" : "", testmode ? "WOULD " : "", arg, dest, atb.num_textures,
			atb.num_patterns, atb.num_banks);

	if (!testmode)
	{
		char setup_path[PATH_MAX];
		snprintf (
			setup_path, sizeof (setup_path), "%s/%satb-setup.txt", dest, basedir ? basedir : "");
		File_t SF;
		CreateFileOpt (&SF, true, setup_path, false, arg);
		FILE *f = SF.f;

		if (f)
		{
			fprintf (f, "# ATB setup manifest\n");
			fprintf (f, "references %u\n\n", atb.num_references);
		}

		for (uint i = 0; i < atb.num_textures; i++)
		{
			atb_texture_t *t = &atb.textures[i];
			char raw_name[64], png_name[64];
			snprintf (raw_name, sizeof (raw_name), "tex_%03u.raw", i);
			snprintf (png_name, sizeof (png_name), "tex_%03u.png", i);

			if (f)
				fprintf (f, "texture %u format=%u bpp=%u pal_size=%u size=%u,%u raw=%s png=%s\n", i,
					t->format, t->bpp, t->palette_size, t->width, t->height, raw_name, png_name);

			char raw_path[PATH_MAX];
			snprintf (
				raw_path, sizeof (raw_path), "%s/%s%s", dest, basedir ? basedir : "", raw_name);
			File_t RF;
			CreateFileOpt (&RF, true, raw_path, false, arg);
			if (RF.f)
			{
				if (t->palette_size > 0 && t->palette_data)
					fwrite (t->palette_data, 1, t->palette_size * 2, RF.f);
				if (t->image_size > 0 && t->image_data)
					fwrite (t->image_data, 1, t->image_size, RF.f);
			}
			ResetFile (&RF, opt_preserve);

			// Decode to PNG
			uint gx_fmt = 6;
			uint pal_fmt = 2;
			switch (t->format)
			{
				case 0x00:
					gx_fmt = 6;
					break;
				case 0x01:
					gx_fmt = 5;
					break;
				case 0x02:
					gx_fmt = 4;
					break;
				case 0x03:
					gx_fmt = 9;
					pal_fmt = 2;
					break;
				case 0x04:
					gx_fmt = 8;
					pal_fmt = 2;
					break;
				case 0x05:
				case 0x09:
					gx_fmt = 3;
					break;
				case 0x06:
					gx_fmt = 2;
					break;
				case 0x07:
					gx_fmt = 1;
					break;
				case 0x08:
					gx_fmt = 0;
					break;
				case 0x0a:
					gx_fmt = 14;
					break;
				default:
					gx_fmt = 6;
					break;
			}

			if (t->width && t->height && t->image_data)
			{
				u8 *rgba = 0;
				if (!DecodeGXTexture_RGBA (&rgba, t->width, t->height, gx_fmt, t->image_data,
						t->image_size, t->palette_data, t->palette_size, pal_fmt))
				{
					char png_path[PATH_MAX];
					snprintf (png_path, sizeof (png_path), "%s/%s%s", dest, basedir ? basedir : "",
						png_name);
					SaveDecodedRGBAToPNG (rgba, t->width, t->height, &be_func, png_path, 0, true);
				}
			}
		}

		if (f)
		{
			fprintf (f, "\n# Patterns\n");
			for (uint i = 0; i < atb.num_patterns; i++)
			{
				atb_pattern_t *p = &atb.patterns[i];
				fprintf (f, "pattern %u center=%d,%d size=%d,%d\n", i, p->center_x, p->center_y,
					p->width, p->height);
				for (uint l = 0; l < p->layer_count; l++)
				{
					atb_layer_t *layer = &p->layers[l];
					fprintf (f,
						"layer pattern=%u tex=%d alpha=%u flip=%u coord=%d,%d,%d,%d shift=%d,%d "
						"vtx=%d,%d,%d,%d,%d,%d,%d,%d\n",
						i, layer->texture_index, layer->alpha, layer->flip, layer->tex_coord_tl_x,
						layer->tex_coord_tl_y, layer->tex_coord_w, layer->tex_coord_h,
						layer->shift_x, layer->shift_y, layer->vtx_tl_x, layer->vtx_tl_y,
						layer->vtx_tr_x, layer->vtx_tr_y, layer->vtx_br_x, layer->vtx_br_y,
						layer->vtx_bl_x, layer->vtx_bl_y);
				}
			}

			fprintf (f, "\n# Banks\n");
			for (uint i = 0; i < atb.num_banks; i++)
			{
				atb_bank_t *b = &atb.banks[i];
				fprintf (f, "bank %u\n", i);
				for (uint k = 0; k < b->frame_count; k++)
				{
					atb_anim_frame_t *fr = &b->frames[k];
					fprintf (f, "frame bank=%u pattern=%d length=%d shift=%d,%d flip=%d unk=%d\n",
						i, fr->pattern_index, fr->frame_length, fr->shift_x, fr->shift_y, fr->flip,
						fr->unk);
				}
			}
			ResetFile (&SF, opt_preserve);
		}
	}

	ResetATB (&atb);
	FREE (raw);

	if (!err && !testmode)
	{
		enumError sub = extract_tree_complete (dest, depth + 1);
		if (err < sub)
			err = sub;
	}
	return err;
}
