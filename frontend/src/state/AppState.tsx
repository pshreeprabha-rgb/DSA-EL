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
  MetricsResponse,
  OptimizeResponse,
  RoutesResponse,
  SimStateResponse,
  StopInfo,
  StopsResponse,
} from '../api/types';
import { usePolling } from '../hooks/usePolling';
import { blockedSegmentKey, segmentMidpoint } from '../lib/interpolate';

export type ConnectionStatus = 'connected' | 'disconnected';

export interface PanTarget {
  lat: number;
  lng: number;
  seq: number;
}

export interface TelemetrySnapshot {
  state: SimStateResponse | null;
  fetchedAtMs: number;
  config: ConfigResponse | null;
  geometry: GeometryResponse | null;
  stops: StopInfo[];
}

export interface MapContextValue {
  stops: StopInfo[];
  geometry: GeometryResponse | null;
  panTarget: PanTarget | null;
  selectedStopIdx: number | null;
  optimizedPath: OptimizeResponse | null;
  getLatestTelemetry: () => TelemetrySnapshot;
  subscribeTelemetry: (listener: () => void) => () => void;
}

export interface AppContextValue {
  state: SimStateResponse | null;
  fetchedAtMs: number;
  config: ConfigResponse | null;
  stops: StopInfo[];
  geometry: GeometryResponse | null;
  routes: RoutesResponse | null;
  routesError: string | null;
  metrics: MetricsResponse | null;
  metricsError: string | null;
  connectionStatus: ConnectionStatus;
  connectionMessage: string | null;
  lastError: ApiError | null;
  selectedStopIdx: number | null;
  setSelectedStopIdx: (stopIdx: number | null) => void;
  panTarget: PanTarget | null;
  focusStop: (stopIdx: number) => void;
  expectNewBlockPan: () => void;
  optimizedPath: OptimizeResponse | null;
  setOptimizedPath: (res: OptimizeResponse | null) => void;
  pollStateNow: () => Promise<void>;
  refreshDashboardNow: () => Promise<void>;
}

const AppStateContext = createContext<AppContextValue | null>(null);
const MapStateContext = createContext<MapContextValue | null>(null);

const STATE_POLL_MS = 1000;
const DASHBOARD_POLL_MS = 5000;
const STATE_TIMEOUT_MS = 4500;

export interface AppStateProviderProps {
  children: ReactNode;
  pollIntervalMs?: number;
}

/** Provides shared simulation state, static metadata, geometry, and decoupled map telemetry. */
export function AppStateProvider({
  children,
  pollIntervalMs = STATE_POLL_MS,
}: AppStateProviderProps) {
  const [state, setState] = useState<SimStateResponse | null>(null);
  const [fetchedAtMs, setFetchedAtMs] = useState<number>(() => Date.now());
  const [config, setConfig] = useState<ConfigResponse | null>(null);
  const [stops, setStops] = useState<StopInfo[]>([]);
  const [geometry, setGeometry] = useState<GeometryResponse | null>(null);
  const [routes, setRoutes] = useState<RoutesResponse | null>(null);
  const [routesError, setRoutesError] = useState<string | null>(null);
  const [metrics, setMetrics] = useState<MetricsResponse | null>(null);
  const [metricsError, setMetricsError] = useState<string | null>(null);
  const [connectionStatus, setConnectionStatus] =
    useState<ConnectionStatus>('connected');
  const [connectionMessage, setConnectionMessage] = useState<string | null>(
    null,
  );
  const [lastError, setLastError] = useState<ApiError | null>(null);
  const [selectedStopIdx, setSelectedStopIdx] = useState<number | null>(null);
  const [panTarget, setPanTarget] = useState<PanTarget | null>(null);
  const [optimizedPath, setOptimizedPath] = useState<OptimizeResponse | null>(
    null,
  );

  const mountedRef = useRef(true);
  const loadedGeometryVersionRef = useRef<number | null>(null);
  const loadingGeometryVersionRef = useRef<number | null>(null);
  const configLoadedRef = useRef(false);
  const stopsLoadedRef = useRef(false);
  const stopsRef = useRef<StopInfo[]>(stops);
  const geometryRef = useRef<GeometryResponse | null>(geometry);
  const panSeqRef = useRef(0);
  const awaitingBlockPanRef = useRef(false);
  const knownBlockedKeysRef = useRef<Set<string>>(new Set());

  const telemetryRef = useRef<TelemetrySnapshot>({
    state: null,
    fetchedAtMs: 0,
    config: null,
    geometry: null,
    stops: [],
  });
  const telemetryListenersRef = useRef<Set<() => void>>(new Set());

  const notifyTelemetry = useCallback(() => {
    for (const listener of telemetryListenersRef.current) {
      listener();
    }
  }, []);

  useEffect(() => {
    stopsRef.current = stops;
    telemetryRef.current = { ...telemetryRef.current, stops };
    notifyTelemetry();
  }, [stops, notifyTelemetry]);

  useEffect(() => {
    geometryRef.current = geometry;
    telemetryRef.current = { ...telemetryRef.current, geometry };
    notifyTelemetry();
  }, [geometry, notifyTelemetry]);

  useEffect(() => {
    telemetryRef.current = { ...telemetryRef.current, config };
    notifyTelemetry();
  }, [config, notifyTelemetry]);

  useEffect(() => {
    mountedRef.current = true;
    return () => {
      mountedRef.current = false;
    };
  }, []);

  const getLatestTelemetry = useCallback(
    () => telemetryRef.current,
    [],
  );

  const subscribeTelemetry = useCallback((listener: () => void) => {
    telemetryListenersRef.current.add(listener);
    return () => {
      telemetryListenersRef.current.delete(listener);
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

      if (awaitingBlockPanRef.current && geo.blocked.length > 0) {
        const nextBlocked =
          geo.blocked.find(
            (seg) => !knownBlockedKeysRef.current.has(blockedSegmentKey(seg)),
          ) ?? null;
        if (nextBlocked) {
          const mid = segmentMidpoint(nextBlocked);
          panSeqRef.current += 1;
          setPanTarget({ lat: mid[0], lng: mid[1], seq: panSeqRef.current });
          awaitingBlockPanRef.current = false;
          knownBlockedKeysRef.current = new Set(
            geo.blocked.map((seg) => blockedSegmentKey(seg)),
          );
        }
      }
    } catch {
      // Allow retry if version remains unmet.
    } finally {
      if (loadingGeometryVersionRef.current === version) {
        loadingGeometryVersionRef.current = null;
      }
    }
  }, []);

  const refreshDashboardNow = useCallback(async () => {
    const rTask = getJson<RoutesResponse>('/api/routes')
      .then((res) => {
        if (!mountedRef.current) return;
        setRoutes(res);
        setRoutesError(null);
      })
      .catch((err: unknown) => {
        if (!mountedRef.current) return;
        setRoutesError(
          err instanceof Error ? err.message : 'Failed to load routes',
        );
      });

    const mTask = getJson<MetricsResponse>('/api/metrics')
      .then((res) => {
        if (!mountedRef.current) return;
        setMetrics(res);
        setMetricsError(null);
      })
      .catch((err: unknown) => {
        if (!mountedRef.current) return;
        setMetricsError(
          err instanceof Error ? err.message : 'Failed to load metrics',
        );
      });

    await Promise.all([rTask, mTask]);
  }, []);

  const pollStateNow = useCallback(async () => {
    try {
      const next = await getJson<SimStateResponse>('/api/state', {
        timeoutMs: STATE_TIMEOUT_MS,
      });
      if (!mountedRef.current) return;
      const now = Date.now();
      telemetryRef.current = {
        ...telemetryRef.current,
        state: next,
        fetchedAtMs: now,
      };
      notifyTelemetry();

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
  }, [fetchGeometryForVersion, fetchStaticOnce, notifyTelemetry]);

  const focusStop = useCallback((stopIdx: number) => {
    setSelectedStopIdx(stopIdx);
    const match = stopsRef.current.find((s) => s.idx === stopIdx);
    if (match) {
      panSeqRef.current += 1;
      setPanTarget({ lat: match.lat, lng: match.lng, seq: panSeqRef.current });
    }
  }, []);

  const expectNewBlockPan = useCallback(() => {
    const currentBlocked = geometryRef.current?.blocked ?? [];
    knownBlockedKeysRef.current = new Set(
      currentBlocked.map((seg) => blockedSegmentKey(seg)),
    );
    awaitingBlockPanRef.current = true;
  }, []);

  useEffect(() => {
    let cancelled = false;
    Promise.resolve().then(() => {
      if (cancelled) return;
      void fetchStaticOnce();
      void pollStateNow();
      void refreshDashboardNow();
    });
    return () => {
      cancelled = true;
    };
  }, [fetchStaticOnce, pollStateNow, refreshDashboardNow]);

  usePolling(pollStateNow, pollIntervalMs);
  usePolling(refreshDashboardNow, DASHBOARD_POLL_MS);

  const mapValue = useMemo<MapContextValue>(
    () => ({
      stops,
      geometry,
      panTarget,
      selectedStopIdx,
      optimizedPath,
      getLatestTelemetry,
      subscribeTelemetry,
    }),
    [
      stops,
      geometry,
      panTarget,
      selectedStopIdx,
      optimizedPath,
      getLatestTelemetry,
      subscribeTelemetry,
    ],
  );

  const value = useMemo<AppContextValue>(
    () => ({
      state,
      fetchedAtMs,
      config,
      stops,
      geometry,
      routes,
      routesError,
      metrics,
      metricsError,
      connectionStatus,
      connectionMessage,
      lastError,
      selectedStopIdx,
      setSelectedStopIdx,
      panTarget,
      focusStop,
      expectNewBlockPan,
      optimizedPath,
      setOptimizedPath,
      pollStateNow,
      refreshDashboardNow,
    }),
    [
      state,
      fetchedAtMs,
      config,
      stops,
      geometry,
      routes,
      routesError,
      metrics,
      metricsError,
      connectionStatus,
      connectionMessage,
      lastError,
      selectedStopIdx,
      panTarget,
      focusStop,
      expectNewBlockPan,
      optimizedPath,
      pollStateNow,
      refreshDashboardNow,
    ],
  );

  return (
    <MapStateContext.Provider value={mapValue}>
      <AppStateContext.Provider value={value}>
        {children}
      </AppStateContext.Provider>
    </MapStateContext.Provider>
  );
}

/** Accessor hook for shared simulation state context. */
export function useAppState(): AppContextValue {
  const ctx = useContext(AppStateContext);
  if (!ctx) {
    throw new Error('useAppState must be used within AppStateProvider');
  }
  return ctx;
}

/** Accessor hook for decoupled map state that avoids re-rendering on 1 Hz telemetry ticks. */
export function useMapState(): MapContextValue {
  const ctx = useContext(MapStateContext);
  if (!ctx) {
    throw new Error('useMapState must be used within AppStateProvider');
  }
  return ctx;
}
