/** Color-blind safe Okabe-Ito palette for the 6 Bengaluru routes. */
export const ROUTE_PALETTE: readonly string[] = [
  '#0072B2', // blue
  '#D55E00', // vermilion
  '#009E73', // bluish green
  '#CC79A7', // reddish purple
  '#E69F00', // orange
  '#56B4E9', // sky blue
];

/** Formats minutes since midnight as HH:MM, flooring fractional minutes. */
export function clockHHMM(minutes: number): string {
  if (!Number.isFinite(minutes) || minutes < 0) {
    return '00:00';
  }
  const total = Math.floor(minutes);
  const hh = Math.floor(total / 60);
  const mm = total % 60;
  return `${String(hh).padStart(2, '0')}:${String(mm).padStart(2, '0')}`;
}

/** Formats a duration in seconds as "<x.x> min" (e.g. "12.5 min"). */
export function minutesLabel(seconds: number): string {
  if (!Number.isFinite(seconds) || seconds < 0) {
    return 'N/A';
  }
  return `${(seconds / 60).toFixed(1)} min`;
}

/** Formats a lower/upper confidence interval bound pair. */
export function ciLabel(lo: number, hi: number): string {
  if (!Number.isFinite(lo) || !Number.isFinite(hi)) {
    return 'N/A';
  }
  return `[${lo.toFixed(1)}, ${hi.toFixed(1)}]`;
}

/** Maps stop land-use type to a color-blind safe marker hex color. */
export function stopTypeColor(type: string): string {
  switch (type) {
    case 'hub':
      return '#D55E00';
    case 'office':
      return '#0072B2';
    case 'market':
      return '#E69F00';
    case 'residential':
      return '#009E73';
    default:
      return '#64748b';
  }
}

/** Maps a zero-based route id to a color-blind safe route hex color. */
export function routeColor(routeId: number): string {
  if (!Number.isInteger(routeId) || routeId < 0) {
    return '#64748b';
  }
  return ROUTE_PALETTE[routeId % ROUTE_PALETTE.length];
}

/** Maps a bus operational state to a distinct marker color. */
export function busStateColor(state: string): string {
  switch (state) {
    case 'running':
      return '#0072B2';
    case 'deadhead':
      return '#E69F00';
    case 'reserve':
      return '#64748b';
    case 'broken':
      return '#D55E00';
    default:
      return '#334155';
  }
}

