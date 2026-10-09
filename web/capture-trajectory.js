// Version-1 input packing and cosmetic flight path. The native core resolves
// impact and capture, and is checked against this decoder in the host tests.
export const CAPTURE_STAGE = Object.freeze({ size: 412, launchX: 206, launchY: 300, targetX: 206, targetY: 120, targetRadius: 48 });
const clamp = (n, lo, hi) => Math.max(lo, Math.min(hi, n));
export function packCaptureFlick({ vx, vy }) {
  if (!Number.isFinite(vx) || !Number.isFinite(vy) || vy >= 0) return null;
  const dx = clamp(Math.round(vx * CAPTURE_STAGE.size * 0.18), -160, 160);
  const reach = clamp(Math.round(-vy * CAPTURE_STAGE.size * 0.18), 0, 255);
  return (dx + 160) * 256 + reach;
}
export function decodeCaptureFlick(value) {
  if (!Number.isSafeInteger(value) || value < 0 || value > 82175) return null;
  const dx = Math.floor(value / 256) - 160, reach = value % 256;
  return { inputVersion: 1, landingX: 206 + dx, landingY: 300 - reach, hit: dx * dx + (180 - reach) ** 2 <= 48 ** 2 };
}
export function captureFlightPoint(value, progress, start = { x: 206, y: 300 }) {
  const aim = decodeCaptureFlick(value);
  if (!aim || !Number.isFinite(progress)) return null;
  const t = clamp(progress, 0, 1);
  return { x: start.x + (aim.landingX - start.x) * t, y: start.y + (aim.landingY - start.y) * t - Math.sin(t * Math.PI) * 28, radius: 21 - t * 10 };
}
