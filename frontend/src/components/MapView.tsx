import { memo, useEffect, useMemo, useRef } from 'react';
import L from 'leaflet';
import {
  CircleMarker,
  MapContainer,
  Polyline,
  Popup,
  TileLayer,
  useMap,
} from 'react-leaflet';
import type { GeometryResponse, StopInfo } from '../api/types';
import { routeColor, stopTypeColor } from '../lib/format';
import { useAppState } from '../state/AppState';
import { BusLayer } from './BusLayer';

const BENGALURU_CENTER: [number, number] = [12.9716, 77.5946];

interface FitBoundsOnStopsProps {
  stops: StopInfo[];
}

function FitBoundsOnStops({ stops }: FitBoundsOnStopsProps) {
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

interface RouteGeometryLayerProps {
  geometry: GeometryResponse | null;
}

const RouteGeometryLayer = memo(
  function RouteGeometryLayer({ geometry }: RouteGeometryLayerProps) {
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
        {geometry.blocked.map((seg, idx) => (
          <Polyline
            key={`blocked-${idx}`}
            positions={seg}
            pathOptions={{
              color: '#dc2626',
              weight: 5,
              opacity: 0.95,
              dashArray: '6 4',
            }}
          />
        ))}
      </>
    );
  },
  (prev, next) =>
    prev.geometry?.version === next.geometry?.version &&
    (prev.geometry === null) === (next.geometry === null),
);

/** Interactive Leaflet map displaying OSM tiles, stops, routes, blocked edges, and live buses. */
export function MapView() {
  const { stops, geometry, state } = useAppState();

  const queueMap = useMemo(() => {
    const map = new Map<number, number>();
    if (state) {
      for (const q of state.queues) {
        map.set(q.stop, q.waiting);
      }
    }
    return map;
  }, [state]);

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
        <FitBoundsOnStops stops={stops} />
        <RouteGeometryLayer geometry={geometry} />
        {stops.map((stop) => {
          const waiting = queueMap.get(stop.idx) ?? 0;
          return (
            <CircleMarker
              key={stop.idx}
              center={[stop.lat, stop.lng]}
              radius={stop.type === 'hub' ? 7 : 5}
              pathOptions={{
                color: '#ffffff',
                weight: 1.5,
                fillColor: stopTypeColor(stop.type),
                fillOpacity: 0.92,
              }}
            >
              <Popup>
                <div className="popup-card">
                  <strong>{stop.name}</strong>
                  <div>Type: {stop.type}</div>
                  <div>
                    Route:{' '}
                    {stop.route >= 0 ? `R${stop.route + 1}` : 'Depot / Unassigned'}
                  </div>
                  <div>Queue length: {waiting}</div>
                  <div>Event flag: {stop.event_active ? 'Active' : 'None'}</div>
                </div>
              </Popup>
            </CircleMarker>
          );
        })}
        <BusLayer />
      </MapContainer>
    </div>
  );
}

