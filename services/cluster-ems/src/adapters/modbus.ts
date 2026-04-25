import type { GridInverterState, InverterSetpoint } from "@clusterstore/contracts";

export interface GridInverterPort {
  readState(): Promise<GridInverterState>;
  writeSetpoint(setpoint: InverterSetpoint): Promise<void>;
  prechargeDcBus(targetVoltageV: number): Promise<void>;
  holdOpenBus(): Promise<void>;
}
