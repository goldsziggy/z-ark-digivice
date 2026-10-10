// Migration checks compare every pre-rules12 field with independent golden
// output. New derived care/throw/offer fields are validated separately by the
// rules12 HTTP tests. Capacity projects back to 8 for historic fixtures;
// collection contents, IDs and all gameplay values remain exact.
const dropped = ['care', 'lastCapture', 'offerSeed', 'offers', 'pendingEncounter', 'foregroundSequence', 'receivedTrades', 'autoCapture', 'worldSeed', 'partyCapacity', 'partyMemberIds', 'careMinute', 'critical', 'captureDeferred', 'carePoints', 'toilet', 'careMissed', 'careMistakes', 'injury', 'maxCareMistakes', 'careRouteOpen', 'focus'];
export function legacyFields(value: any): any {
  if (Array.isArray(value)) return value.map(legacyFields);
  if (value === null || typeof value !== 'object') return value;
  return Object.fromEntries(Object.entries(value).filter(([key]) => !dropped.includes(key)).map(([key, child]) => [key, key === 'collectionCapacity' ? 8 : legacyFields(child)]));
}

// Rules 16 recomputes route gates and raises the level cap. Frozen receipts keep
// the wording and gates they were recorded with, so equality checks ignore that
// presentation and the durable care clocks added beside the old care object.
const optionGates = ['requiredLevel', 'requiredBond', 'requiredCare', 'previewLevel', 'combat', 'eligible'];
export function historicComparable(value: any): any {
  if (Array.isArray(value)) return value.map(historicComparable);
  if (value === null || typeof value !== 'object') return value;
  const out: Record<string, any> = {};
  for (const [key, child] of Object.entries(value)) {
    if (dropped.includes(key) || key === 'maxLevel') continue;
    if (key === 'evolution' && child && typeof child === 'object' && !Array.isArray(child)) {
      const evolution = historicComparable(child);
      if (Array.isArray(evolution.options)) evolution.options = evolution.options.map((option: any) => {
        const next = { ...option };
        for (const gate of optionGates) delete next[gate];
        return next;
      });
      out[key] = evolution;
      continue;
    }
    out[key] = historicComparable(child);
  }
  return out;
}
