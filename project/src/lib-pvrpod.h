// PowerVR POD (PVRTModelPOD, "AB.POD.2.0") scene reader.
#ifndef SZS_LIB_PVRPOD_H
#define SZS_LIB_PVRPOD_H 1

#include <stddef.h>
#include <stdint.h>
#include "lib-model-glb.h"

#ifdef __cplusplus
extern "C"
{
#endif

	// True when data starts with a POD format-version block ("AB.POD.").
	int IsPowerVRPOD (const uint8_t *data, size_t size);

	// Meshes (skinned through their bone batches), the node hierarchy as
	// joints and the material names. NULL when the file has no mesh.
	model_t *ParsePowerVRPOD (const uint8_t *data, size_t size);

	// Appends the node animation of a POD (e.g. a Miitomo .Anim.pod) to
	// model as one clip named clip_name, matching nodes to joints by name.
	// Returns 1 when a clip was added.
	int ImportPowerVRPODAnim (model_t *model, const uint8_t *data, size_t size, const char *clip_name);

#ifdef __cplusplus
}
#endif

#endif
