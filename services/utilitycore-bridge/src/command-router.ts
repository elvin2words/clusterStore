import type { RemoteCommand } from "@clusterstore/contracts";

export interface CommandValidationContext {
  siteId: string;
  clusterId: string;
  now: Date;
  maxCommandTtlMs: number;
}

export function validateRemoteCommand(
  command: RemoteCommand,
  context: CommandValidationContext
): string[] {
  const issues: string[] = [];
  const nowMs = context.now.getTime();
  const createdAtMs = Date.parse(command.createdAt);
  const expiresAtMs = Date.parse(command.expiresAt);
  const payload =
    typeof command.payload === "object" && command.payload !== null
      ? command.payload
      : {};
  const target = command.target;
  const authorization = command.authorization;
  const authorizationIssuedAtMs = Date.parse(authorization?.issuedAt ?? "");
  const authorizationExpiresAtMs = Date.parse(authorization?.expiresAt ?? "");
  const supportedCommandTypes = new Set([
    "force_charge",
    "force_discharge",
    "set_dispatch_mode",
    "set_maintenance_mode",
    "clear_fault_latch"
  ]);

  if (!command.id) {
    issues.push("Command id is required.");
  }

  if (!command.idempotencyKey) {
    issues.push("Command idempotencyKey is required.");
  }

  if (!command.requestedBy) {
    issues.push("requestedBy is required.");
  }

  if (!supportedCommandTypes.has(command.type)) {
    issues.push("Command type is not supported.");
  }

  if (!Number.isFinite(createdAtMs)) {
    issues.push("Command createdAt must be a valid timestamp.");
  }

  if (!target?.siteId || !target?.clusterId) {
    issues.push("Command target siteId and clusterId are required.");
  } else {
    if (target.siteId !== context.siteId) {
      issues.push("Command target site does not match this bridge.");
    }

    if (target.clusterId !== context.clusterId) {
      issues.push("Command target cluster does not match this bridge.");
    }

    if (target.nodeIds !== undefined) {
      if (
        !Array.isArray(target.nodeIds) ||
        target.nodeIds.some((nodeId) => typeof nodeId !== "string" || nodeId.length === 0)
      ) {
        issues.push("target.nodeIds must be an array of non-empty node ids.");
      } else if (target.nodeIds.length > 0) {
        issues.push("Per-node targeting is not yet supported by this bridge.");
      }
    }
  }

  if (!authorization?.tokenId) {
    issues.push("authorization.tokenId is required.");
  }

  if (!authorization?.role) {
    issues.push("authorization.role is required.");
  }

  if (!Array.isArray(authorization?.scopes) || authorization.scopes.length === 0) {
    issues.push("authorization.scopes must contain at least one scope.");
  }

  if (!Number.isInteger(command.sequence) || command.sequence <= 0) {
    issues.push("Command sequence must be a positive integer.");
  }

  if (!Number.isFinite(expiresAtMs) || expiresAtMs <= nowMs) {
    issues.push("Command expiresAt must be a future timestamp.");
  } else if (expiresAtMs - nowMs > context.maxCommandTtlMs) {
    issues.push("Command expiresAt exceeds the allowed TTL.");
  }

  if (Number.isFinite(createdAtMs) && Number.isFinite(expiresAtMs) && createdAtMs > expiresAtMs) {
    issues.push("Command createdAt must be before expiresAt.");
  }

  if (!Number.isFinite(authorizationIssuedAtMs)) {
    issues.push("authorization.issuedAt must be a valid timestamp.");
  } else if (authorizationIssuedAtMs > nowMs) {
    issues.push("authorization.issuedAt cannot be in the future.");
  }

  if (!Number.isFinite(authorizationExpiresAtMs)) {
    issues.push("authorization.expiresAt must be a valid timestamp.");
  } else if (authorizationExpiresAtMs <= nowMs) {
    issues.push("authorization.expiresAt must be in the future.");
  }

  if (
    Number.isFinite(authorizationIssuedAtMs) &&
    Number.isFinite(authorizationExpiresAtMs) &&
    authorizationIssuedAtMs >= authorizationExpiresAtMs
  ) {
    issues.push("authorization.issuedAt must be before authorization.expiresAt.");
  }

  if (
    (command.type === "force_charge" || command.type === "force_discharge") &&
    typeof payload.currentA !== "number"
  ) {
    issues.push("Force charge/discharge commands require payload.currentA.");
  }

  if (
    command.type === "set_dispatch_mode" &&
    !["equal_current", "soc_weighted", "temperature_weighted"].includes(
      String(payload.dispatchStrategy ?? "")
    )
  ) {
    issues.push("set_dispatch_mode requires a supported dispatchStrategy.");
  }

  if (
    Array.isArray(authorization?.scopes) &&
    !authorization.scopes.includes(`cluster:${command.type}`)
  ) {
    issues.push("authorization.scopes does not allow this command type.");
  }

  return issues;
}
