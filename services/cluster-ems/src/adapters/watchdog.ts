export interface WatchdogPort {
  kick(): Promise<void>;
  triggerFailSafe(reason: string): Promise<void>;
}

