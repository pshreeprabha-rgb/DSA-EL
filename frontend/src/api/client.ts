import type { ApiErrorBody, ServerErrorCode } from './types';

export type ApiErrorCode = ServerErrorCode | 'network' | 'timeout' | 'bad_json';

export class ApiError extends Error {
  readonly status: number;
  readonly code: ApiErrorCode;

  constructor(status: number, code: ApiErrorCode, message: string) {
    super(message);
    this.name = 'ApiError';
    this.status = status;
    this.code = code;
  }
}

export interface RequestOptions {
  timeoutMs?: number;
  signal?: AbortSignal;
  headers?: Record<string, string>;
}

const DEFAULT_TIMEOUT_MS = 5000;

function isApiErrorEnvelope(value: unknown): value is ApiErrorBody {
  if (typeof value !== 'object' || value === null) {
    return false;
  }
  const maybeError = (value as { error?: unknown }).error;
  if (typeof maybeError !== 'object' || maybeError === null) {
    return false;
  }
  const code = (maybeError as { code?: unknown }).code;
  const message = (maybeError as { message?: unknown }).message;
  return typeof code === 'string' && typeof message === 'string';
}

async function requestJson<T>(
  path: string,
  init: RequestInit,
  opts?: RequestOptions,
): Promise<T> {
  const timeoutMs = opts?.timeoutMs ?? DEFAULT_TIMEOUT_MS;
  const controller = new AbortController();
  let timedOut = false;

  const callerSignal = opts?.signal;
  const onCallerAbort = () => controller.abort();

  if (callerSignal) {
    if (callerSignal.aborted) {
      controller.abort();
    } else {
      callerSignal.addEventListener('abort', onCallerAbort, { once: true });
    }
  }

  const timer =
    timeoutMs > 0
      ? globalThis.setTimeout(() => {
          timedOut = true;
          controller.abort();
        }, timeoutMs)
      : null;

  let res: Response;
  try {
    res = await fetch(path, {
      ...init,
      signal: controller.signal,
    });
  } catch (err: unknown) {
    const isAbortError =
      timedOut ||
      (err instanceof Error && err.name === 'AbortError') ||
      (typeof DOMException !== 'undefined' &&
        err instanceof DOMException &&
        err.name === 'AbortError');
    if (isAbortError) {
      throw new ApiError(0, 'timeout', `Request timed out after ${timeoutMs} ms`);
    }
    const msg = err instanceof Error ? err.message : 'Network request failed';
    throw new ApiError(0, 'network', msg);
  } finally {
    if (timer !== null) {
      globalThis.clearTimeout(timer);
    }
    if (callerSignal) {
      callerSignal.removeEventListener('abort', onCallerAbort);
    }
  }

  let parsed: unknown;
  try {
    parsed = await res.json();
  } catch {
    if (!res.ok) {
      throw new ApiError(res.status, 'internal', res.statusText || `HTTP ${res.status}`);
    }
    throw new ApiError(res.status, 'bad_json', 'Response body is not valid JSON');
  }

  if (!res.ok) {
    if (isApiErrorEnvelope(parsed)) {
      throw new ApiError(
        res.status,
        parsed.error.code,
        parsed.error.message,
      );
    }
    throw new ApiError(res.status, 'internal', res.statusText || `HTTP ${res.status}`);
  }

  return parsed as T;
}

/** Sends a GET request expecting a JSON response, with a 5 s default timeout. */
export function getJson<T>(path: string, opts?: RequestOptions): Promise<T> {
  return requestJson<T>(
    path,
    {
      method: 'GET',
      headers: opts?.headers,
    },
    opts,
  );
}

/** Sends a POST request with a JSON body, expecting a JSON response, with a 5 s default timeout. */
export function postJson<T>(
  path: string,
  body: unknown,
  opts?: RequestOptions,
): Promise<T> {
  return requestJson<T>(
    path,
    {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
        ...opts?.headers,
      },
      body: JSON.stringify(body ?? {}),
    },
    opts,
  );
}

