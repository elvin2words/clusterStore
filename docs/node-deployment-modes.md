# Node Deployment Modes

ClusterStore supports two node deployment modes that converge at the Cluster EMS software layer.

## Mode A: Native Node

```text
STM32G474RET6
  -> portable firmware core
  -> BSP / HAL
  -> cluster node controller semantics
```

This path is for PowerHive-native hardware where ClusterStore owns the embedded control stack.

Current locked decisions:

- MCU: `STM32G474RET6`
- Bus: `FDCAN1`, `500 kbps` classic CAN for MVP
- Watchdog: `IWDG`
- NVM: internal flash with reserved pages for BCB and journal
- Boot strategy: dual-slot OTA with CRC32 now and signature-ready headers

The portable native-node firmware scaffold now lives under `firmware/clusterstore-firmware/`.

## Mode B: Overlay Node

```text
Existing BESS asset
  -> Modbus or CAN-BMS adapter
  -> normalized ClusterStore node semantics
  -> Cluster EMS
```

This path is for faster commercial deployments where ClusterStore supervises existing storage assets such as:

- Victron
- Pylontech
- Growatt
- Deye

The overlay-node path avoids rebuilding the chemistry / low-level BMS layer for early deployments while preserving the same EMS behavior above the adapter boundary.

## Convergence Point

Both modes converge at the Cluster EMS layer:

- Native nodes expose ClusterStore semantics directly from the embedded firmware.
- Overlay nodes expose the same semantics through a `bms_adapter` layer.

That means dispatch, telemetry, alerting, cluster balancing policy, and cloud integration can stay unified even while hardware paths differ underneath.

The first EMS-side adapter now lives at [bms-adapter.ts](../services/cluster-ems/src/adapters/bms-adapter.ts). It maps overlay asset telemetry and dispatch requests into the existing `CanBusPort` contract so wrapped BESS assets can participate in the same EMS control flow as native nodes.
