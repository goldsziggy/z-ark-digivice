// Migration checks compare every pre-rules12 field with independent golden
// output. New derived care/throw/offer fields are validated separately by the
// rules12 HTTP tests; this projection does not change values or rule markers.
export function legacyFields(value: any): any {
  if (Array.isArray(value)) return value.map(legacyFields);
  if (value === null || typeof value !== 'object') return value;
  return Object.fromEntries(Object.entries(value).filter(([key]) => !['care', 'lastCapture', 'offerSeed', 'offers', 'pendingEncounter', 'foregroundSequence'].includes(key)).map(([key, child]) => [key, legacyFields(child)]));
}
