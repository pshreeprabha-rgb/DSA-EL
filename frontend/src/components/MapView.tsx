import { memo, useEffect, useRef } from 'react';
import L from 'leaflet';
import {
  MapContainer,
  Marker,
  Pane,
  Polyline,
  TileLayer,
  useMap,
} from 'react-leaflet';
import type { GeometryResponse, OptimizeResponse, StopInfo } from '../api/types';
import { routeColor, stopTypeColor } from '../lib/format';
import { segmentMidpoint } from '../lib/interpolate';
import { useMapState } from '../state/AppState';
import { BusLayer } from './BusLayer';

const BENGALURU_CENTER: [number, number] = [12.9716, 77.5946];

const blockedMarkerIcon = L.divIcon({
  className: 'blocked-marker-icon',
  iconSize: [112, 24],
  iconAnchor: [56, 12],
  html: '<div class="blocked-marker-badge"><span class="blocked-marker-dot" aria-hidden="true"></span><span>Road blocked</span></div>',
});

function FitBoundsOnStops() {
  const { stops } = useMapState();
  const map = useMap();
  const fittedRef = useRef(false);

  useEffect(() => {
    if (fittedRef.current || stops.length === 0) {
      return;
    }
    const bounds = L.latLngBounds(stops.map((stop) => [stop.lat, stop.lng]));
    if (bounds.isValid()) {
      map.fitBounds(bounds, { padding: [24, 24] });
      fittedRef.current = true;
    }
  }, [map, stops]);

  return null;
}

function PanController() {
  const { panTarget } = useMapState();
  const map = useMap();
  const lastSeqRef = useRef<number>(0);

  useEffect(() => {
    if (!panTarget || panTarget.seq === lastSeqRef.current) {
      return;
    }
    lastSeqRef.current = panTarget.seq;
    map.panTo([panTarget.lat, panTarget.lng], { animate: true });
  }, [map, panTarget]);

  return null;
}

interface RouteGeometryLayerInnerProps {
  geometry: GeometryResponse | null;
}

const RouteGeometryLayerInner = memo(
  function RouteGeometryLayerInner({ geometry }: RouteGeometryLayerInnerProps) {
    if (!geometry) {
      return null;
    }
    return (
      <>
        {geometry.legs.map((leg) => (
          <Polyline
            key={`leg-${leg.route}-${leg.leg}`}
            positions={leg.path}
            pathOptions={{
              color: routeColor(leg.route),
              weight: 3,
              opacity: 0.78,
            }}
          />
        ))}
        <Pane name="blocked-roads" style={{ zIndex: 450 }}>
          {geometry.blocked.map((seg, idx) => (
            <Polyline
              key={`blocked-line-${idx}`}
              positions={seg}
              pathOptions={{
                color: '#dc2626',
                weight: 6,
                opacity: 1,
                dashArray: '6 4',
              }}
            />
          ))}
        </Pane>
        <Pane name="blocked-markers" style={{ zIndex: 650 }}>
          {geometry.blocked.map((seg, idx) => (
            <Marker
              key={`blocked-marker-${idx}`}
              position={segmentMidpoint(seg)}
              icon={blockedMarkerIcon}
              zIndexOffset={1000}
            />
          ))}
        </Pane>
      </>
    );
  },
  (prev, next) =>
    prev.geometry?.version === next.geometry?.version &&
    prev.geometry?.blocked.length === next.geometry?.blocked.length &&
    (prev.geometry === null) === (next.geometry === null),
);

function RouteGeometryLayer() {
  const { geometry } = useMapState();
  return <RouteGeometryLayerInner geometry={geometry} />;
}

function OptimizedPathLayerInner({
  pathData,
}: {
  pathData: OptimizeResponse | null;
}) {
  if (!pathData) {
    return null;
  }
  const primaryPath = pathData.astar?.path ?? pathData.dijkstra?.path ?? null;
  const secondaryPath =
    pathData.astar && pathData.dijkstra ? pathData.dijkstra.path : null;

  return (
    <>
      {secondaryPath && secondaryPath.length > 0 && (
        <Polyline
          positions={secondaryPath}
          pathOptions={{
            color: '#6366f1',
            weight: 6,
            opacity: 0.5,
            dashArray: '8 6',
          }}
        />
      )}
      {primaryPath && primaryPath.length > 0 && (
        <Polyline
          positions={primaryPath}
          pathOptions={{
            color: '#16a34a',
            weight: 4,
            opacity: 0.95,
          }}
        />
      )}
    </>
  );
}

function OptimizedPathLayer() {
  const { optimizedPath } = useMapState();
  return <OptimizedPathLayerInner pathData={optimizedPath} />;
}

function stopPopupHtml(stop: StopInfo, waiting: number): string {
  const routeLabel = stop.route >= 0 ? `R${stop.route + 1}` : 'Depot / Unassigned';
  const eventLabel = stop.event_active ? 'Active' : 'None';
  return [
    `<div class="popup-card">`,
    `<strong>${stop.name}</strong>`,
    `<div>Type: ${stop.type}</div>`,
    `<div>Route: ${routeLabel}</div>`,
    `<div>Queue length: ${waiting}</div>`,
    `<div>Event flag: ${eventLabel}</div>`,
    `</div>`,
  ].join('');
}

function StopMarkersLayer() {
  const map = useMap();
  const { stops, selectedStopIdx, getLatestTelemetry, subscribeTelemetry } =
    useMapState();
  const markersRef = useRef<Map<number, L.CircleMarker>>(new Map());

  useEffect(() => {
    const markers = markersRef.current;
    const { state } = getLatestTelemetry();
    const qMap = new Map<number, number>();
    if (state) {
      for (const q of state.queues) {
        qMap.set(q.stop, q.waiting);
      }
    }

    const active = new Set<number>();
    for (const stop of stops) {
      active.add(stop.idx);
      const isSelected = selectedStopIdx === stop.idx;
      const style: L.CircleMarkerOptions = {
        radius: isSelected ? 9 : stop.type === 'hub' ? 7 : 5,
        color: isSelected ? '#0f172a' : '#ffffff',
        weight: isSelected ? 2.5 : 1.5,
        fillColor: stopTypeColor(stop.type),
        fillOpacity: 0.92,
      };
      const waiting = qMap.get(stop.idx) ?? 0;

      let cm = markers.get(stop.idx);
      if (!cm) {
        cm = L.circleMarker([stop.lat, stop.lng], style)
          .bindPopup(stopPopupHtml(stop, waiting))
          .addTo(map);
        markers.set(stop.idx, cm);
      } else {
        cm.setLatLng([stop.lat, stop.lng]);
        cm.setStyle(style);
        cm.setPopupContent(stopPopupHtml(stop, waiting));
      }
    }

    for (const [idx, cm] of markers.entries()) {
      if (!active.has(idx)) {
        cm.remove();
        markers.delete(idx);
      }
    }
  }, [map, stops, selectedStopIdx, getLatestTelemetry]);

  useEffect(() => {
    const syncQueues = () => {
      const { state: latestState, stops: latestStops } = getLatestTelemetry();
      if (!latestState) return;
      const qMap = new Map<number, number>();
      for (const q of latestState.queues) {
        qMap.set(q.stop, q.waiting);
      }
      for (const stop of latestStops) {
        const cm = markersRef.current.get(stop.idx);
        if (cm) {
          cm.setPopupContent(stopPopupHtml(stop, qMap.get(stop.idx) ?? 0));
        }
      }
    };

    const unsub = subscribeTelemetry(syncQueues);
    return unsub;
  }, [getLatestTelemetry, subscribeTelemetry]);

  useEffect(() => {
    const markers = markersRef.current;
    return () => {
      for (const cm of markers.values()) {
        cm.remove();
      }
      markers.clear();
    };
  }, []);

  return null;
}

/**
 * Interactive Leaflet map wrapped in React.memo and decoupled from 1 Hz state
 * updates so polling does not trigger React commits in the map tree.
 */
export const MapView = memo(function MapView() {
  return (
    <div className="map-wrapper" data-testid="map-view">
      <MapContainer
        center={BENGALURU_CENTER}
        zoom={12}
        className="leaflet-container-root"
      >
        <TileLayer
          url="https://tile.openstreetmap.org/{z}/{x}/{y}.png"
          attribution="(c) OpenStreetMap contributors"
        />
        <FitBoundsOnStops />
        <PanController />
        <RouteGeometryLayer />
        <OptimizedPathLayer />
        <StopMarkersLayer />
        <BusLayer />
      </MapContainer>
    </div>
  );
});
