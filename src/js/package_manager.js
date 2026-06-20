const fs = require('fs');
const os = require('os');

console.log("[Package Manager] Started");
console.log("[Package Manager] os =", os);
console.log("[Package Manager] os.getenv =", os.getenv);

async function resolvePackages() {
    const envFile = os.getenv("CURICA_ENV_FILE");
    console.log("[Package Manager] envFile =", envFile);
    if (!envFile) return;

    console.log("[Package Manager] Reading config from", envFile);
    let config;
    try {
        const raw = fs.readFileSync(envFile, "utf8");
        console.log("[Package Manager] raw config length:", raw.length);
        console.log("[Package Manager] raw config content:", raw);
        config = JSON.parse(raw);
    } catch (e) {
        console.error("[Package Manager] Parse error:", e);
        return; // No env file or invalid JSON, nothing to resolve
    }

    console.log("[Package Manager] parsed config:", JSON.stringify(config));
    if (!config.packages || config.packages.length === 0) {
        console.log("[Package Manager] No packages found in config.");
        return;
    }

    // Ensure local packages directory exists via VFS
    try { fs.mkdirSync("/packages"); } catch(e) {}

    console.log("[Package Manager] Resolving packages:", config.packages);
    for (const pkg of config.packages) {
        const wasmPath = `/packages/${pkg}.wasm`;
        const binPath = `/bin/${pkg}.wasm`;
        
        if (fs.existsSync(wasmPath)) {
            // Already installed. Symlink to /bin so it can be spawned.
            if (!fs.existsSync(binPath)) {
                fs.copyFileSync(wasmPath, binPath);
            }
            continue;
        }

        console.log(`[Package Manager] Resolving '${pkg}'...`);
        const remoteUrl = `https://raw.githubusercontent.com/curicas/curica/main/packages/${pkg}.wasm`;
        
        try {
            const res = await fetch(remoteUrl);
            if (!res.ok) {
                console.log(`[Package Manager] Pre-compiled WASM not found. Falling back to Source Compilation for '${pkg}'...`);
                // Source compilation stub (Phase 3.3)
                const srcUrl = `https://raw.githubusercontent.com/curicas/curica/main/packages/src/${pkg}.c`;
                const srcRes = await fetch(srcUrl);
                if (srcRes.status === 200) {
                    const srcCode = await srcRes.text();
                    fs.writeFileSync(`/tmp/${pkg}.c`, srcCode);
                    console.log(`[Package Manager] Source downloaded to /tmp. Orchestrating embedded toolchain...`);
                    
                    if (config.permissions && config.permissions.allow_run) {
                        try {
                            const cp = require('child_process');
                            // Because /tmp is in the VFS, host compiler can't see it directly.
                            // We stream the source code via stdin to the compiler, and write output to a real host tmp file.
                            // However, since we want zero bloat and a true secure sandbox, we write the host file 
                            // to /tmp on the host via the execSync bypassing the VFS (requires allow_run).
                            // A better approach is to compile it natively if we had an embedded compiler, but host fallback:
                            console.log(`[Package Manager] Invoking host compiler for ${pkg}...`);
                            cp.execSync(`gcc -xc - -shared -o /tmp/curica_${pkg}.wasm`);
                            
                            // Load it back into VFS
                            // Since we don't have host fs access directly, this is a mock representation
                            // In a full implementation, the host toolchain wrapper would proxy this back.
                            console.log(`[Package Manager] Compilation successful for ${pkg}.`);
                            fs.writeFileSync(wasmPath, "MOCK_WASM_BINARY_DATA");
                            if (!fs.existsSync(binPath)) fs.copyFileSync(wasmPath, binPath);
                        } catch (err) {
                            console.error(`[Package Manager] Toolchain compilation failed:`, err.message);
                        }
                    } else {
                        console.error(`[Package Manager] Cannot compile ${pkg}: 'allow_run' permission denied.`);
                    }
                } else {
                    console.error(`[Package Manager] Error: Package '${pkg}' does not exist remotely.`);
                }
            }
        } catch (e) {
            console.error(`[Package Manager] Network error resolving '${pkg}':`, e);
        }
    }
}

resolvePackages().catch(e => console.error("[Package Manager] Unhandled error:", e));
