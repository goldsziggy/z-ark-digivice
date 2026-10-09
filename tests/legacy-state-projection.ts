// Migration checks compare every pre-rules12 field with independent golden
// output. New derived care/throw/offer fields are validated separately by the
// rules12 HTTP tests. Capacity projects back to8 for historic fixtures;
// collection contents, IDs and all gameplay values remain exact.
export function legacyFields(value: any): any {
  if (Array.isArray(value)) return value.map(legacyFields);
  if (value === null || typeof value !== 'object') return value;
  return Object.fromEntries(Object.entries(value).filter(([key]) => !['care', 'lastCapture', 'offerSeed', 'offers', 'pendingEncounter', 'foregroundSequence', 'receivedTrades', 'autoCapture', 'worldSeed', 'partyCapacity', 'partyMemberIds'].includes(key)).map(([key, child]) => [key, key === 'collectionCapacity' ? 8 : legacyFields(child)]));
}
