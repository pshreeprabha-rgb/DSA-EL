import { useEffect, useRef } from 'react';
import L from 'leaflet';
import { useMap } from 'react-leaflet';
import type { BusState } from '../api/types';
import { busStateColor } from '../lib/format';
import { busPosition, simNowS } from '../lib/interpolate';
import { useMapState } from '../state/AppState';

const FRAME_MS = 100;

function createBusIcon(bus: BusState): L.DivIcon {
  const color = busStateColor(bus.state);
  return L.divIcon({
    className: 'bus-div-icon',
    iconSize: [24, 24],
    iconAnchor: [12, 12],
    html: `<span class="bus-marker-pill" style="background-color:${color}">${bus.id}</span>`,
  });
}

function busPopupHtml(bus: BusState): string {
  const routeText = bus.route >= 0 ? `R${bus.route + 1}` : 'Reserve';
  return [
    `<div class="popup-card">`,
    `<strong>Bus #${bus.id}</strong>`,
    `<div>State: ${bus.state}</div>`,
    `<div>Route: ${routeText}</div>`,
    `<div>Stop idx: ${bus.stop_idx}</div>`,
    `<div>Load: ${bus.load}</div>`,
    `</div>`,
  ].join('');
}

/**
 * Imperatively updates Leaflet bus markers every 100 ms using continuous
 * interpolation without subscribing to 1 Hz state updates or putting coordinates in React state.
 */
export function BusLayer() {
  const map = useMap();
  const { getLatestTelemetry, subscribeTelemetry } = useMapState();

  const markersRef = useRef<Map<number, L.Marker>>(new Map());
  const metaRef = useRef<Map<number, string>>(new Map());

  useEffect(() => {
    const syncPositions = () => {
      const { state, fetchedAtMs, geometry, stops, config } =
        getLatestTelemetry();

      if (!state || stops.length === 0) {
        return;
      }

      const endMin = config?.end_min ?? 1320;
      const nowS = simNowS(state, fetchedAtMs, Date.now(), endMin);
      const activeIds = new Set<number>();

      for (const bus of state.buses) {
        const pos = busPosition(bus, geometry, stops, nowS);
        if (!pos) {
          continue;
        }
        activeIds.add(bus.id);

        const latLng: L.LatLngExpression = [pos[0], pos[1]];
        const metaKey = `${bus.state}|${bus.route}|${bus.stop_idx}|${bus.load}`;
        let marker = markersRef.current.get(bus.id);

        if (!marker) {
          marker = L.marker(latLng, {
            icon: createBusIcon(bus),
            title: `Bus #${bus.id} (${bus.state})`,
            zIndexOffset: bus.state === 'running' ? 200 : 100,
          })
            .bindPopup(busPopupHtml(bus))
            .addTo(map);
          markersRef.current.set(bus.id, marker);
          metaRef.current.set(bus.id, metaKey);
        } else {
          marker.setLatLng(latLng);
          if (metaRef.current.get(bus.id) !== metaKey) {
            marker.setIcon(createBusIcon(bus));
            marker.setPopupContent(busPopupHtml(bus));
            metaRef.current.set(bus.id, metaKey);
          }
        }
      }

      for (const [id, marker] of markersRef.current.entries()) {
        if (!activeIds.has(id)) {
          marker.remove();
          markersRef.current.delete(id);
          metaRef.current.delete(id);
        }
      }
    };

    syncPositions();
    const unsub = subscribeTelemetry(syncPositions);
    const timer = globalThis.setInterval(syncPositions, FRAME_MS);

    return () => {
      unsub();
      globalThis.clearInterval(timer);
    };
  }, [map, getLatestTelemetry, subscribeTelemetry]);

  useEffect(() => {
    const markers = markersRef.current;
    const meta = metaRef.current;
    return () => {
      for (const marker of markers.values()) {
        marker.remove();
      }
      markers.clear();
      meta.clear();
    };
  }, []);

  return null;
}
