// Sequential, bounded-memory Khronos validation of private preview model files.
// Usage: node validate_smo_glbs.cjs <asset-root> <local-gltf-validator-module>
const fs = require('fs');
const path = require('path');
const validator = require(path.resolve(process.argv[3]));
(async () => {
    let checked = 0, errors = 0;
    const root = path.resolve(process.argv[2]);
    for (const entry of fs.readdirSync(root, {withFileTypes: true})) {
        const file = entry.isDirectory() ? path.join(root, entry.name, entry.name + '.glb')
            : path.join(root, entry.name);
        if (!file.endsWith('.glb') || !fs.existsSync(file)) continue;
        const result = await validator.validateBytes(new Uint8Array(fs.readFileSync(file)), {maxIssues: 100});
        checked++;
        if (result.issues.numErrors) {
            errors += result.issues.numErrors;
            console.log(entry.name, JSON.stringify(result.issues.messages.filter(issue => issue.severity === 0)));
        }
    }
    console.log(JSON.stringify({checked, errors}));
    process.exitCode = errors || !checked ? 1 : 0;
})().catch(error => { console.error(error); process.exitCode = 1; });
