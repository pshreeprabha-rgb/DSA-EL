import type { ValidationResult } from '../src/api/validators';

export function validateEndpointResponse(
  path: string,
  method: string,
  data: unknown,
): ValidationResult;

export function runLiveContractCheck(baseUrl: string): Promise<boolean>;

