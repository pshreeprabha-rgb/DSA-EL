import { useEffect, useRef } from 'react';

export interface UsePollingOptions {
  enabled?: boolean;
}

/**
 * Invokes callback every intervalMs while enabled and document is visible,
 * cleaning up cleanly on unmount. Async rejections are caught safely.
 */
export function usePolling(
  callback: () => Promise<void> | void,
  intervalMs: number,
  options?: UsePollingOptions,
): void {
  const enabled = options?.enabled ?? true;
  const callbackRef = useRef(callback);
  const inFlightRef = useRef(false);

  useEffect(() => {
    callbackRef.current = callback;
  }, [callback]);

  useEffect(() => {
    if (!enabled || intervalMs <= 0) {
      return;
    }

    let cancelled = false;
    const id = globalThis.setInterval(() => {
      if (cancelled) {
        return;
      }
      if (typeof document !== 'undefined' && document.hidden) {
        return;
      }
      if (inFlightRef.current) {
        return;
      }
      inFlightRef.current = true;
      Promise.resolve()
        .then(() => callbackRef.current())
        .catch(() => {
          // Caller handles and surfaces errors cleanly; prevent unhandled rejection.
        })
        .finally(() => {
          inFlightRef.current = false;
        });
    }, intervalMs);

    return () => {
      cancelled = true;
      globalThis.clearInterval(id);
    };
  }, [enabled, intervalMs]);
}

