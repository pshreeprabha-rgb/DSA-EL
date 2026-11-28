export interface ConnectionBannerProps {
  connected: boolean;
  message?: string | null;
}

/** Shows a top banner when backend polling fails and clears automatically on recovery. */
export function ConnectionBanner({
  connected,
  message = 'Backend not reachable',
}: ConnectionBannerProps) {
  if (connected) {
    return null;
  }
  return (
    <div
      role="alert"
      className="connection-banner"
      data-testid="connection-banner"
    >
      {message || 'Backend not reachable'}
    </div>
  );
}

