import type {
  ClusterAlert,
  ClusterTelemetry,
  CommandAcknowledgement,
  RemoteCommand
} from "./types.ts";

export const MQTT_SCHEMA_VERSION = "1.0.0";

export interface MqttEnvelope<TPayload> {
  schemaVersion: string;
  sentAt: string;
  payload: TPayload;
}

export function telemetryTopic(siteId: string, clusterId: string): string {
  return `cluster/${siteId}/${clusterId}/telemetry`;
}

export function alertsTopic(siteId: string, clusterId: string): string {
  return `cluster/${siteId}/${clusterId}/alerts`;
}

export function commandsTopic(siteId: string, clusterId: string): string {
  return `cluster/${siteId}/${clusterId}/cmd`;
}

export function commandAckTopic(siteId: string, clusterId: string): string {
  return `cluster/${siteId}/${clusterId}/cmd_ack`;
}

export function wrapTelemetry(
  timestamp: string,
  payload: ClusterTelemetry
): MqttEnvelope<ClusterTelemetry> {
  return {
    schemaVersion: MQTT_SCHEMA_VERSION,
    sentAt: timestamp,
    payload
  };
}

export function wrapAlert(
  timestamp: string,
  payload: ClusterAlert
): MqttEnvelope<ClusterAlert> {
  return {
    schemaVersion: MQTT_SCHEMA_VERSION,
    sentAt: timestamp,
    payload
  };
}

export function wrapCommand(
  timestamp: string,
  payload: RemoteCommand
): MqttEnvelope<RemoteCommand> {
  return {
    schemaVersion: MQTT_SCHEMA_VERSION,
    sentAt: timestamp,
    payload
  };
}

export function wrapCommandAck(
  timestamp: string,
  payload: CommandAcknowledgement
): MqttEnvelope<CommandAcknowledgement> {
  return {
    schemaVersion: MQTT_SCHEMA_VERSION,
    sentAt: timestamp,
    payload
  };
}
