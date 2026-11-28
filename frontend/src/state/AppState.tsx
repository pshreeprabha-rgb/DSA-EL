import type { ReactNode } from 'react';
import {
  createContext,
  useCallback,
  useContext,
  useEffect,
  useMemo,
  useRef,
  useState,
} from 'react';
import { ApiError, getJson } from '../api/client';
import type {
  ConfigResponse,
  GeometryResponse,
  SimStateResponse,
  StopInfo,
  StopsResponse,
} from '../api/types';
import { usePolling } from '../hooks/usePolling';

export type ConnectionStatus = 'connected' | 'disconnected';

export interface AppContextValue {
  state: SimStateResponse | null;
  fetchedAtMs: number;
  config: ConfigResponse | null;
  stops: StopInfo[];
  geometry: GeometryResponse | null;
  connectionStatus: ConnectionStatus;
  connectionMessage: string | null;
  lastError: ApiError | null;
  pollStateNow: () => Promise<void>;
}

const AppStateContext = createContext<AppContextValue | null>(null);

const STATE_POLL_MS = 1000;
const STATE_TIMEOUT_MS = 4500;

export interface AppStateProviderProps {
  children: ReactNode;
  pollIntervalMs?: number;
}

/** Provides shared simulation state, static metadata, geometry, and backend connection status. */
export function AppStateProvider({
  children,
  pollIntervalMs = STATE_POLL_MS,
}: AppStateProviderProps) {
  const [state, setState] = useState<SimStateResponse | null>(null);
  const [fetchedAtMs, setFetchedAtMs] = useState<number>(() => Date.now());
  const [config, setConfig] = useState<ConfigResponse | null>(null);
  const [stops, setStops] = useState<StopInfo[]>([]);
  const [geometry, setGeometry] = useState<GeometryResponse | null>(null);
  const [connectionStatus, setConnectionStatus] =
    useState<ConnectionStatus>('connected');
  const [connectionMessage, setConnectionMessage] = useState<string | null>(
    null,
  );
  const [lastError, setLastError] = useState<ApiError | null>(null);

  const mountedRef = useRef(true);
  const loadedGeometryVersionRef = useRef<number | null>(null);
  const loadingGeometryVersionRef = useRef<number | null>(null);
  const configLoadedRef = useRef(false);
  const stopsLoadedRef = useRef(false);

  useEffect(() => {
    mountedRef.current = true;
    return () => {
      mountedRef.current = false;
    };
  }, []);

  const fetchStaticOnce = useCallback(async () => {
    const tasks: Promise<void>[] = [];
    if (!configLoadedRef.current) {
      tasks.push(
        getJson<ConfigResponse>('/api/config')
          .then((cfg) => {
            if (!mountedRef.current) return;
            configLoadedRef.current = true;
            setConfig(cfg);
          })
          .catch(() => {
            // Leave unloaded so next cycle or state recovery can retry.
          }),
      );
    }
    if (!stopsLoadedRef.current) {
      tasks.push(
        getJson<StopsResponse>('/api/stops')
          .then((res) => {
            if (!mountedRef.current) return;
            stopsLoadedRef.current = true;
            setStops(res.stops);
          })
          .catch(() => {
            // Leave unloaded cleanly without unhandled rejection.
          }),
      );
    }
    await Promise.all(tasks);
  }, []);

  const fetchGeometryForVersion = useCallback(async (version: number) => {
    if (
      loadedGeometryVersionRef.current === version ||
      loadingGeometryVersionRef.current === version
    ) {
      return;
    }
    loadingGeometryVersionRef.current = version;
    try {
      const geo = await getJson<GeometryResponse>('/api/geometry');
      if (!mountedRef.current) return;
      loadedGeometryVersionRef.current = geo.version;
      setGeometry(geo);
    } catch {
      // Allow retry if version remains unmet.
    } finally {
      if (loadingGeometryVersionRef.current === version) {
        loadingGeometryVersionRef.current = null;
      }
    }
  }, []);

  const pollStateNow = useCallback(async () => {
    try {
      const next = await getJson<SimStateResponse>('/api/state', {
        timeoutMs: STATE_TIMEOUT_MS,
      });
      if (!mountedRef.current) return;
      const now = Date.now();
      setState(next);
      setFetchedAtMs(now);
      setConnectionStatus('connected');
      setConnectionMessage(null);
      setLastError(null);

      if (!configLoadedRef.current || !stopsLoadedRef.current) {
        void fetchStaticOnce();
      }
      if (loadedGeometryVersionRef.current !== next.geometry_version) {
        void fetchGeometryForVersion(next.geometry_version);
      }
    } catch (err: unknown) {
      if (!mountedRef.current) return;
      const apiErr =
        err instanceof ApiError
          ? err
          : new ApiError(0, 'network', 'Backend not reachable');
      setLastError(apiErr);
      setConnectionStatus('disconnected');
      setConnectionMessage('Backend not reachable');
    }
  }, [fetchGeometryForVersion, fetchStaticOnce]);

  useEffect(() => {
    let cancelled = false;
    Promise.resolve().then(() => {
      if (cancelled) return;
      void fetchStaticOnce();
      void pollStateNow();
    });
    return () => {
      cancelled = true;
    };
  }, [fetchStaticOnce, pollStateNow]);

  usePolling(pollStateNow, pollIntervalMs);

  const value = useMemo<AppContextValue>(
    () => ({
      state,
      fetchedAtMs,
      config,
      stops,
      geometry,
      connectionStatus,
      connectionMessage,
      lastError,
      pollStateNow,
    }),
    [
      state,
      fetchedAtMs,
      config,
      stops,
      geometry,
      connectionStatus,
      connectionMessage,
      lastError,
      pollStateNow,
    ],
  );

  return (
    <AppStateContext.Provider value={value}>
      {children}
    </AppStateContext.Provider>
  );
}

/** Accessor hook for the shared simulation state context. */
export function useAppState(): AppContextValue {
  const ctx = useContext(AppStateContext);
  if (!ctx) {
    throw new Error('useAppState must be used within AppStateProvider');
  }
  return ctx;
}
