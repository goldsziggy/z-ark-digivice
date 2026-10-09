import test from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { CAPTURE_RING, sampleCaptureRing, captureRingChance } from '../web/capture-ring.js';

test('generated files match the canonical bounded timing contract', () => {
  execFileSync('python3', ['scripts/generate-capture-ring.py', '--check']);
  assert.ok(Object.isFrozen(CAPTURE_RING.targetRadii));
  assert.ok(Object.isFrozen(CAPTURE_RING.gradeFactors));
});

test('browser/native samples agree at every millisecond for every target band', () => {
  const rows = execFileSync('./build/capture-ring-model-test', ['--dump'], { encoding: 'utf8' }).trim().split('\n');
  assert.equal(rows.length, 4 * CAPTURE_RING.cycleMs);
  for (const row of rows) {
    const columns = row.split(',');
    const [form, phaseMs, radiusQ8, targetRadius, hit, flickValue] = columns.slice(0,6).map(Number);
    const grade=columns[6];
    assert.deepEqual(sampleCaptureRing(phaseMs, form), { phaseMs, radiusQ8, targetRadius, hit: !!hit, flickValue, grade });
    assert.deepEqual(sampleCaptureRing(phaseMs + CAPTURE_RING.cycleMs * 1000000, form), sampleCaptureRing(phaseMs, form));
  }
});

test('one timing factor preserves eligible green and a small nonzero red chance', () => {
  for (let base=0;base<=90;base++) {
    for (const [grade,factor] of [['red',10],['orange',50],['green',100]]) {
      const expected=base ? Math.max(1,Math.floor(base*factor/100)) : 0;
      assert.equal(captureRingChance(base,grade),expected);
      assert.ok(expected<=base);
    }
    assert.equal(captureRingChance(base,'green'),base);
  }
  for (const base of [-1,91,0.5,NaN,Infinity]) assert.throws(()=>captureRingChance(base,'green'),RangeError);
  for (const grade of ['',null,'Green','constructor','__proto__']) assert.throws(()=>captureRingChance(50,grade),RangeError);
});

test('green and orange include exact Q8 boundaries with adjacent milliseconds on the correct side', () => {
  for (let form=0;form<4;form++) {
    const center=(100-CAPTURE_RING.targetRadii[form])*30;
    for (const [offset,grade] of [[-721,'red'],[-720,'orange'],[-361,'orange'],[-360,'green'],[360,'green'],[361,'orange'],[720,'orange'],[721,'red']]) {
      const phase=center+offset;
      if(phase>=0&&phase<2400) assert.equal(sampleCaptureRing(phase,form).grade,grade,`form${form} phase${phase}`);
    }
  }
});

test('CLI exposes the additive capability and uses the same bounded sample/odds', () => {
  const core='./build/digivice-core';
  assert.deepEqual(JSON.parse(execFileSync(core,['--capture-ring-contract'],{encoding:'utf8'})),
    {inputVersion:1,action:'ring-capture',cycleMs:2400,factors:{red:10,orange:50,green:100}});
  for(let form=0;form<4;form++)for(const phase of [0,1,360,361,720,721,1080,1081,1440,1800,2160,2399])for(const base of [0,1,10,31,50,89,90]){
    const sample=sampleCaptureRing(phase,form);
    assert.deepEqual(JSON.parse(execFileSync(core,['--capture-ring-sample',String(phase),String(form),String(base)],{encoding:'utf8'})),
      {phaseMs:phase,radiusQ8:sample.radiusQ8,targetRadius:sample.targetRadius,grade:sample.grade,
        factorPercent:CAPTURE_RING.gradeFactors[sample.grade],chance:captureRingChance(base,sample.grade)});
  }
  for(const args of [['2400','1','50'],['-1','1','50'],['1','4294967296','50'],['1','1','91'],['0.5','1','50']])
    assert.throws(()=>execFileSync(core,['--capture-ring-sample',...args],{stdio:'pipe'}));
  assert.throws(()=>execFileSync(core,['--replay-onboarding','1'],{input:'ring-capture\n',stdio:'pipe'}),/explicit value/);
});

test('timestamps and form IDs are bounded; sparse frames do not change timing', () => {
  for (const invalid of [NaN, Infinity, -1, 0.5, Number.MAX_SAFE_INTEGER + 1])
    assert.throws(() => sampleCaptureRing(invalid, 1), RangeError);
  for (const invalid of [NaN, Infinity, -1, 0.5, 0x100000000])
    assert.throws(() => sampleCaptureRing(1, invalid), RangeError);
  assert.equal(sampleCaptureRing(Number.MAX_SAFE_INTEGER, 0xffffffff).targetRadius, 76);
  for (const step of [16, 83, 167, 400]) {
    for (let time = 0; time < 10000; time += step)
      assert.equal(sampleCaptureRing(time, 7).phaseMs, time % 2400);
  }
  assert.equal(sampleCaptureRing(2399, 1).radiusQ8, 5129);
  assert.equal(sampleCaptureRing(2400, 1).radiusQ8, 25600);
});
