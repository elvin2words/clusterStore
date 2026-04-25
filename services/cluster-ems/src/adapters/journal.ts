import type { OperationalEvent } from "@clusterstore/contracts";

export interface OperationalJournalPort {
  record(event: OperationalEvent): Promise<void>;
}

