import { dirname, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { buildGaragePlan, createGarageAssets, GarageError } from '../service/garage-assets.ts';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
export async function runGarageCli(args: string[]) {
  const command = args[0] ?? 'dry-run';
  if (args.length > 1 || !['dry-run', 'diagnose', 'verify', 'upload'].includes(command)) throw new GarageError('garage_usage', 'Usage: ./scripts/node.sh scripts/garage-assets.ts [dry-run|diagnose|verify|upload]');
  const garage = createGarageAssets({ rootDir: ROOT });
  try {
    if (command === 'diagnose') return await garage.diagnose();
    if (command === 'dry-run') {
      const plan = await buildGaragePlan(ROOT);
      return { mode: 'dry-run', remoteWrites: 0, objects: plan.objects.map(({ id, version, kind, bytes, sha256 }) => ({ id, version, kind, bytes, sha256 })), totalBytes: plan.totalBytes };
    }
    if (command === 'verify') return { mode: 'verify', ...(await garage.verify()) };
    const receipt = await garage.upload();
    return { mode: 'upload', verifiedObjects: receipt.objects.length, verifiedBytes: receipt.objects.reduce((sum, item) => sum + item.bytes, 0) };
  } finally { garage.close(); }
}
if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  try { console.log(JSON.stringify(await runGarageCli(process.argv.slice(2)), null, 2)); }
  catch (error) {
    // Never print raw SDK errors, request headers, .env contents, or credentials.
    console.error(JSON.stringify({ error: error instanceof GarageError ? error.code : 'garage_operation_failed', message: error instanceof GarageError ? error.message : 'The Garage operation failed without a successful completion receipt.' }));
    process.exitCode = 1;
  }
}
