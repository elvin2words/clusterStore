# Next Development Roadmap

This is the recommended development order after the current repo baseline.

## Priority 0: Site-Proving Work

These are the next items most likely to unlock a real pilot.

1. Hardware-in-the-loop validation for CAN, inverter, watchdog, and LTE paths
2. Real inverter register-map validation against the first supported production inverter
3. Real broker TLS validation with certificates, ACLs, and requester policies
4. STM32 board flash, boot, rollback, watchdog, and FDCAN validation on hardware

Done when:

- the deployment guide can be executed end to end on a real site without placeholder values

## Priority 1: Native-Firmware Convergence

The repo still carries a convergence gap between the new portable workspace and the older node runtime path.

Build next:

1. Collapse duplicated runtime logic so STM32 images consume the same core modules proven in the host tests
2. Add HIL tests for boot-control, journal recovery, and CAN supervision timeout behavior
3. Freeze the first production board-support contract for the STM32G474 node

Done when:

- the code proven in host tests is the same core used in the target images

## Priority 2: Overlay Node Commercial Path

The fastest deployment path is still overlay mode.

Build next:

1. Vendor-specific overlay adapters for Victron
2. Vendor-specific overlay adapters for Pylontech
3. Vendor-specific overlay adapters for Deye or Growatt, depending on the first customer target
4. Adapter qualification fixtures and replay tests

Done when:

- at least one supported third-party BESS can participate in the EMS with no per-site code changes

## Priority 3: Security and Provisioning

The system is locally operational, but not yet at a mature industrial security baseline.

Build next:

1. Secret injection and rotation workflow for MQTT credentials
2. CA and client-certificate provisioning workflow
3. Per-device identity and bootstrap enrollment
4. Signed OTA artifact flow for firmware and controller services
5. Auditable operator acknowledgement and command approval flow

Done when:

- deployment does not rely on static placeholder secrets or manual ad hoc credential placement

## Priority 4: Operations and Observability

Build next:

1. Site commissioning checklist automation
2. Controller metrics and service-health dashboarding
3. Journal export, bundle collection, and field-service diagnostics tooling
4. Brownout and black-start replay testing
5. Release packaging for controller deployments

Done when:

- field support can diagnose and recover a site without repo-level manual digging

## Priority 5: Fleet and Commercial Layer

Build next:

1. UtilityCore asset model and dashboard templates
2. Incident workflow and escalation rules
3. Tariff-aware dispatch policy engine
4. Degradation and cycle analytics
5. Service and maintenance records

Done when:

- multiple sites can be run as a managed fleet rather than one-off projects
