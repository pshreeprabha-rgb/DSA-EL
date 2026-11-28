import type { BusState, GeometryResponse, SimStateResponse, StopInfo } from '../api/types';

export type LatLng = readonly [number, number];

const EARTH_RADIUS_M = 6_371_000;

function toRadians(deg: number): number {
  return (deg * Math.PI) / 180;
}

/** Great-circle Haversine distance between two [lat, lng] coordinates in meters. */
export function haversineMeters(a: LatLng, b: LatLng): number {
  const lat1 = toRadians(a[0]);
  const lat2 = toRadians(b[0]);
  const dLat = lat2 - lat1;
  const dLng = toRadians(b[1] - a[1]);

  const sinLat = Math.sin(dLat / 2);
  const sinLng = Math.sin(dLng / 2);
  const h = sinLat * sinLat + Math.cos(lat1) * Math.cos(lat2) * sinLng * sinLng;
  const c = 2 * Math.atan2(Math.sqrt(h), Math.sqrt(Math.max(0, 1 - h)));
  return EARTH_RADIUS_M * c;
}

function clamp01(value: number): number {
  if (!Number.isFinite(value)) {
    return 0;
  }
  return Math.max(0, Math.min(1, value));
}

/** Returns the interpolated [lat, lng] at fraction (clamped 0..1) of total path length. */
export function pointAlong(points: readonly LatLng[], fraction: number): LatLng {
  if (points.length === 0) {
    return [0, 0];
  }
  const first: LatLng = [points[0][0], points[0][1]];
  if (points.length === 1) {
    return first;
  }

  const f = clamp01(fraction);
  const segLengths: number[] = [];
  let total = 0;
  for (let i = 0; i < points.length - 1; i += 1) {
    const len = haversineMeters(points[i], points[i + 1]);
    segLengths.push(len);
    total += len;
  }

  if (total <= 0 || f === 0) {
    return first;
  }
  if (f === 1) {
    const last = points[points.length - 1];
    return [last[0], last[1]];
  }

  const target = f * total;
  let accumulated = 0;
  for (let i = 0; i < segLengths.length; i += 1) {
    const segLen = segLengths[i];
    if (accumulated + segLen >= target && segLen > 0) {
      const localT = (target - accumulated) / segLen;
      const p0 = points[i];
      const p1 = points[i + 1];
      return [
        p0[0] + (p1[0] - p0[0]) * localT,
        p0[1] + (p1[1] - p0[1]) * localT,
      ];
    }
    accumulated += segLen;
  }

  const last = points[points.length - 1];
  return [last[0], last[1]];
}

/** Normalized progress in [0, 1] along a leg; returns 1 if arriveS <= departS. */
export function legProgress(departS: number, arriveS: number, nowS: number): number {
  if (arriveS <= departS) {
    return 1;
  }
  return clamp01((nowS - departS) / (arriveS - departS));
}

/** Computes current simulated time in seconds, clamping to the end of the sim window. */
export function simNowS(
  state: Pick<SimStateResponse, 'clock_min' | 'running' | 'finished' | 'speed'>,
  fetchedAtMs: number,
  nowMs: number,
  endMin = 1320,
): number {
  const baseS = state.clock_min * 60;
  const endS = endMin * 60;
  if (!state.running || state.finished) {
    return Math.min(endS, baseS);
  }
  const dtS = Math.max(0, nowMs - fetchedAtMs) / 1000;
  return Math.min(endS, baseS + dtS * state.speed);
}

function findStopCoords(stops: readonly StopInfo[], stopIdx: number): LatLng | null {
  if (stopIdx < 0) {
    return null;
  }
  const direct = stops[stopIdx];
  if (direct && direct.idx === stopIdx) {
    return [direct.lat, direct.lng];
  }
  const match = stops.find((s) => s.idx === stopIdx);
  return match ? [match.lat, match.lng] : null;
}

/** Computes the current [lat, lng] position of a bus at simulated time nowS. */
export function busPosition(
  bus: BusState,
  geometry: GeometryResponse | null,
  stops: readonly StopInfo[],
  nowS: number,
): LatLng | null {
  const motion = bus.motion;

  if (bus.state === 'deadhead') {
    const from = findStopCoords(stops, motion.from_stop);
    const to = findStopCoords(stops, motion.to_stop);
    if (motion.moving && from && to) {
      const progress = legProgress(motion.depart_s, motion.arrive_s, nowS);
      return pointAlong([from, to], progress);
    }
    return findStopCoords(stops, bus.stop_idx) ?? from ?? to;
  }

  if (motion.moving) {
    const progress = legProgress(motion.depart_s, motion.arrive_s, nowS);
    if (motion.leg_index >= 0 && geometry) {
      const leg = geometry.legs.find(
        (item) => item.route === motion.leg_route && item.leg === motion.leg_index,
      );
      if (leg && leg.path.length > 0) {
        return pointAlong(leg.path, progress);
      }
    }
    const from = findStopCoords(stops, motion.from_stop);
    const to = findStopCoords(stops, motion.to_stop);
    if (from && to) {
      return pointAlong([from, to], progress);
    }
  }

  return findStopCoords(stops, bus.stop_idx) ?? findStopCoords(stops, motion.from_stop);
}

