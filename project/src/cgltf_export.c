#include "lib-model-glb.h"
#include "cgltf.h"
#include "cgltf_write.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Just a dummy to see if it compiles
int ExportModelToGLB(const model_t *model, const char *out_glb_file) {
    cgltf_options options = {0};
    cgltf_data data = {0};
    data.asset.generator = (char*)"wiimms-szs-tools-plus";
    data.asset.version = (char*)"2.0";
    
    // ... we will fill this in ...
    
    cgltf_result res = cgltf_write_file(&options, out_glb_file, &data);
    return (res == cgltf_result_success) ? 0 : -1;
}

model_t *ParseGLBFile(const char *filename) {
    return NULL;
}
model_t *ParseGLB(const uint8_t *data, size_t size) {
    return NULL;
}
