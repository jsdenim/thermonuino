const slotsPerWeek = 7 * 24 * 4;
const dayNames = ["Lun", "Mar", "Mer", "Jeu", "Ven", "Sam", "Dim"];

const moduleStatus = document.querySelector("#module-status");
const slotLabel = document.querySelector("#slot-label");
const absoluteLabel = document.querySelector("#absolute-label");
const decisionLabel = document.querySelector("#decision-label");
const targetLabel = document.querySelector("#target-label");
const powerLabel = document.querySelector("#power-label");
const modeLabel = document.querySelector("#mode-label");
const playButton = document.querySelector("#play-button");
const stepButton = document.querySelector("#step-button");
const resetButton = document.querySelector("#reset-button");
const rerunButton = document.querySelector("#rerun-button");
const saveScenarioButton = document.querySelector("#save-scenario-button");
const loadScenarioButton = document.querySelector("#load-scenario-button");
const loadScenarioInput = document.querySelector("#load-scenario-input");
const loopToggle = document.querySelector("#loop-toggle");
const speedButtons = Array.from(document.querySelectorAll("[data-speed]"));
const measuredTempInput = document.querySelector("#measured-temp");
const outsideTempInput = document.querySelector("#outside-temp");
const thermalLossInput = document.querySelector("#thermal-loss");
const thermalMassInput = document.querySelector("#thermal-mass");
const baseTempInput = document.querySelector("#base-temp");
const variationMinusButton = document.querySelector("#variation-minus");
const variationPlusButton = document.querySelector("#variation-plus");
const learningToggle = document.querySelector("#learning-toggle");
const presenceToggle = document.querySelector("#presence-toggle");
const presencePulseButton = document.querySelector("#presence-pulse");
const doorOpenPulseButton = document.querySelector("#door-open-pulse");
const weekChart = document.querySelector("#week-chart");
const serialLog = document.querySelector("#serial-log");
const clearLogButton = document.querySelector("#clear-log-button");

let wasm = {
  evaluateSlot: null,
  evaluateRegulation: null,
  observeRegulation: null,
  setup: null,
  reset: null,
  resetRegulation: null,
};
let timer = null;
let absoluteSlot = 0;
let weekResults = Array(slotsPerWeek).fill(null);
let presenceDetected = false;
let currentSlotVariation = 0;
let pendingUserAction = false;
let pendingPresencePulse = false;
let pendingDoorOpenPulse = false;
let learningEnabled = true;
let playbackDelay = 500;
let variationHoldTimer = null;
let variationTargetSlot = null;
let variationBaseTarget = null;
let variationTargetDelta = 0;
let scenarioEvents = [];
let restoringScenario = false;
const chartContext = weekChart.getContext("2d");
const variationStep = 0.5;
const variationMin = -8;
const variationMax = 8;
const variationHoldStepCount = 6;
const variationHoldMinMs = 650;
const variationHoldMaxMs = 5000;
const btuPerWattHour = 3.412141633;
const slotHours = 0.25;
const defaultInstalledPowerW = 2100;
const defaultLossBtuPerHourC = 450;
const defaultThermalMassBtuPerC = 20000;

function readNumber(input) {
  return Number.parseFloat(input.value);
}

function writeNumber(input, value, digits = 2) {
  input.value = Number.isFinite(value) ? value.toFixed(digits) : input.value;
}

function clamp(value, min, max) {
  return Math.min(max, Math.max(min, value));
}

function resetRegulationLearning() {
  if (wasm.resetRegulation) {
    wasm.resetRegulation();
  }
}

function exportRegulationState() {
  return null;
}

function importRegulationState(state) {
  resetRegulationLearning();
}

function setPresence(value) {
  presenceDetected = value;
  presenceToggle.setAttribute("aria-pressed", String(value));
  presenceToggle.textContent = value ? "Detectee" : "Absente";
  presenceToggle.classList.toggle("is-off", !value);
}

function setLearningEnabled(value) {
  learningEnabled = value;
  learningToggle.setAttribute("aria-pressed", String(value));
  learningToggle.textContent = value ? "Actif" : "Inactif";
  learningToggle.classList.toggle("is-off", !value);
}

function clearVariationHold() {
  if (variationHoldTimer) {
    window.clearTimeout(variationHoldTimer);
    variationHoldTimer = null;
  }
  variationTargetSlot = null;
  variationBaseTarget = null;
  variationTargetDelta = 0;
}

function clearImpulseInputs() {
  clearVariationHold();
  currentSlotVariation = 0;
  pendingUserAction = false;
  pendingPresencePulse = false;
  pendingDoorOpenPulse = false;
}

function clearPendingImpulses() {
  currentSlotVariation = 0;
  pendingUserAction = false;
  pendingPresencePulse = false;
  pendingDoorOpenPulse = false;
}

function getVariationHoldMs() {
  return clamp(
    playbackDelay * variationHoldStepCount,
    variationHoldMinMs,
    variationHoldMaxMs,
  );
}

function stepVariation(delta) {
  if (variationTargetSlot === null) {
    variationTargetSlot = absoluteSlot;
    variationBaseTarget = evaluateEntry(variationTargetSlot, true).learnedTarget;
    variationTargetDelta = 0;
  }

  variationTargetDelta = clamp(variationTargetDelta + delta, variationMin, variationMax);
  absoluteSlot = variationTargetSlot;
  const currentTarget = evaluateEntry(variationTargetSlot, true).learnedTarget;
  currentSlotVariation = variationBaseTarget + variationTargetDelta - currentTarget;
  pendingUserAction = true;
  pendingPresencePulse = true;
  executeSlot(false);

  if (variationHoldTimer) {
    window.clearTimeout(variationHoldTimer);
  }
  variationHoldTimer = window.setTimeout(clearVariationHold, getVariationHoldMs());
}

function pulsePresence() {
  clearVariationHold();
  currentSlotVariation = 0;
  pendingUserAction = false;
  pendingPresencePulse = true;
  executeSlot(false);
}

function pulseDoorOpen() {
  clearVariationHold();
  currentSlotVariation = 0;
  pendingUserAction = false;
  pendingDoorOpenPulse = true;
  executeSlot(false);
}

function formatSlot(slotOfWeek) {
  const day = Math.floor(slotOfWeek / 96);
  const slotOfDay = slotOfWeek % 96;
  const minutes = slotOfDay * 15;
  const hour = Math.floor(minutes / 60);
  const minute = minutes % 60;

  return `${dayNames[day]} ${String(hour).padStart(2, "0")}:${String(minute).padStart(2, "0")}`;
}

function appendLog(entry, replayOnly) {
  const line = [
    replayOnly ? "REPLAY" : "RUN",
    `#${entry.absoluteSlot}`,
    formatSlot(entry.slotOfWeek),
    entry.mode,
    `target=${entry.learnedTarget.toFixed(1)}`,
    `conf=${entry.confidence}`,
    entry.scheduleChanged ? "learned" : null,
    entry.contradiction ? "contradiction" : null,
    entry.candidateActive ? `candidate=${entry.candidateTarget.toFixed(1)}x${entry.candidateCount}` : null,
    `variation=${entry.userVariation.toFixed(1)}`,
    `learning=${entry.learningEnabled ? "on" : "off"}`,
    `presence=${entry.presenceDetected ? "yes" : "no"}`,
    entry.presenceDetected !== entry.previousPresenceDetected ? "presence-updated" : null,
    entry.doorOpened ? "door-open" : null,
    `airing=${entry.doorOpenHabit}`,
    `measured=${entry.measured.toFixed(1)}`,
    `outside=${entry.outsideTemp?.toFixed?.(1) ?? readNumber(outsideTempInput).toFixed(1)}`,
    `hold=${Math.round(entry.learnedHoldBtuPerHour ?? 0)}BTU/h@${entry.holdConfidence ?? 0}`,
    `response=${Math.round(entry.learnedResponseBtuPerC ?? 0)}BTU/C`,
    `demand=${Math.round(entry.requestedBtuPerHour ?? 0)}BTU/h`,
    `requested=${entry.requestedPowerW}W`,
    `installed=${entry.installedPowerW}W`,
    `workload=${entry.workload}/255`,
  ].filter(Boolean).join(" | ");

  serialLog.textContent = `${line}\n${serialLog.textContent}`.slice(0, 12000);
}

function render(entry, replayOnly) {
  slotLabel.textContent = formatSlot(entry.slotOfWeek);
  absoluteLabel.textContent = `Execution ${entry.absoluteSlot}`;
  decisionLabel.textContent = entry.heating ? "Chauffe" : "Arret";
  targetLabel.textContent = `Consigne ${entry.learnedTarget.toFixed(1)} C`;
  powerLabel.textContent = `${entry.requestedPowerW} W`;
  modeLabel.textContent = `PWM ${entry.workload}/255`;
  const previousEntry = weekResults[entry.slotOfWeek];
  if (replayOnly && previousEntry && previousEntry.doorOpened) {
    entry.doorOpened = true;
  }
  weekResults[entry.slotOfWeek] = entry;
  drawChart();
  appendLog(entry, replayOnly);
}

function recordScenarioEvent(event) {
  if (restoringScenario) {
    return;
  }
  scenarioEvents.push({
    absoluteSlot: event.absoluteSlot,
    measuredTemp: event.measuredTemp,
    outsideTemp: event.outsideTemp,
    thermalLossBtuPerHourC: event.thermalLossBtuPerHourC,
    thermalMassBtuPerC: event.thermalMassBtuPerC,
    userVariation: event.userVariation,
    presenceDetected: event.presenceDetected,
    explicitUserAction: event.explicitUserAction,
    temporaryOverride: event.temporaryOverride,
    learningEnabled: event.learningEnabled,
    doorOpened: event.doorOpened,
  });
}

function evaluateEntry(slot, replayOnly = true) {
  const json = wasm.evaluateSlot(
    slot,
    readNumber(measuredTempInput),
    0,
    0,
    replayOnly ? 1 : 0,
    0,
    0,
    learningEnabled ? 1 : 0,
    0,
  );
  return JSON.parse(json);
}

function readThermalParameters(source = {}) {
  return {
    outsideTemp: Number.isFinite(source.outsideTemp)
      ? source.outsideTemp
      : readNumber(outsideTempInput),
    thermalLossBtuPerHourC: Math.max(
      0,
      Number.isFinite(source.thermalLossBtuPerHourC)
        ? source.thermalLossBtuPerHourC
        : readNumber(thermalLossInput),
    ),
    thermalMassBtuPerC: Math.max(
      1,
      Number.isFinite(source.thermalMassBtuPerC)
        ? source.thermalMassBtuPerC
        : readNumber(thermalMassInput),
    ),
  };
}

function normalizeLossBtuPerHourC(value, fallback = defaultLossBtuPerHourC) {
  const parsed = Number.parseFloat(value);
  return Number.isFinite(parsed) && parsed >= 0 ? parsed : fallback;
}

function normalizeThermalMassBtuPerC(value, oldGainCPerHour, fallback = defaultThermalMassBtuPerC) {
  const parsed = Number.parseFloat(value);
  if (Number.isFinite(parsed) && parsed > 0) {
    return parsed;
  }
  const oldGain = Number.parseFloat(oldGainCPerHour);
  if (Number.isFinite(oldGain) && oldGain > 0) {
    return (defaultInstalledPowerW * btuPerWattHour) / oldGain;
  }
  return fallback;
}

function applyThermalPowerDecision(entry, parameters = {}) {
  const thermal = readThermalParameters(parameters);
  const measuredDeciC = Math.round(
    (Number.isFinite(entry.measured) ? entry.measured : readNumber(measuredTempInput)) * 10,
  );
  const targetDeciC = Math.round(entry.learnedTarget * 10);
  const installedPowerW = Math.max(1, entry.installedPowerW || defaultInstalledPowerW);
  const regulation = JSON.parse(wasm.evaluateRegulation(
    entry.zone || 0,
    measuredDeciC,
    targetDeciC,
    installedPowerW,
    entry.doorOpened ? 1 : 0,
    0,
  ));

  Object.assign(entry, regulation);
  entry.requestedBtuPerHour = regulation.requestedBtuPerHour;
  entry.maintenanceBtuPerHour = regulation.maintenanceBtuPerHour;
  entry.catchupBtuPerHour = regulation.catchupBtuPerHour;
  entry.learnedHoldBtuPerHour = regulation.learnedHoldBtuPerHour;
  entry.learnedResponseBtuPerC = regulation.learnedResponseBtuPerC;
  entry.holdConfidence = regulation.holdConfidence;
  entry.outsideTemp = thermal.outsideTemp;
  entry.thermalLossBtuPerHourC = thermal.thermalLossBtuPerHourC;
  entry.thermalMassBtuPerC = thermal.thermalMassBtuPerC;
  entry.heating = regulation.heating;
  entry.idle = !regulation.heating;
  return entry;
}

function observeThermalPowerDecision(entry, nextTemp) {
  if (!wasm.observeRegulation || !Number.isFinite(entry.measured) || !Number.isFinite(nextTemp)) {
    return;
  }
  wasm.observeRegulation(
    entry.zone || 0,
    Math.round(entry.measured * 10),
    Math.round(nextTemp * 10),
    Math.round(entry.installedPowerW || defaultInstalledPowerW),
    Math.round(entry.requestedBtuPerHour || 0),
    Math.round(entry.maintenanceBtuPerHour || 0),
    learningEnabled ? 1 : 0,
  );
}

function refreshWeekProjection() {
  if (!wasm.evaluateSlot) {
    return;
  }

  const previousResults = weekResults;
  const weekBase = Math.floor(absoluteSlot / slotsPerWeek) * slotsPerWeek;
  weekResults = previousResults.map((previousEntry, slot) => {
    const entry = evaluateEntry(weekBase + slot, true);
    if (previousEntry) {
      entry.userVariation = previousEntry.userVariation;
      entry.explicitUserAction = previousEntry.explicitUserAction;
      entry.presenceDetected = previousEntry.presenceDetected || entry.presenceDetected;
      entry.doorOpened = previousEntry.doorOpened || entry.doorOpened;
      entry.measured = previousEntry.measured;
      entry.outsideTemp = previousEntry.outsideTemp;
      entry.thermalLossBtuPerHourC = previousEntry.thermalLossBtuPerHourC;
      entry.thermalMassBtuPerC = previousEntry.thermalMassBtuPerC;
    } else {
      entry.measured = NaN;
    }
    return applyThermalPowerDecision(entry, entry);
  });
  drawChart();
}

function computeNextMeasuredTemp(entry) {
  const measured = entry.measured;
  const thermal = readThermalParameters(entry);
  const heatBtuPerHour = Number.isFinite(entry.requestedBtuPerHour)
    ? entry.requestedBtuPerHour
    : Math.max(0, entry.requestedPowerW || 0) * btuPerWattHour;
  const lossBtuPerHour = (thermal.outsideTemp - measured) * thermal.thermalLossBtuPerHourC;
  const netBtu = (heatBtuPerHour + lossBtuPerHour) * slotHours;
  return measured + (netBtu / thermal.thermalMassBtuPerC);
}

function advanceThermalModel(entry) {
  const nextTemp = computeNextMeasuredTemp(entry);
  observeThermalPowerDecision(entry, nextTemp);
  writeNumber(measuredTempInput, nextTemp, 2);
}

function chartX(slot, bounds) {
  return bounds.left + (slot / (slotsPerWeek - 1)) * bounds.width;
}

function chartY(temp, minTemp, maxTemp, bounds) {
  return bounds.top + ((maxTemp - temp) / (maxTemp - minTemp)) * bounds.height;
}

function getChartBounds(cssWidth, cssHeight) {
  return {
    left: 46,
    top: 18,
    width: cssWidth - 66,
    height: cssHeight - 54,
  };
}

function seekToSlot(slotOfWeek) {
  stopPlayback();
  const weekBase = Math.floor(absoluteSlot / slotsPerWeek) * slotsPerWeek;
  absoluteSlot = weekBase + clamp(slotOfWeek, 0, slotsPerWeek - 1);
  clearImpulseInputs();
  executeSlot(true);
}

function drawChart() {
  const rect = weekChart.getBoundingClientRect();
  const ratio = window.devicePixelRatio || 1;
  const cssWidth = Math.max(320, Math.floor(rect.width));
  const cssHeight = 300;

  if (weekChart.width !== cssWidth * ratio || weekChart.height !== cssHeight * ratio) {
    weekChart.width = cssWidth * ratio;
    weekChart.height = cssHeight * ratio;
  }

  chartContext.setTransform(ratio, 0, 0, ratio, 0, 0);
  chartContext.clearRect(0, 0, cssWidth, cssHeight);

  const bounds = getChartBounds(cssWidth, cssHeight);
  const values = weekResults.filter(Boolean);
  const baseTemp = readNumber(baseTempInput);
  const rawTemps = values
    .flatMap((entry) => [entry.learnedTarget, entry.measured])
    .filter(Number.isFinite);
  const minTemp = Math.floor(Math.min(baseTemp - 3, ...rawTemps) - 1);
  const maxTemp = Math.ceil(Math.max(baseTemp + 4, ...rawTemps) + 1);

  chartContext.fillStyle = "#ffffff";
  chartContext.fillRect(0, 0, cssWidth, cssHeight);
  chartContext.strokeStyle = "#d8dee7";
  chartContext.lineWidth = 1;

  for (let day = 0; day <= 7; day += 1) {
    const x = bounds.left + (day / 7) * bounds.width;
    chartContext.beginPath();
    chartContext.moveTo(x, bounds.top);
    chartContext.lineTo(x, bounds.top + bounds.height);
    chartContext.stroke();
  }

  for (let temp = minTemp; temp <= maxTemp; temp += 1) {
    const y = chartY(temp, minTemp, maxTemp, bounds);
    chartContext.beginPath();
    chartContext.moveTo(bounds.left, y);
    chartContext.lineTo(bounds.left + bounds.width, y);
    chartContext.stroke();

    if (temp % 2 === 0) {
      chartContext.fillStyle = "#607083";
      chartContext.fillText(`${temp} C`, 8, y + 4);
    }
  }

  chartContext.strokeStyle = "#1b6fd8";
  chartContext.lineWidth = 2;
  chartContext.beginPath();
  let started = false;
  weekResults.forEach((entry, slot) => {
    if (!entry) {
      return;
    }
    const x = chartX(slot, bounds);
    const y = chartY(entry.learnedTarget, minTemp, maxTemp, bounds);
    if (!started) {
      chartContext.moveTo(x, y);
      started = true;
      return;
    }
    chartContext.lineTo(x, y);
  });
  chartContext.stroke();

  chartContext.strokeStyle = "#e08b21";
  chartContext.lineWidth = 2;
  chartContext.setLineDash([6, 5]);
  chartContext.beginPath();
  started = false;
  weekResults.forEach((entry, slot) => {
    if (!entry || !Number.isFinite(entry.measured)) {
      return;
    }
    const x = chartX(slot, bounds);
    const y = chartY(entry.measured, minTemp, maxTemp, bounds);
    if (!started) {
      chartContext.moveTo(x, y);
      started = true;
      return;
    }
    chartContext.lineTo(x, y);
  });
  chartContext.stroke();
  chartContext.setLineDash([]);

  weekResults.forEach((entry, slot) => {
    if (!entry || !entry.doorOpened) {
      return;
    }
    const x = chartX(slot, bounds);
    const y = bounds.top - 8;
    chartContext.beginPath();
    chartContext.moveTo(x, y + 5);
    chartContext.lineTo(x + 5, y - 4);
    chartContext.lineTo(x - 5, y - 4);
    chartContext.closePath();
    chartContext.fillStyle = "#8b5a2b";
    chartContext.fill();
  });

  weekResults.forEach((entry, slot) => {
    if (!entry || Math.abs(entry.userVariation) < 0.001) {
      return;
    }
    const x = chartX(slot, bounds);
    const y = chartY(entry.learnedTarget, minTemp, maxTemp, bounds);
    chartContext.beginPath();
    chartContext.arc(x, y, 3.5, 0, Math.PI * 2);
    chartContext.fillStyle = entry.userVariation > 0 ? "#d94b35" : "#2b8c5f";
    chartContext.fill();
  });

  weekResults.forEach((entry, slot) => {
    if (!entry || !entry.presenceDetected) {
      return;
    }
    const x = chartX(slot, bounds);
    const y = bounds.top + bounds.height + 8;
    chartContext.beginPath();
    chartContext.moveTo(x, y - 5);
    chartContext.lineTo(x + 5, y + 4);
    chartContext.lineTo(x - 5, y + 4);
    chartContext.closePath();
    chartContext.fillStyle = "#7a4bc2";
    chartContext.fill();
  });

  const playheadX = chartX(absoluteSlot % slotsPerWeek, bounds);
  chartContext.strokeStyle = "#111827";
  chartContext.lineWidth = 1;
  chartContext.beginPath();
  chartContext.moveTo(playheadX, bounds.top - 2);
  chartContext.lineTo(playheadX, bounds.top + bounds.height + 17);
  chartContext.stroke();
  chartContext.fillStyle = "#111827";
  chartContext.beginPath();
  chartContext.moveTo(playheadX, bounds.top - 2);
  chartContext.lineTo(playheadX - 5, bounds.top - 10);
  chartContext.lineTo(playheadX + 5, bounds.top - 10);
  chartContext.closePath();
  chartContext.fill();

  chartContext.fillStyle = "#607083";
  chartContext.textAlign = "center";
  dayNames.forEach((day, index) => {
    const x = bounds.left + ((index + 0.5) / 7) * bounds.width;
    chartContext.fillText(day, x, cssHeight - 16);
  });
  chartContext.textAlign = "start";
}

function executeSlot(replayOnly = false) {
  if (!wasm.evaluateSlot) {
    return;
  }

  const userVariation = replayOnly ? 0 : currentSlotVariation;
  const explicitUserAction = pendingUserAction && !replayOnly;
  const temporaryOverride = !explicitUserAction && Math.abs(userVariation) >= 0.001;
  const effectivePresence = presenceDetected || pendingPresencePulse || explicitUserAction;
  const doorOpened = pendingDoorOpenPulse && !replayOnly;
  const measuredTemp = readNumber(measuredTempInput);
  const outsideTemp = readNumber(outsideTempInput);
  const thermalLossBtuPerHourC = readNumber(thermalLossInput);
  const thermalMassBtuPerC = readNumber(thermalMassInput);
  const json = wasm.evaluateSlot(
    absoluteSlot,
    measuredTemp,
    userVariation,
    effectivePresence ? 1 : 0,
    replayOnly ? 1 : 0,
    explicitUserAction ? 1 : 0,
    temporaryOverride ? 1 : 0,
    learningEnabled ? 1 : 0,
    doorOpened ? 1 : 0,
  );
  if (!replayOnly) {
    recordScenarioEvent({
      absoluteSlot,
      measuredTemp,
      outsideTemp,
      thermalLossBtuPerHourC,
      thermalMassBtuPerC,
      userVariation,
      presenceDetected: effectivePresence,
      explicitUserAction,
      temporaryOverride,
      learningEnabled,
      doorOpened,
    });
  }
  if (!replayOnly) {
    clearPendingImpulses();
  }
  const entry = JSON.parse(json);
  applyThermalPowerDecision(entry, {
    outsideTemp,
    thermalLossBtuPerHourC,
    thermalMassBtuPerC,
  });
  render(entry, replayOnly);
  if (!replayOnly && explicitUserAction) {
    refreshWeekProjection();
  }
  return entry;
}

function stopPlayback() {
  if (timer) {
    window.clearInterval(timer);
    timer = null;
  }
  playButton.textContent = "Lecture";
}

function schedulePlayback() {
  stopPlayback();
  timer = window.setInterval(() => {
    stepForward();
  }, playbackDelay);
  playButton.textContent = "Pause";
}

function stepForward() {
  const nextSlotOfWeek = (absoluteSlot + 1) % slotsPerWeek;

  if (!loopToggle.checked && nextSlotOfWeek === 0 && absoluteSlot > 0) {
    stopPlayback();
    return;
  }

  clearPendingImpulses();
  absoluteSlot += 1;
  const entry = executeSlot(false);
  if (entry) {
    advanceThermalModel(entry);
  }
}

function resetSimulation() {
  stopPlayback();
  absoluteSlot = 0;
  weekResults = Array(slotsPerWeek).fill(null);
  scenarioEvents = [];
  if (wasm.setup) {
    wasm.setup(readNumber(baseTempInput));
  } else if (wasm.reset) {
    wasm.reset();
  }
  resetRegulationLearning();
  clearImpulseInputs();
  serialLog.textContent = "";
  executeSlot(false);
}

function exportScenario() {
  const data = {
    format: "thermonuino-simul-scenario",
    version: 1,
    exportedAt: new Date().toISOString(),
    baseTemp: readNumber(baseTempInput),
    measuredTemp: readNumber(measuredTempInput),
    outsideTemp: readNumber(outsideTempInput),
    thermalLossBtuPerHourC: readNumber(thermalLossInput),
    thermalMassBtuPerC: readNumber(thermalMassInput),
    learningEnabled,
    presenceDetected,
    absoluteSlot,
    playbackDelay,
    loop: loopToggle.checked,
    regulation: exportRegulationState(),
    events: scenarioEvents,
  };
  const blob = new Blob([JSON.stringify(data, null, 2)], { type: "application/json" });
  const url = URL.createObjectURL(blob);
  const link = document.createElement("a");
  link.href = url;
  link.download = `thermonuino-scenario-${Date.now()}.json`;
  document.body.appendChild(link);
  link.click();
  link.remove();
  URL.revokeObjectURL(url);
}

function normalizeScenarioEvent(event) {
  const thermalMassBtuPerC = normalizeThermalMassBtuPerC(
    event.thermalMassBtuPerC,
    event.thermalGainCPerHour,
  );
  return {
    absoluteSlot: Number.parseInt(event.absoluteSlot, 10) || 0,
    measuredTemp: Number.parseFloat(event.measuredTemp),
    outsideTemp: Number.parseFloat(event.outsideTemp),
    thermalLossBtuPerHourC: normalizeLossBtuPerHourC(event.thermalLossBtuPerHourC),
    thermalMassBtuPerC,
    userVariation: Number.parseFloat(event.userVariation) || 0,
    presenceDetected: Boolean(event.presenceDetected),
    explicitUserAction: Boolean(event.explicitUserAction),
    temporaryOverride: Boolean(event.temporaryOverride),
    learningEnabled: event.learningEnabled !== false,
    doorOpened: Boolean(event.doorOpened),
  };
}

function replayScenarioEvent(event) {
  absoluteSlot = event.absoluteSlot;
  const json = wasm.evaluateSlot(
    event.absoluteSlot,
    Number.isFinite(event.measuredTemp) ? event.measuredTemp : readNumber(measuredTempInput),
    event.userVariation,
    event.presenceDetected ? 1 : 0,
    0,
    event.explicitUserAction ? 1 : 0,
    event.temporaryOverride ? 1 : 0,
    event.learningEnabled ? 1 : 0,
    event.doorOpened ? 1 : 0,
  );
  const entry = JSON.parse(json);
  entry.outsideTemp = Number.isFinite(event.outsideTemp) ? event.outsideTemp : readNumber(outsideTempInput);
  applyThermalPowerDecision(entry, {
    outsideTemp: entry.outsideTemp,
    thermalLossBtuPerHourC: Number.isFinite(event.thermalLossBtuPerHourC)
      ? event.thermalLossBtuPerHourC
      : readNumber(thermalLossInput),
    thermalMassBtuPerC: Number.isFinite(event.thermalMassBtuPerC)
      ? event.thermalMassBtuPerC
      : readNumber(thermalMassInput),
  });
  render(entry, false);
}

function importScenario(data) {
  if (!data || data.format !== "thermonuino-simul-scenario" || !Array.isArray(data.events)) {
    throw new Error("Format de scenario Thermonuino invalide");
  }
  stopPlayback();
  clearImpulseInputs();
  baseTempInput.value = Number.isFinite(Number.parseFloat(data.baseTemp)) ? data.baseTemp : 17;
  measuredTempInput.value = Number.isFinite(Number.parseFloat(data.measuredTemp)) ? data.measuredTemp : 18.5;
  outsideTempInput.value = Number.isFinite(Number.parseFloat(data.outsideTemp)) ? data.outsideTemp : 7;
  thermalLossInput.value = normalizeLossBtuPerHourC(data.thermalLossBtuPerHourC);
  thermalMassInput.value = normalizeThermalMassBtuPerC(
    data.thermalMassBtuPerC,
    data.thermalGainCPerHour,
  );
  setLearningEnabled(data.learningEnabled !== false);
  setPresence(Boolean(data.presenceDetected));
  loopToggle.checked = data.loop !== false;
  playbackDelay = Number.parseInt(data.playbackDelay, 10) || playbackDelay;
  speedButtons.forEach((button) => {
    button.classList.toggle("is-active", Number.parseInt(button.dataset.speed, 10) === playbackDelay);
  });

  weekResults = Array(slotsPerWeek).fill(null);
  serialLog.textContent = "";
  if (wasm.setup) {
    wasm.setup(readNumber(baseTempInput));
  } else if (wasm.reset) {
    wasm.reset();
  }
  importRegulationState(data.regulation);

  const importedEvents = data.events.map(normalizeScenarioEvent);
  restoringScenario = true;
  try {
    importedEvents.forEach(replayScenarioEvent);
  } finally {
    restoringScenario = false;
  }
  scenarioEvents = importedEvents;
  const importedAbsoluteSlot = Number.parseInt(data.absoluteSlot, 10);
  absoluteSlot = Number.isFinite(importedAbsoluteSlot)
    ? importedAbsoluteSlot
    : (importedEvents.at(-1)?.absoluteSlot ?? 0);
  executeSlot(true);
  moduleStatus.textContent = "Scenario charge";
}

createGreetingsModule().then((module) => {
  wasm.evaluateSlot = module.cwrap("evaluateThermostatSlotEx", "string", [
    "number",
    "number",
    "number",
    "number",
    "number",
    "number",
    "number",
    "number",
    "number",
  ]);
  wasm.evaluateRegulation = module.cwrap("evaluateHeatingRegulator", "string", [
    "number",
    "number",
    "number",
    "number",
    "number",
    "number",
  ]);
  wasm.observeRegulation = module.cwrap("observeHeatingRegulator", null, [
    "number",
    "number",
    "number",
    "number",
    "number",
    "number",
    "number",
  ]);
  wasm.setup = module.cwrap("setupThermostat", null, ["number"]);
  wasm.reset = module.cwrap("resetThermostat", null, []);
  wasm.resetRegulation = module.cwrap("resetHeatingRegulator", null, []);
  moduleStatus.textContent = "WASM pret";
  resetSimulation();
});

playButton.addEventListener("click", () => {
  if (timer) {
    stopPlayback();
    return;
  }
  schedulePlayback();
});

stepButton.addEventListener("click", () => {
  stopPlayback();
  stepForward();
});

resetButton.addEventListener("click", resetSimulation);
rerunButton.addEventListener("click", () => {
  clearImpulseInputs();
  executeSlot(true);
});
saveScenarioButton.addEventListener("click", exportScenario);
loadScenarioButton.addEventListener("click", () => {
  loadScenarioInput.value = "";
  loadScenarioInput.click();
});
loadScenarioInput.addEventListener("change", () => {
  const file = loadScenarioInput.files?.[0];
  if (!file) {
    return;
  }
  file.text()
    .then((text) => importScenario(JSON.parse(text)))
    .catch((error) => {
      moduleStatus.textContent = "Scenario K.O.";
      console.error(error);
      window.alert(error.message || "Impossible de charger le scenario");
    });
});
clearLogButton.addEventListener("click", () => {
  serialLog.textContent = "";
});

speedButtons.forEach((button) => {
  button.addEventListener("click", () => {
    playbackDelay = Number.parseInt(button.dataset.speed, 10);
    speedButtons.forEach((speedButton) => {
      speedButton.classList.toggle("is-active", speedButton === button);
    });
    if (timer) {
      schedulePlayback();
    }
  });
});

measuredTempInput.addEventListener("change", () => {
  clearImpulseInputs();
  executeSlot(true);
});
outsideTempInput.addEventListener("change", () => {
  clearImpulseInputs();
  executeSlot(true);
});
thermalLossInput.addEventListener("change", () => {
  clearImpulseInputs();
  executeSlot(true);
});
thermalMassInput.addEventListener("change", () => {
  clearImpulseInputs();
  executeSlot(true);
});

variationMinusButton.addEventListener("click", () => stepVariation(-variationStep));
variationPlusButton.addEventListener("click", () => stepVariation(variationStep));
learningToggle.addEventListener("click", () => {
  setLearningEnabled(!learningEnabled);
  clearImpulseInputs();
  executeSlot(true);
});
presenceToggle.addEventListener("click", () => {
  setPresence(!presenceDetected);
  clearImpulseInputs();
  executeSlot(true);
});
presencePulseButton.addEventListener("click", pulsePresence);
doorOpenPulseButton.addEventListener("click", pulseDoorOpen);

weekChart.addEventListener("click", (event) => {
  const rect = weekChart.getBoundingClientRect();
  const bounds = getChartBounds(Math.max(320, Math.floor(rect.width)), 300);
  const localX = event.clientX - rect.left;
  const ratio = clamp((localX - bounds.left) / bounds.width, 0, 1);
  seekToSlot(Math.round(ratio * (slotsPerWeek - 1)));
});

baseTempInput.addEventListener("change", resetSimulation);
window.addEventListener("resize", drawChart);
