import { clockHHMM } from '../lib/format';

export interface ClockDisplayProps {
  clockMin: number;
  className?: string;
}

/** Displays simulated wall-clock time as HH:MM. */
export function ClockDisplay({ clockMin, className }: ClockDisplayProps) {
  return (
    <time
      className={className ? `clock-display ${className}` : 'clock-display'}
      data-testid="clock-display"
    >
      {clockHHMM(clockMin)}
    </time>
  );
}

