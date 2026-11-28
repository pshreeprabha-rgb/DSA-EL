import { describe, it, expect, vi, afterEach } from 'vitest';
import { ApiError, getJson, postJson } from './client';

describe('api client', () => {
  afterEach(() => {
    vi.restoreAllMocks();
    vi.useRealTimers();
  });

  it('parses JSON response on HTTP 200', async () => {
    const payload = { ok: true, sim_running: false };
    vi.stubGlobal(
      'fetch',
      vi.fn().mockResolvedValue(
        new Response(JSON.stringify(payload), {
          status: 200,
          headers: { 'Content-Type': 'application/json' },
        }),
      ),
    );

    const data = await getJson<typeof payload>('/api/health');
    expect(data).toEqual(payload);
  });

  it('throws ApiError with server code on HTTP 400 envelope', async () => {
    const errBody = {
      error: { code: 'invalid_argument', message: 'bad stop index' },
    };
    vi.stubGlobal(
      'fetch',
      vi.fn().mockResolvedValue(
        new Response(JSON.stringify(errBody), {
          status: 400,
          headers: { 'Content-Type': 'application/json' },
        }),
      ),
    );

    let caught: unknown;
    try {
      await getJson('/api/demand/forecast?stop=-1');
    } catch (err) {
      caught = err;
    }
    expect(caught).toBeInstanceOf(ApiError);
    expect(caught).toMatchObject({
      status: 400,
      code: 'invalid_argument',
      message: 'bad stop index',
    });
  });

  it('maps rejected fetch to ApiError code network', async () => {
    vi.stubGlobal(
      'fetch',
      vi.fn().mockRejectedValue(new TypeError('Failed to fetch')),
    );

    let caught: unknown;
    try {
      await getJson('/api/state');
    } catch (err) {
      caught = err;
    }
    expect(caught).toBeInstanceOf(ApiError);
    expect(caught).toMatchObject({
      status: 0,
      code: 'network',
    });
  });

  it('maps aborted request to ApiError code timeout', async () => {
    vi.useFakeTimers();
    vi.stubGlobal(
      'fetch',
      vi.fn().mockImplementation((_url: string, init?: RequestInit) => {
        return new Promise((_resolve, reject) => {
          init?.signal?.addEventListener('abort', () => {
            reject(new DOMException('The operation was aborted', 'AbortError'));
          });
        });
      }),
    );

    const promise = getJson('/api/state', { timeoutMs: 5000 });
    const expectRejection = promise.catch((err: unknown) => err);
    await vi.advanceTimersByTimeAsync(5000);
    const caught = await expectRejection;

    expect(caught).toBeInstanceOf(ApiError);
    expect(caught).toMatchObject({
      status: 0,
      code: 'timeout',
    });
  });

  it('maps invalid JSON on HTTP 200 to ApiError code bad_json', async () => {
    vi.stubGlobal(
      'fetch',
      vi.fn().mockResolvedValue(new Response('not-json{', { status: 200 })),
    );

    let caught: unknown;
    try {
      await getJson('/api/config');
    } catch (err) {
      caught = err;
    }
    expect(caught).toBeInstanceOf(ApiError);
    expect(caught).toMatchObject({
      status: 200,
      code: 'bad_json',
    });
  });

  it('sends JSON body and Content-Type header in postJson', async () => {
    const fetchMock = vi.fn().mockResolvedValue(
      new Response(JSON.stringify({ ok: true, clock_min: 360, running: true, speed: 120 }), {
        status: 200,
        headers: { 'Content-Type': 'application/json' },
      }),
    );
    vi.stubGlobal('fetch', fetchMock);

    const res = await postJson<{ ok: boolean; clock_min: number; running: boolean; speed: number }>(
      '/api/sim/control',
      { action: 'start', speed: 120 },
    );

    expect(res.running).toBe(true);
    expect(fetchMock).toHaveBeenCalledTimes(1);
    const [url, init] = fetchMock.mock.calls[0] as [string, RequestInit];
    expect(url).toBe('/api/sim/control');
    expect(init.method).toBe('POST');
    expect(init.headers).toMatchObject({ 'Content-Type': 'application/json' });
    expect(init.body).toBe(JSON.stringify({ action: 'start', speed: 120 }));
  });
});

