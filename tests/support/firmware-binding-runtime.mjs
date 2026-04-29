export const SLOT_A = 0;
export const SLOT_B = 1;
export const PERSISTENT_STATE_COPY_COUNT = 2;

function clone(value) {
  return JSON.parse(JSON.stringify(value));
}

export function buildFlashLayout(config) {
  if (!config || !config.flashSizeBytes) {
    throw new Error("Flash layout config is required.");
  }

  if (
    config.flashSizeBytes <=
    config.bootloaderSizeBytes + config.metadataSizeBytes + config.journalSizeBytes
  ) {
    throw new Error("Flash layout does not leave room for application slots.");
  }

  const usableBytes =
    config.flashSizeBytes -
    config.bootloaderSizeBytes -
    config.metadataSizeBytes -
    config.journalSizeBytes;
  const slotSizeBytes = config.slotSizeBytes || Math.floor(usableBytes / 2);
  if (!slotSizeBytes || slotSizeBytes * 2 > usableBytes) {
    throw new Error("Slot sizing is invalid.");
  }

  const bootloaderAddress = config.flashBaseAddress;
  const slotAAddress = bootloaderAddress + config.bootloaderSizeBytes;
  const slotBAddress = slotAAddress + slotSizeBytes;
  const metadataAddress = slotBAddress + slotSizeBytes;
  const journalAddress = metadataAddress + config.metadataSizeBytes;
  const endAddress = config.flashBaseAddress + config.flashSizeBytes;
  if (journalAddress + config.journalSizeBytes > endAddress) {
    throw new Error("Journal spills beyond flash.");
  }

  return {
    bootloaderAddress,
    slotAAddress,
    slotBAddress,
    metadataAddress,
    journalAddress,
    endAddress,
    slotSizeBytes,
    metadataSizeBytes: config.metadataSizeBytes,
    journalSizeBytes: config.journalSizeBytes
  };
}

export function buildG474NodeLayout() {
  return {
    bootloaderAddress: 0x08000000,
    bootloaderSizeBytes: 32 * 1024,
    bcbPrimaryAddress: 0x08008000,
    bcbShadowAddress: 0x08009000,
    bcbCopySizeBytes: 4 * 1024,
    journalAddress: 0x0800a000,
    journalSizeBytes: 24 * 1024,
    journalMetaAAddress: 0x0800a000,
    journalMetaBAddress: 0x0800a800,
    journalRecordAreaAddress: 0x0800b000,
    slotAAddress: 0x08010000,
    slotBAddress: 0x08040000,
    slotSizeBytes: 192 * 1024,
    reservedAddress: 0x08070000,
    reservedSizeBytes: 64 * 1024,
    endAddress: 0x08080000
  };
}

function slotAddress(layout, slotId) {
  if (slotId === SLOT_A) {
    return layout.slotAAddress;
  }

  if (slotId === SLOT_B) {
    return layout.slotBAddress;
  }

  return 0;
}

function findSlot(control, slotId) {
  return control.slots.find((slot) => slot.slotId === slotId) ?? null;
}

export function createBootControl(layout, activeSlotId, activeVersion) {
  return {
    activeSlotId,
    fallbackSlotId: activeSlotId === SLOT_A ? SLOT_B : SLOT_A,
    stayInBootloader: false,
    slots: [
      {
        slotId: SLOT_A,
        state: activeSlotId === SLOT_A ? "confirmed" : "empty",
        rollbackRequested: false,
        remainingBootAttempts: 0,
        imageAddress: slotAddress(layout, SLOT_A),
        imageSizeBytes: 0,
        imageCrc32: 0,
        version: activeSlotId === SLOT_A ? activeVersion : ""
      },
      {
        slotId: SLOT_B,
        state: activeSlotId === SLOT_B ? "confirmed" : "empty",
        rollbackRequested: false,
        remainingBootAttempts: 0,
        imageAddress: slotAddress(layout, SLOT_B),
        imageSizeBytes: 0,
        imageCrc32: 0,
        version: activeSlotId === SLOT_B ? activeVersion : ""
      }
    ]
  };
}

export function activateBootSlot(
  control,
  layout,
  slotId,
  version,
  imageSizeBytes,
  imageCrc32,
  maxTrialBootAttempts
) {
  const slot = findSlot(control, slotId);
  if (!slot || !maxTrialBootAttempts) {
    throw new Error("Cannot activate slot without a valid target and trial budget.");
  }

  slot.imageAddress = slotAddress(layout, slotId);
  slot.imageSizeBytes = imageSizeBytes;
  slot.imageCrc32 = imageCrc32;
  slot.state = "pending_test";
  slot.rollbackRequested = false;
  slot.remainingBootAttempts = maxTrialBootAttempts;
  slot.version = version;
  control.fallbackSlotId = control.activeSlotId;
  control.activeSlotId = slotId;
  control.stayInBootloader = false;
  return control;
}

export function selectBootSlot(control) {
  const activeSlot = findSlot(control, control.activeSlotId);
  const fallbackSlot = findSlot(control, control.fallbackSlotId);
  if (control.stayInBootloader || !activeSlot) {
    return {
      action: "stay_in_bootloader",
      slotId: 0
    };
  }

  if (activeSlot.rollbackRequested) {
    if (fallbackSlot?.state === "confirmed") {
      const previousActive = activeSlot.slotId;
      control.activeSlotId = fallbackSlot.slotId;
      control.fallbackSlotId = previousActive;
      activeSlot.state = "invalid";
      activeSlot.rollbackRequested = false;
      return {
        action: "boot_fallback",
        slotId: fallbackSlot.slotId
      };
    }

    control.stayInBootloader = true;
    return {
      action: "stay_in_bootloader",
      slotId: 0
    };
  }

  if (activeSlot.state === "pending_test") {
    if (activeSlot.remainingBootAttempts === 0) {
      activeSlot.rollbackRequested = true;
      return selectBootSlot(control);
    }

    activeSlot.remainingBootAttempts -= 1;
    return {
      action: "boot_active",
      slotId: activeSlot.slotId
    };
  }

  if (activeSlot.state === "confirmed") {
    return {
      action: "boot_active",
      slotId: activeSlot.slotId
    };
  }

  if (fallbackSlot?.state === "confirmed") {
    return {
      action: "boot_fallback",
      slotId: fallbackSlot.slotId
    };
  }

  control.stayInBootloader = true;
  return {
    action: "stay_in_bootloader",
    slotId: 0
  };
}

export class InMemoryPersistentStateStore {
  constructor({ layout, journalCapacity, defaultActiveSlotId, defaultVersion }) {
    this.layout = layout;
    this.journalCapacity = journalCapacity;
    this.defaultActiveSlotId = defaultActiveSlotId;
    this.defaultVersion = defaultVersion;
    this.metadataCopies = new Array(PERSISTENT_STATE_COPY_COUNT).fill(null);
    this.journal = [];
  }

  defaultState() {
    return {
      activeCopyIndex: 0,
      generation: 0,
      journalCapacity: this.journalCapacity,
      journalMetadata: {
        head: 0,
        count: 0,
        nextSequence: 1
      },
      bootControl: createBootControl(
        this.layout,
        this.defaultActiveSlotId,
        this.defaultVersion
      )
    };
  }

  load() {
    const validCopies = this.metadataCopies
      .filter((copy) => copy?.valid)
      .sort((left, right) => right.generation - left.generation);
    return validCopies.length > 0 ? clone(validCopies[0]) : this.defaultState();
  }

  save(state) {
    const targetCopyIndex = (state.activeCopyIndex + 1) % PERSISTENT_STATE_COPY_COUNT;
    const savedState = clone({
      ...state,
      generation: state.generation + 1,
      activeCopyIndex: targetCopyIndex,
      valid: true
    });
    this.metadataCopies[targetCopyIndex] = savedState;
    return clone(savedState);
  }

  flushJournal(state, records, metadata) {
    this.journal = records.slice(0, state.journalCapacity).map((record) => clone(record));
    return this.save({
      ...state,
      journalMetadata: clone(metadata)
    });
  }

  restoreJournal() {
    return this.journal.map((record) => clone(record));
  }

  corruptCopy(copyIndex) {
    if (this.metadataCopies[copyIndex]) {
      this.metadataCopies[copyIndex].valid = false;
    }
  }
}
