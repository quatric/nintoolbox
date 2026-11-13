#!/usr/bin/env node
// Validate GLB files with the official Khronos glTF-Validator.
//   npm i gltf-validator   (once, in any dir on NODE_PATH)
//   node tests/khronos-validate-glb.js a.glb [b.glb ...]
// Prints one line per problem; exits nonzero if any file has errors.
const v = require('gltf-validator'), fs = require('fs');
(async () => {
  let bad = 0;
  for (const f of process.argv.slice(2)) {
    const r = await v.validateBytes(new Uint8Array(fs.readFileSync(f)), { uri: f });
    for (const m of r.issues.messages.filter(m => m.severity === 0))
      console.log(`${f}: ${m.code} ${m.pointer}: ${m.message}`);
    if (r.issues.numErrors) bad++;
  }
  process.exit(bad ? 1 : 0);
})();
