import test from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { resolve } from 'node:path';
import { packCaptureFlick, decodeCaptureFlick, captureFlightPoint } from '../web/capture-trajectory.js';

test('browser trajectory matches the authoritative native decoder at hit boundaries and input extremes', () => {
  const core = resolve(process.env.DIGIVICE_TEST_CORE_PATH || 'build/digivice-core');
  for (const dx of [-160, -49, -48, -1, 0, 1, 48, 49, 160]) {
    for (const reach of [0, 131, 132, 133, 179, 180, 181, 227, 228, 229, 255]) {
      const value = (dx + 160) * 256 + reach;
      const native = JSON.parse(execFileSync(core, ['--flick-trajectory', String(value)], { encoding: 'utf8' }));
      assert.deepEqual(decodeCaptureFlick(value), native, `dx=${dx}, reach=${reach}`);
    }
  }
});

test('release velocity maps to bounded quantized input without using game state or randomness', () => {
  assert.equal(packCaptureFlick({ vx: 0, vy: -1000 / 412 }), 41140);
  assert.equal(decodeCaptureFlick(packCaptureFlick({ vx: 0, vy: -.25 })).hit, false);
  assert.equal(decodeCaptureFlick(packCaptureFlick({ vx: 4, vy: -2.43 })).hit, false);
  for (const bad of [NaN, Infinity, undefined]) {
    assert.equal(packCaptureFlick({ vx: bad, vy: -1 }), null);
    assert.equal(packCaptureFlick({ vx: 0, vy: bad }), null);
  }
  assert.equal(packCaptureFlick({ vx: 0, vy: 0 }), null);
  assert.equal(packCaptureFlick({ vx: 0, vy: 1 }), null);
  for (const bad of [-1, 82176, 1.2, NaN, '41140']) assert.equal(decodeCaptureFlick(bad), null);
});

test('cosmetic flight ends at the same quantized landing and clamps elapsed animation', () => {
  const value = 41140;
  assert.deepEqual(captureFlightPoint(value, -1), { x: 206, y: 300, radius: 21 });
  const end = captureFlightPoint(value, 2), aim = decodeCaptureFlick(value);
  assert.ok(Math.abs(end.x - aim.landingX) < 1e-9 && Math.abs(end.y - aim.landingY) < 1e-9);
  assert.equal(end.radius, 11);
  assert.equal(captureFlightPoint(-1, .5), null);
  assert.equal(captureFlightPoint(value, NaN), null);
});
