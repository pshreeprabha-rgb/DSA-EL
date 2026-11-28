import type { TrafficSource } from '../api/types';

export interface BadgeProps {
  trafficSource?: TrafficSource | string;
}

/** Displays whether traffic telemetry is Live or Simulated. */
export function Badge({ trafficSource }: BadgeProps) {
  const isLive = trafficSource === 'live';
  return (
    <span
      className={isLive ? 'badge badge--live' : 'badge badge--sim'}
      data-testid="traffic-badge"
    >
      {isLive ? 'Live' : 'Simulated'}
    </span>
  );
}

