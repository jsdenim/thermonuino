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
const loopToggle = document.querySelector("#loop-toggle");
const speedButtons = Array.from(document.querySelectorAll("[data-speed]"));
const measuredTempInput = document.querySelector("#measured-temp");
const baseTempInput = document.querySelector("#base-temp");
const variationMinusButton = document.querySelector("#variation-minus");
const variationPlusButton = document.querySelector("#variation-plus");
const presenceToggle = document.querySelector("#presence-toggle");
const presencePulseButton = document.querySelector("#presence-pulse");
const weekChart = document.querySelector("#week-chart");
const serialLog = document.querySelector("#serial-log");
const clearLogButton = document.querySelector("#clear-log-button");

let wasm = {
  evaluateSlot: null,
  setup: null,
  reset: null,
};
let timer = null;
let absoluteSlot = 0;
let weekResults = Array(slotsPerWeek).fill(null);
let presenceDetected = false;
let currentSlotVariation = 0;
let pendingUserAction = false;
let pendingPresencePulse = false;
let playbackDelay = 500;
let variationHoldTimer = null;
let variationTargetSlot = null;
let variationTargetDelta = 0;
const chartContext = weekChart.getContext("2d");
const variationStep = 0.5;
const variationMin = -8;
const variationMax = 8;
const variationHoldMs = 5000;

function readNumber(input) {
  return Number.parseFloat(input.value);
}

function clamp(value, min, max) {
  return Math.min(max, Math.max(min, value));
}

function setPresence(value) {
  presenceDetected = value;
  presenceToggle.setAttribute("aria-pressed", String(value));
  presenceToggle.textContent = value ? "Detectee" : "Absente";
  presenceToggle.classList.toggle("is-off", !value);
}

function clearVariationHold() {
  if (variationHoldTimer) {
    window.clearTimeout(variationHoldTimer);
    variationHoldTimer = null;
  }
  variationTargetSlot = null;
  variationTargetDelta = 0;
}

function clearImpulseInputs() {
  clearVariationHold();
  currentSlotVariation = 0;
  pendingUserAction = false;
  pendingPresencePulse = false;
}

function clearPendingImpulses() {
  currentSlotVariation = 0;
  pendingUserAction = false;
  pendingPresencePulse = false;
}

function stepVariation(delta) {
  if (variationTargetSlot === null) {
    variationTargetSlot = absoluteSlot;
    variationTargetDelta = 0;
  }

  variationTargetDelta = clamp(variationTargetDelta + delta, variationMin, variationMax);
  absoluteSlot = variationTargetSlot;
  currentSlotVariation = variationTargetDelta;
  pendingUserAction = true;
  pendingPresencePulse = true;
  executeSlot(false);

  if (variationHoldTimer) {
    window.clearTimeout(variationHoldTimer);
  }
  variationHoldTimer = window.setTimeout(clearVariationHold, variationHoldMs);
}

function pulsePresence() {
  clearVariationHold();
  currentSlotVariation = 0;
  pendingUserAction = false;
  pendingPresencePulse = true;
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
    `presence=${entry.presenceDetected ? "yes" : "no"}`,
    entry.presenceDetected !== entry.previousPresenceDetected ? "presence-updated" : null,
    `measured=${entry.measured.toFixed(1)}`,
    `power=${entry.power}`,
  ].filter(Boolean).join(" | ");

  serialLog.textContent = `${line}\n${serialLog.textContent}`.slice(0, 12000);
}

function render(entry, replayOnly) {
  slotLabel.textContent = formatSlot(entry.slotOfWeek);
  absoluteLabel.textContent = `Execution ${entry.absoluteSlot}`;
  decisionLabel.textContent = entry.heating ? "Chauffe" : "Arret";
  targetLabel.textContent = `Consigne ${entry.learnedTarget.toFixed(1)} C`;
  powerLabel.textContent = `${entry.power} %`;
  modeLabel.textContent = `Mode ${entry.mode}`;
  weekResults[entry.slotOfWeek] = entry;
  drawChart();
  appendLog(entry, replayOnly);
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
  const rawTemps = values.flatMap((entry) => [
    entry.learnedTarget,
    entry.baseTemp + entry.userVariation,
  ]);
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

  weekResults.forEach((entry, slot) => {
    if (!entry || Math.abs(entry.userVariation) < 0.001) {
      return;
    }
    const x = chartX(slot, bounds);
    const y = chartY(entry.baseTemp + entry.userVariation, minTemp, maxTemp, bounds);
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
  const json = wasm.evaluateSlot(
    absoluteSlot,
    readNumber(measuredTempInput),
    userVariation,
    effectivePresence ? 1 : 0,
    replayOnly ? 1 : 0,
    explicitUserAction ? 1 : 0,
    temporaryOverride ? 1 : 0,
  );
  if (!replayOnly) {
    clearPendingImpulses();
  }
  const entry = JSON.parse(json);
  render(entry, replayOnly);
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
  executeSlot(false);
}

function resetSimulation() {
  stopPlayback();
  absoluteSlot = 0;
  weekResults = Array(slotsPerWeek).fill(null);
  if (wasm.setup) {
    wasm.setup(readNumber(baseTempInput));
  } else if (wasm.reset) {
    wasm.reset();
  }
  clearImpulseInputs();
  serialLog.textContent = "";
  executeSlot(false);
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
  ]);
  wasm.setup = module.cwrap("setupThermostat", null, ["number"]);
  wasm.reset = module.cwrap("resetThermostat", null, []);
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

variationMinusButton.addEventListener("click", () => stepVariation(-variationStep));
variationPlusButton.addEventListener("click", () => stepVariation(variationStep));
presenceToggle.addEventListener("click", () => {
  setPresence(!presenceDetected);
  clearImpulseInputs();
  executeSlot(true);
});
presencePulseButton.addEventListener("click", pulsePresence);

weekChart.addEventListener("click", (event) => {
  const rect = weekChart.getBoundingClientRect();
  const bounds = getChartBounds(Math.max(320, Math.floor(rect.width)), 300);
  const localX = event.clientX - rect.left;
  const ratio = clamp((localX - bounds.left) / bounds.width, 0, 1);
  seekToSlot(Math.round(ratio * (slotsPerWeek - 1)));
});

baseTempInput.addEventListener("change", resetSimulation);
window.addEventListener("resize", drawChart);
