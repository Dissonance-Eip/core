/**
 * Tests for the Node addon's JavaScript API (src/addon/addon.cpp).
 *
 * Loads build/Release/dissonance_core.node (built by `npm run build`), or the
 * addon at DISSONANCE_CORE_ADDON — the coverage build points this at its own
 * instrumented copy.
 */
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const addonPath = path.resolve(
  process.env.DISSONANCE_CORE_ADDON ||
    path.join(__dirname, '..', '..', 'build', 'Release', 'dissonance_core.node')
);
const addon = require(addonPath);

const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'dissonance-addon-'));
test.after(() => fs.rmSync(dir, { recursive: true, force: true }));

function chunk(id, payload) {
  const header = Buffer.alloc(8);
  header.write(id, 0, 'ascii');
  header.writeUInt32LE(payload.length, 4);
  const padding = Buffer.alloc(payload.length % 2);
  return Buffer.concat([header, payload, padding]);
}

function infoChunk(fields) {
  const subChunks = Object.entries(fields).map(([id, value]) =>
    chunk(id, Buffer.from(`${value}\0`, 'utf8'))
  );
  return chunk('LIST', Buffer.concat([Buffer.from('INFO', 'ascii'), ...subChunks]));
}

function wav({ samples = [0, 8000, -8000, 16000], channels = 1, sampleRate = 44100, extraChunks = [] } = {}) {
  const blockAlign = channels * 2;
  const fmt = Buffer.alloc(16);
  fmt.writeUInt16LE(1, 0);
  fmt.writeUInt16LE(channels, 2);
  fmt.writeUInt32LE(sampleRate, 4);
  fmt.writeUInt32LE(sampleRate * blockAlign, 8);
  fmt.writeUInt16LE(blockAlign, 12);
  fmt.writeUInt16LE(16, 14);

  const data = Buffer.alloc(samples.length * 2);
  samples.forEach((sample, i) => data.writeInt16LE(sample, i * 2));

  const body = Buffer.concat([chunk('fmt ', fmt), ...extraChunks, chunk('data', data)]);
  const header = Buffer.alloc(12);
  header.write('RIFF', 0, 'ascii');
  header.writeUInt32LE(4 + body.length, 4);
  header.write('WAVE', 8, 'ascii');
  return Buffer.concat([header, body]);
}

function sine(count, frequency = 440) {
  return Array.from({ length: count }, (_, i) =>
    Math.round(16000 * Math.sin((2 * Math.PI * frequency * i) / 44100))
  );
}

function write(name, contents) {
  const file = path.join(dir, name);
  fs.writeFileSync(file, contents);
  return file;
}

test('exports process, readMetadata and writeTags', () => {
  for (const name of ['process', 'readMetadata', 'writeTags']) {
    assert.equal(typeof addon[name], 'function', name);
  }
});

test.describe('process', () => {
  test('writes the processed file to the requested output path', async () => {
    const input = write('process-in.wav', wav({ samples: sine(4096) }));
    const output = path.join(dir, 'process-out.wav');

    const result = await addon.process(input, {
      outputPath: output,
      perturbation: 0.2,
      maskingStrength: 0.9,
      modes: ['white_noise', 42, 'phase_distortion'],
    });

    assert.deepEqual(result, { ok: true, processedPath: output });
    assert.ok(fs.statSync(output).size > 44);
  });

  test('ignores options of the wrong type and writes next to the input by default', async () => {
    const input = write('defaults.wav', wav());

    const result = await addon.process(input, {
      outputPath: 7,
      perturbation: 'high',
      maskingStrength: null,
      modes: 'white_noise',
    });

    assert.equal(result.processedPath, path.join(dir, 'defaults-processed.wav'));
    assert.ok(fs.existsSync(result.processedPath));
  });

  test('runs without options or with a non-object options value', async () => {
    const input = write('no-options.wav', wav());

    assert.equal((await addon.process(input)).ok, true);
    assert.equal((await addon.process(input, 5)).ok, true);
  });

  test('rejects a file it cannot open', async () => {
    const missing = path.join(dir, 'missing.wav');

    await assert.rejects(addon.process(missing), { message: `Failed to open file: ${missing}` });
  });

  test('throws when the input path is not a string', () => {
    const message = 'Input path must be a string';

    assert.throws(() => addon.process(), { name: 'TypeError', message });
    assert.throws(() => addon.process(42), { name: 'TypeError', message });
  });
});

test.describe('readMetadata', () => {
  test('reads the format, duration and tags', async () => {
    const input = write(
      'tagged.wav',
      wav({
        samples: new Array(44100).fill(0),
        extraChunks: [infoChunk({ INAM: 'Song', IART: 'Luca', ICRD: '20260916' })],
      })
    );

    const result = await addon.readMetadata(input);

    assert.deepEqual(result, {
      ok: true,
      audio: {
        audioFormat: 1,
        numChannels: 1,
        sampleRate: 44100,
        byteRate: 88200,
        blockAlign: 2,
        bitsPerSample: 16,
        durationSec: 1,
      },
      tags: {
        title: 'Song',
        artist: 'Luca',
        comment: '',
        date: '2026-09-16',
        genre: '',
        software: '',
        copyright: '',
      },
    });
  });

  test('reports zero duration when the header has no channels', async () => {
    const input = write('no-channels.wav', wav({ channels: 0 }));

    const { audio } = await addon.readMetadata(input);

    assert.equal(audio.durationSec, 0);
  });

  test('rejects a missing file and a file that is not a WAV', async () => {
    const missing = path.join(dir, 'missing.wav');
    const notWav = write('not-a-wav.wav', Buffer.from('hello world!'));

    await assert.rejects(addon.readMetadata(missing), {
      message: `Failed to open file: ${missing}`,
    });
    await assert.rejects(addon.readMetadata(notWav), { message: 'Not a valid RIFF/WAVE file' });
  });

  test('throws when the path is not a string', () => {
    assert.throws(() => addon.readMetadata(), {
      name: 'TypeError',
      message: 'Input path must be a string',
    });
  });
});

test.describe('writeTags', () => {
  test('writes tags that readMetadata reads back, ignoring non-string values', async () => {
    const input = write('write-tags.wav', wav());

    const result = await addon.writeTags(input, {
      title: 'New title',
      artist: 'Luca',
      comment: 42,
      date: '2026-09-16',
      genre: 'Ambient',
      software: 'Dissonance',
      copyright: '(c) 2026',
    });

    assert.deepEqual(result, { ok: true });
    assert.deepEqual((await addon.readMetadata(input)).tags, {
      title: 'New title',
      artist: 'Luca',
      comment: '',
      date: '2026-09-16',
      genre: 'Ambient',
      software: 'Dissonance',
      copyright: '(c) 2026',
    });
  });

  test('rejects a file it cannot open', async () => {
    const missing = path.join(dir, 'missing.wav');

    await assert.rejects(addon.writeTags(missing, { title: 'x' }), {
      message: `Cannot open file for tag write: ${missing}`,
    });
  });

  test('throws unless given a path and a tags object', () => {
    const expected = { name: 'TypeError', message: 'Expected (filePath: string, tags: object)' };

    assert.throws(() => addon.writeTags('/a.wav'), expected);
    assert.throws(() => addon.writeTags(42, {}), expected);
    assert.throws(() => addon.writeTags('/a.wav', 'title'), expected);
  });
});
