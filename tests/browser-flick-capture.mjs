// Compatibility entry point: current capture input is the timing ring.
// Legacy Flick wire/trajectory behavior remains covered by capture-trajectory
// and core tests; live UI coverage is shared with manual Auto capture.
await import('./browser-ring-capture.mjs');
