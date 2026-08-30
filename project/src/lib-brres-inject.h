#ifndef LIB_BRRES_INJECT_H
#define LIB_BRRES_INJECT_H 1

#include <stddef.h>
#include <stdint.h>
#include "lib-model-dae.h"

#ifdef __cplusplus
extern "C"
{
#endif

	// Inject/replace mesh geometry from a parsed COLLADA DAE model_t into a parent MDL0 binary.
	// Returns 1 on success (allocating *out_data and setting *out_size), or 0 on failure.
	int InjectDAEIntoMDL0 (const uint8_t *mdl0_data, size_t mdl0_size, const model_t *dae_model,
		uint8_t **out_data, size_t *out_size);

	// Inject/replace mesh geometry from a parsed COLLADA DAE model_t into a parent BRRES archive.
	// Returns 1 on success (allocating *out_data and setting *out_size), or 0 on failure.
	int InjectDAEIntoBRRES (const uint8_t *brres_data, size_t brres_size, const model_t *dae_model,
		uint8_t **out_data, size_t *out_size);

	// Inject/replace mesh geometry from a parsed COLLADA DAE model_t into a parent BFRES archive
	// (Wii U FRES / FMDL). Returns 1 on success (allocating *out_data and setting *out_size), or 0
	// on failure.
	int InjectDAEIntoBFRES (const uint8_t *bfres_data, size_t bfres_size, const model_t *dae_model,
		uint8_t **out_data, size_t *out_size);

	// Inject/replace mesh geometry from a parsed COLLADA DAE model_t into a Switch BFRES archive.
	// Switch BFRES: little-endian, absolute s64 pointers, BufferInfo pool, non-interleaved FVTX.
	// Returns 1 on success (allocating *out_data and setting *out_size), or 0 on failure.
	int InjectDAEIntoSwitchBFRES (const uint8_t *data, size_t data_size, const model_t *model,
		uint8_t **out_data, size_t *out_size);

	// Create a new Switch BFRES from a parsed COLLADA DAE model_t.
	// Builds a complete little-endian Switch BFRES v8 file from scratch.
	// Returns 1 on success (allocating *out_data and setting *out_size), or 0 on failure.
	int CreateSwitchBFRES (const model_t *model, uint8_t **out_data, size_t *out_size);

	// Inject/replace mesh geometry from a parsed COLLADA DAE model_t into a parent BCH binary (3DS
	// H3D). Returns 1 on success (allocating *out_data and setting *out_size), or 0 on failure.
	int InjectDAEIntoBCH (const uint8_t *bch_data, size_t bch_size, const model_t *dae_model,
		uint8_t **out_data, size_t *out_size);

	// Inject/replace mesh geometry from a parsed COLLADA DAE model_t into a parent BCRES / CGFX
	// binary (3DS CGFX). Returns 1 on success (allocating *out_data and setting *out_size), or 0 on
	// failure.
	int InjectDAEIntoBCRES (const uint8_t *bcres_data, size_t bcres_size, const model_t *dae_model,
		uint8_t **out_data, size_t *out_size);

	// Inject/replace mesh geometry from a parsed COLLADA DAE model_t into a parent NSBMD binary (DS
	// BMD0). Returns 1 on success (allocating *out_data and setting *out_size), or 0 on failure.
	int InjectDAEIntoNSBMD (const uint8_t *nsbmd_data, size_t nsbmd_size, const model_t *dae_model,
		uint8_t **out_data, size_t *out_size);

	// Inject/replace mesh geometry from a parsed COLLADA DAE model_t into an early DS BMD binary.
	// Returns 1 on success (allocating *out_data and setting *out_size), or 0 on failure.
	int InjectDAEIntoEarlyDSBMD (const uint8_t *bmd_data, size_t bmd_size, const model_t *dae_model,
		uint8_t **out_data, size_t *out_size);

	// Universal dispatcher: detects parent format by header magic and calls the appropriate
	// injector. Supports: BRRES ("bres"), MDL0 ("MDL0"), BFRES ("FRES"), BCH ("BCH\0"), BCRES
	// ("CGFX"), NSBMD ("BMD0"), Early DS BMD. Returns 1 on success (allocating *out_data and
	// setting *out_size), or 0 on failure.
	int InjectDAEIntoModel (const uint8_t *parent_data, size_t parent_size,
		const model_t *dae_model, uint8_t **out_data, size_t *out_size);

#ifdef __cplusplus
}
#endif

#endif // LIB_BRRES_INJECT_H
