// Historical replay executors keep their original strings. Translate only
// these known game messages when presenting them, including accessibility text.
export function displayGameMessage(value) {
  if (typeof value !== 'string' || !value) return 'A new little adventure awaits.';
  if (value === 'A new friend joined your collection!') return 'A new Digimon joined your collection!';
  if (value === 'Your friend is free to roam; your journal remembers them.') return 'Your Digimon is free to roam; your journal remembers them.';
  return value.replaceAll('_', ' ');
}
