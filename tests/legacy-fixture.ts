import { createHash } from 'node:crypto';

/** Historical format-3 identities for regression tests, never production credentials. */
export function legacyFixture(count = 1) {
  const identities = Array.from({ length: count }, (_, index) => ({
    deviceId: `dv_${(index + 1).toString(16).padStart(24, '0')}`,
    token: Buffer.alloc(32, index + 1).toString('base64url'),
  }));
  const store = { formatVersion: 3, gameSchemaVersion: 4, rulesVersion: 3,
    devices: identities.map(identity => ({ deviceId: identity.deviceId,
      tokenHash: createHash('sha256').update(identity.token).digest('hex'),
      seed: 12345, revision: 0, legacy: null, events: [], receipts: [],
    })),
  };
  return { identities, store };
}

/** Named starter baseline for tests of new production actions. Old Mote fixtures
 * above remain unchanged for historical decode and migration coverage. */
export function namedFixture(count = 1) {
  const { identities } = legacyFixture(count);
  const events = [{ type: 'hatch', value: 1 }];
  const bodyHash = createHash('sha256').update(JSON.stringify({ rulesVersion: 3, baseRevision: 0, events })).digest('hex');
  return { identities, store: { formatVersion: 4, gameSchemaVersion: 5, rulesVersion: 3,
    devices: identities.map(identity => ({ deviceId: identity.deviceId,
      tokenHash: createHash('sha256').update(identity.token).digest('hex'), seed: 12345, initialMode: 'onboarding', revision: 1, legacy: null, events,
      receipts: [{ batchId: 'named-starter-test-fixture', revision: 1, eventEnd: 1, bodyHash }],
    })),
  } };
}
